/*
 * user/av/av_store.h -- the store's calls for the player (av_player.c): taken with the store's
 * lock held (av__store_lock / av__store_unlock), except where said.
 */
#ifndef ONYX_AV_STORE_H
#define ONYX_AV_STORE_H

#include "av.h"

void av__store_lock(struct av_store *s);
void av__store_unlock(struct av_store *s);
unsigned av__store_serial(struct av_store *s);
void av__store_set_now(struct av_store *s, av_us t);
/* the first source / track of a kind whose init segment came: 1 found (*info: a copy, no extra) */
int av__store_pick(struct av_store *s, int kind, int *src, int *track, struct av_track *info);
const struct av_track *av__store_track_info(struct av_store *s, int src, int track);
/* the first frame with dts > after (>= when inclusive; after AV_NOTIME: the first): a copy.
 * AV_AGAIN: not there yet (or a hole of more than gap after `after`), AV_EOF: ended */
int av__store_next(struct av_store *s, int src, int track, av_us after, int inclusive, av_us gap,
		struct av_packet *p);
/* the decode time of the random access point to start from to show time t */
av_us av__store_rap_before(struct av_store *s, int src, int track, av_us t);
int av__store_ended(struct av_store *s, int src);
int av__store_is_file(struct av_store *s, int src);
/* how far the buffered data reaches after t (0: t is not buffered) */
av_us av__store_ahead(struct av_store *s, av_us t);
/* the file mode: frames before `before` dropped */
void av__store_trim(struct av_store *s, av_us before);
/* the file mode: the demuxer sent to t (takes the lock itself) -> 0, -1 not seekable */
int av__store_seek_file(struct av_store *s, int src, av_us t);
/* a decoder exists for the track (av_codec.c) */
int av_decoder_supported_track(const struct av_track *t);

#endif
