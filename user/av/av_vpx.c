/*
 * user/av/av_vpx.c -- VP8 and VP9 on libvpx (BSD; NEON on AArch64): compiled with AV_WITH_VPX
 * when third_party/libvpx-<version> is vendored (docs/06 §44: not vendored yet -- this glue has
 * not been compiled; its API is libvpx's stable vpx_decoder.h).
 *
 * One thread (the player's video thread; Onyx's threads share core 0), frames handed out as
 * libvpx's I420 planes (valid until the next decode). Profile 0 (8 bits, 4:2:0) only.
 * Smooth on a Pi 4 (estimated from libvpx's NEON speed on Cortex-A72: ~2.5 ms for a 360p frame,
 * ~5 ms for 480p, ~12 ms for 720p on one core) -- 854 x 480 at 30 fps advertised.
 */
#ifdef AV_WITH_VPX
#include "av_int.h"
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"

struct vpx {
	vpx_codec_ctx_t ctx;
	vpx_codec_iter_t it;
	av_us pts, dur;
};

static void *vpx_open(const struct av_track *t)
{
	struct vpx *c = (struct vpx *) calloc(1, sizeof *c);
	vpx_codec_dec_cfg_t cfg;

	if (c == NULL)
		return NULL;
	memset(&cfg, 0, sizeof cfg);
	cfg.threads = 1;
	cfg.w = (unsigned) t->width;
	cfg.h = (unsigned) t->height;
	if (vpx_codec_dec_init(&c->ctx, t->codec == AV_C_VP9 ? vpx_codec_vp9_dx() : vpx_codec_vp8_dx(),
			&cfg, 0) != VPX_CODEC_OK) {
		free(c);
		return NULL;
	}
	return c;
}

static int vpx_send(void *cv, const struct av_packet *p)
{
	struct vpx *c = (struct vpx *) cv;

	c->it = NULL;
	if (p == NULL)
		return vpx_codec_decode(&c->ctx, NULL, 0, NULL, 0) == VPX_CODEC_OK ? AV_OK : AV_ERR;
	c->pts = p->pts;
	c->dur = p->dur;
	return vpx_codec_decode(&c->ctx, p->data, (unsigned) p->size, NULL, 0) == VPX_CODEC_OK ? AV_OK : AV_ERR;
}

static int vpx_receive(void *cv, struct av_frame *f)
{
	struct vpx *c = (struct vpx *) cv;
	vpx_image_t *img = vpx_codec_get_frame(&c->ctx, &c->it);

	if (img == NULL)
		return AV_AGAIN;
	if (img->fmt != VPX_IMG_FMT_I420)
		return AV_AGAIN;	/* (4:4:4, 10 bits: profiles we do not offer) */
	memset(f, 0, sizeof *f);
	f->kind = AV_VIDEO;
	f->pts = c->pts;
	f->dur = c->dur;
	f->pix = AV_PIX_I420;
	f->width = (int) img->d_w;
	f->height = (int) img->d_h;
	f->plane[0] = img->planes[0];
	f->plane[1] = img->planes[1];
	f->plane[2] = img->planes[2];
	f->stride[0] = img->stride[0];
	f->stride[1] = img->stride[1];
	f->stride[2] = img->stride[2];
	f->colorspace = img->cs == VPX_CS_BT_709 ? AV_CS_BT709 : img->cs == VPX_CS_BT_2020 ? AV_CS_BT2020 : AV_CS_BT601;
	f->full_range = img->range == VPX_CR_FULL_RANGE;
	return AV_OK;
}

static void vpx_flush(void *cv) { ((struct vpx *) cv)->it = NULL; }

static void vpx_close(void *cv)
{
	struct vpx *c = (struct vpx *) cv;
	vpx_codec_destroy(&c->ctx);
	free(c);
}

/* vp09.PP.LL.DD...: profile 0, 8 bits (av_codec_of_string checks the same) */
static int vp9_profile(const char *s)
{
	return strncmp(s, "vp09.", 5) || (s[5] == '0' && s[6] == '0');
}

const struct av_codec_impl av_vp9_codec = {
	AV_C_VP9, AV_VIDEO, "vp9", "libvpx", 854, 480, 30,
	vpx_open, vpx_send, vpx_receive, vpx_flush, vpx_close, vp9_profile
};
const struct av_codec_impl av_vp8_codec = {
	AV_C_VP8, AV_VIDEO, "vp8", "libvpx", 854, 480, 30,
	vpx_open, vpx_send, vpx_receive, vpx_flush, vpx_close, NULL
};
#endif
