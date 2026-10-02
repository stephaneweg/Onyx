/*
 * user/av/av_dav1d.c -- AV1 on dav1d 1.5.1 (BSD; its AArch64 assembly on the Pi): compiled with
 * AV_WITH_DAV1D (user/av/codecs.mk; third_party/dav1d-1.5.1/README.onyx). Main profile, 8 bits
 * (10-bit pictures are not converted: skipped). One thread, no frame threading (Onyx's threads
 * share core 0; on the Pi dav1d's pthreads are a stand-in that never starts one).
 * Smooth on a Pi 4: 640 x 360 at 30 fps advertised -- AV1 costs about 1.5 x VP9 (a 480p clip at
 * YouTube's rate: ~25 ms a frame under qemu, its key frames several times that), so 480p would
 * share one core too tightly with the page (docs/06 §44).
 */
#ifdef AV_WITH_DAV1D
#include "av_int.h"
#include "dav1d/dav1d.h"

struct av1 {
	Dav1dContext *ctx;
	Dav1dPicture pic;
	int have_pic;
	int pending;		/* data not taken yet (EAGAIN) */
	Dav1dData data;
};

static void *av1_open(const struct av_track *t)
{
	struct av1 *c = (struct av1 *) calloc(1, sizeof *c);
	Dav1dSettings s;

	(void) t;
	if (c == NULL)
		return NULL;
	dav1d_default_settings(&s);
	s.n_threads = 1;
	s.max_frame_delay = 1;
	if (dav1d_open(&c->ctx, &s) < 0) {
		free(c);
		return NULL;
	}
	return c;
}

static int av1_send(void *cv, const struct av_packet *p)
{
	struct av1 *c = (struct av1 *) cv;
	uint8_t *buf;
	int r;

	if (p == NULL)
		return AV_OK;
	if (c->data.sz > 0)
		dav1d_data_unref(&c->data);
	buf = dav1d_data_create(&c->data, p->size);
	if (buf == NULL)
		return AV_ENOMEM;
	memcpy(buf, p->data, p->size);
	c->data.m.timestamp = p->pts;
	c->data.m.duration = p->dur;
	r = dav1d_send_data(c->ctx, &c->data);
	if (r < 0 && r != DAV1D_ERR(EAGAIN)) {
		dav1d_data_unref(&c->data);
		return AV_ERR;
	}
	return AV_OK;
}

static int av1_receive(void *cv, struct av_frame *f)
{
	struct av1 *c = (struct av1 *) cv;
	int r;

	if (c->have_pic) {
		dav1d_picture_unref(&c->pic);
		c->have_pic = 0;
	}
	r = dav1d_get_picture(c->ctx, &c->pic);
	if (r < 0) {
		/* the data not all taken: give the rest now */
		if (c->data.sz > 0 && dav1d_send_data(c->ctx, &c->data) == 0)
			r = dav1d_get_picture(c->ctx, &c->pic);
		if (r < 0)
			return AV_AGAIN;
	}
	c->have_pic = 1;
	if (c->pic.p.bpc != 8 || c->pic.p.layout != DAV1D_PIXEL_LAYOUT_I420)
		return AV_AGAIN;
	memset(f, 0, sizeof *f);
	f->kind = AV_VIDEO;
	f->pts = c->pic.m.timestamp;
	f->dur = c->pic.m.duration;
	f->pix = AV_PIX_I420;
	f->width = c->pic.p.w;
	f->height = c->pic.p.h;
	f->plane[0] = (uint8_t *) c->pic.data[0];
	f->plane[1] = (uint8_t *) c->pic.data[1];
	f->plane[2] = (uint8_t *) c->pic.data[2];
	f->stride[0] = (int) c->pic.stride[0];
	f->stride[1] = f->stride[2] = (int) c->pic.stride[1];
	f->colorspace = c->pic.seq_hdr->mtrx == DAV1D_MC_BT709 ? AV_CS_BT709 : AV_CS_BT601;
	f->full_range = c->pic.seq_hdr->color_range;
	return AV_OK;
}

static void av1_flush(void *cv)
{
	struct av1 *c = (struct av1 *) cv;
	if (c->have_pic)
		dav1d_picture_unref(&c->pic);
	c->have_pic = 0;
	if (c->data.sz > 0)
		dav1d_data_unref(&c->data);
	dav1d_flush(c->ctx);
}

static void av1_close(void *cv)
{
	struct av1 *c = (struct av1 *) cv;
	av1_flush(c);
	dav1d_close(&c->ctx);
	free(c);
}

const struct av_codec_impl av_av1_codec = {
	AV_C_AV1, AV_VIDEO, "av1", "dav1d", 640, 360, 30,
	av1_open, av1_send, av1_receive, av1_flush, av1_close, NULL
};
#endif
