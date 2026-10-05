/*
 * user/Libs/av/av_yuv.c -- YUV 4:2:0 -> 32-bit RGB (BT.601 / BT.709, limited or full range).
 *
 * 16-bit fixed point with 6 fractional bits, the sums saturated (they only saturate past the
 * 0..255 clamp, so the result is exact): the NEON path (AArch64, 16 pixels a step) and the C
 * path compute the same numbers bit for bit (tools/tests/av checks them against each other
 * under qemu-aarch64). Chroma is nearest (each sample covers 2 x 2 pixels).
 */
#include "av_int.h"
#if defined(__aarch64__) && !defined(AV_NO_NEON)
#include <arm_neon.h>
#define AV_NEON 1
#endif

struct coefs { int ymul, yoff, vr, ug, vg, ub; };

/* ymul: Y x ymul >> 1 (149 = 1.164 x 128; 128 full range), yoff subtracted; the chroma's x 64 */
static const struct coefs C601L = { 149, 1192, 102, 25, 52, 129 };
static const struct coefs C709L = { 149, 1192, 115, 14, 34, 135 };
static const struct coefs C601F = { 128, 0, 90, 22, 46, 113 };
static const struct coefs C709F = { 128, 0, 101, 12, 30, 119 };

static inline int sat16(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static inline uint8_t out8(int v) { v = (v + 32) >> 6; return (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : v); }

static void row_c(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *d, int x0, int w,
		const struct coefs *k, int bgr)
{
	int x;

	for (x = x0; x < w; x++) {
		int yy = ((y[x] * k->ymul) >> 1) - k->yoff;
		int uu = u[x >> 1] - 128, vv = v[x >> 1] - 128;
		int r = sat16(yy + k->vr * vv);
		int g = sat16(yy - (k->ug * uu + k->vg * vv));
		int b = sat16(yy + k->ub * uu);
		uint8_t *o = d + 4 * x;
		o[bgr ? 2 : 0] = out8(r);
		o[1] = out8(g);
		o[bgr ? 0 : 2] = out8(b);
		o[3] = 255;
	}
}

#ifdef AV_NEON
/* 16 pixels a step; -> the pixels done */
static int row_neon(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *d, int w,
		const struct coefs *k, int bgr)
{
	int x;
	const uint8x8_t c128 = vdup_n_u8(128);
	const uint8x8_t ymul = vdup_n_u8((uint8_t) k->ymul);
	const int16x8_t yoff = vdupq_n_s16((int16_t) k->yoff);
	const uint8x16_t alpha = vdupq_n_u8(255);

	for (x = 0; x + 16 <= w; x += 16) {
		uint8x16_t yv = vld1q_u8(y + x);
		int16x8_t uu = vreinterpretq_s16_u16(vsubl_u8(vld1_u8(u + (x >> 1)), c128));
		int16x8_t vv = vreinterpretq_s16_u16(vsubl_u8(vld1_u8(v + (x >> 1)), c128));
		int16x8_t rv = vmulq_n_s16(vv, (int16_t) k->vr);
		int16x8_t guv = vmlaq_n_s16(vmulq_n_s16(uu, (int16_t) k->ug), vv, (int16_t) k->vg);
		int16x8_t bu = vmulq_n_s16(uu, (int16_t) k->ub);
		int16x8_t ylo = vsubq_s16(vreinterpretq_s16_u16(vshrq_n_u16(vmull_u8(vget_low_u8(yv), ymul), 1)), yoff);
		int16x8_t yhi = vsubq_s16(vreinterpretq_s16_u16(vshrq_n_u16(vmull_u8(vget_high_u8(yv), ymul), 1)), yoff);
		int16x8_t rlo = vzip1q_s16(rv, rv), rhi = vzip2q_s16(rv, rv);
		int16x8_t glo = vzip1q_s16(guv, guv), ghi = vzip2q_s16(guv, guv);
		int16x8_t blo = vzip1q_s16(bu, bu), bhi = vzip2q_s16(bu, bu);
		uint8x16_t R = vcombine_u8(vqrshrun_n_s16(vqaddq_s16(ylo, rlo), 6), vqrshrun_n_s16(vqaddq_s16(yhi, rhi), 6));
		uint8x16_t G = vcombine_u8(vqrshrun_n_s16(vqsubq_s16(ylo, glo), 6), vqrshrun_n_s16(vqsubq_s16(yhi, ghi), 6));
		uint8x16_t B = vcombine_u8(vqrshrun_n_s16(vqaddq_s16(ylo, blo), 6), vqrshrun_n_s16(vqaddq_s16(yhi, bhi), 6));
		uint8x16x4_t o;
		o.val[0] = bgr ? B : R;
		o.val[1] = G;
		o.val[2] = bgr ? R : B;
		o.val[3] = alpha;
		vst4q_u8(d + 4 * x, o);
	}
	return x;
}
#endif

void av_yuv_to_rgb(const struct av_frame *f, uint8_t *dst, int dst_stride, int pix)
{
	const struct coefs *k;
	int row, bgr = pix == AV_PIX_BGRA;

	if (f->pix == AV_PIX_RGBA || f->pix == AV_PIX_BGRA) {
		/* already 32-bit: copied (swapped if the order differs) */
		for (row = 0; row < f->height; row++) {
			const uint8_t *s = f->plane[0] + (size_t) row * f->stride[0];
			uint8_t *d = dst + (size_t) row * dst_stride;
			if (f->pix == pix) {
				memcpy(d, s, (size_t) f->width * 4);
			} else {
				int x;
				for (x = 0; x < f->width; x++) {
					d[4 * x] = s[4 * x + 2];
					d[4 * x + 1] = s[4 * x + 1];
					d[4 * x + 2] = s[4 * x];
					d[4 * x + 3] = s[4 * x + 3];
				}
			}
		}
		return;
	}
	if (f->colorspace == AV_CS_BT709)
		k = f->full_range ? &C709F : &C709L;
	else
		k = f->full_range ? &C601F : &C601L;
	for (row = 0; row < f->height; row++) {
		const uint8_t *y = f->plane[0] + (size_t) row * f->stride[0];
		const uint8_t *u = f->plane[1] + (size_t) (row >> 1) * f->stride[1];
		const uint8_t *v = f->plane[2] + (size_t) (row >> 1) * f->stride[2];
		uint8_t *d = dst + (size_t) row * dst_stride;
		int x0 = 0;
#ifdef AV_NEON
		x0 = row_neon(y, u, v, d, f->width, k, bgr);
#endif
		row_c(y, u, v, d, x0, f->width, k, bgr);
	}
}

/* the C path alone (the tests compare it with the NEON one) */
void av__yuv_to_rgb_c(const struct av_frame *f, uint8_t *dst, int dst_stride, int pix)
{
	const struct coefs *k = f->colorspace == AV_CS_BT709 ? (f->full_range ? &C709F : &C709L) :
		(f->full_range ? &C601F : &C601L);
	int row;

	for (row = 0; row < f->height; row++)
		row_c(f->plane[0] + (size_t) row * f->stride[0], f->plane[1] + (size_t) (row >> 1) * f->stride[1],
			f->plane[2] + (size_t) (row >> 1) * f->stride[2], dst + (size_t) row * dst_stride, 0,
			f->width, k, pix == AV_PIX_BGRA);
}
