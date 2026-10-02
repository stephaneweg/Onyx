#!/usr/bin/env python3
"""
tools/tests/av/mkcodec.py -- the compressed test clips of the media tests (VP9, AV1, Opus), made
from mkmedia.py's synthetic picture and sound (nothing copyrighted). Needs PyAV (pip install av:
FFmpeg with libvpx, SVT-AV1, libopus, libdav1d); its output is committed in tools/tests/av/clips/
so the tests do not need it:

    python3 tools/tests/av/mkcodec.py tools/tests/av/clips

Files (96 x 64, 25 fps, 2 s, a key frame each 0.5 s; Opus 48 kHz stereo, 20 ms packets):
  vp9.webm        VP9 + Opus, Matroska with Cues
  av1.mp4         AV1 + Opus, MP4 (moov first)
  vp9-mse.webm    VP9 + Opus for MSE: init segment, one cluster each 0.5 s; vp9-mse.json: ranges
  av1-frag.mp4    AV1 + Opus, fragmented MP4 (one fragment each 0.5 s); av1-frag.json: ranges
  <clip>.txt      per frame: index, pts (us), 0, the RGB sum of the frame as decoded by the
                  reference decoders (libvpx's VP9 = FFmpeg's, libdav1d), converted as
                  user/av/av_yuv.c does (mkmedia.rgb_sum)
"""
import io, json, os, struct, sys
import av
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkmedia as M

W, H, FPS, SECONDS = M.W, M.H, M.FPS, 2
AR, ACH = 48000, 2


def encode(path, fmt, vcodec, options=None, vopts=None):
    """the clip: frames forced key at mkmedia.is_key, the sound in 20 ms Opus packets"""
    out = av.open(path, 'w', format=fmt, options=options or {})
    vs = out.add_stream(vcodec, rate=FPS)
    vs.width, vs.height, vs.pix_fmt = W, H, 'yuv420p'
    vs.time_base = Fraction(1, FPS)
    vs.codec_context.gop_size = 1000
    if vopts:
        vs.codec_context.options = vopts
    as_ = out.add_stream('libopus', rate=AR)
    as_.layout = 'stereo'
    as_.codec_context.bit_rate = 64000
    pcm = M.audio_pcm(SECONDS, AR, ACH)
    cw, ch = (W + 1) // 2, (H + 1) // 2
    nfr = SECONDS * FPS
    vi, ai = 0, 0
    apk = 960
    while vi < nfr or ai * apk < SECONDS * AR:
        # interleave by time
        if vi < nfr and (ai * apk >= SECONDS * AR or vi / FPS <= ai * apk / AR):
            fr = M.frame_yuv(vi)
            f = av.VideoFrame(W, H, 'yuv420p')
            for p, (o, w, h) in enumerate(((0, W, H), (W * H, cw, ch), (W * H + cw * ch, cw, ch))):
                ls = f.planes[p].line_size
                buf = bytearray(ls * h)
                for r in range(h):
                    buf[r * ls:r * ls + w] = fr[o + r * w:o + (r + 1) * w]
                f.planes[p].update(bytes(buf))
            f.pts = vi
            f.time_base = Fraction(1, FPS)
            f.pict_type = av.video.frame.PictureType.I if M.is_key(vi) else av.video.frame.PictureType.NONE
            for pk in vs.encode(f):
                out.mux(pk)
            vi += 1
        else:
            n = min(apk, SECONDS * AR - ai * apk)
            chunk = pcm[ai * apk * ACH * 2:(ai * apk + n) * ACH * 2]
            if n < apk:
                chunk += b'\0' * ((apk - n) * ACH * 2)
            af = av.AudioFrame(format='s16', layout='stereo', samples=apk)
            af.planes[0].update(chunk)
            af.sample_rate = AR
            af.pts = ai * apk
            af.time_base = Fraction(1, AR)
            for pk in as_.encode(af):
                out.mux(pk)
            ai += 1
    for pk in vs.encode(None):
        out.mux(pk)
    for pk in as_.encode(None):
        out.mux(pk)
    out.close()


def ref_sums(path, txt):
    """the frames as the reference decoders give them -> mkmedia.rgb_sum"""
    c = av.open(path)
    s = c.streams.video[0]
    if s.codec_context.name in ('av1', 'libdav1d'):
        dec = av.codec.CodecContext.create('libdav1d', 'r')
        dec.extradata = s.codec_context.extradata
    else:
        dec = None
    lines = []
    i = 0

    def take(fr):
        nonlocal i
        cw, ch = (W + 1) // 2, (H + 1) // 2
        y = b''.join(bytes(fr.planes[0])[r * fr.planes[0].line_size:r * fr.planes[0].line_size + W] for r in range(H))
        u = b''.join(bytes(fr.planes[1])[r * fr.planes[1].line_size:r * fr.planes[1].line_size + cw] for r in range(ch))
        v = b''.join(bytes(fr.planes[2])[r * fr.planes[2].line_size:r * fr.planes[2].line_size + cw] for r in range(ch))
        lines.append('%d %d 0 %d' % (i, i * 1000000 // FPS, M.rgb_sum(y + u + v)))
        i += 1

    for pk in c.demux(s):
        if dec is None:
            for fr in pk.decode():
                take(fr)
        elif pk.size:
            for fr in dec.decode(pk):
                take(fr)
    if dec is not None:
        for fr in dec.decode(None):
            take(fr)
    c.close()
    with open(txt, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return i


def ebml_ranges(path):
    """an MSE WebM: [init] + one range per Cluster, with its timecode"""
    d = open(path, 'rb').read()

    def vint(p):
        b = d[p]
        n = 1
        while not b & (0x80 >> (n - 1)):
            n += 1
        v = b & ((0x80 >> (n - 1)) - 1)
        for k in range(1, n):
            v = (v << 8) | d[p + k]
        return v, n, v == (1 << (7 * n)) - 1

    def eid(p):
        b = d[p]
        n = 1
        while not b & (0x80 >> (n - 1)):
            n += 1
        return int.from_bytes(d[p:p + n], 'big'), n

    p = 0
    i, n = eid(p)
    sz, m, _ = vint(p + n)
    p += n + m + sz                       # EBML header
    i, n = eid(p)
    sz, m, unknown = vint(p + n)
    p += n + m                            # into the Segment
    ranges = []
    while p < len(d):
        i, n = eid(p)
        sz, m, _ = vint(p + n)
        if i == 0x1F43B675:
            if not ranges:
                ranges.append([0, p, -1])
            tc_id, tn = eid(p + n + m)
            tsz, tm, _ = vint(p + n + m + tn)
            tc = int.from_bytes(d[p + n + m + tn + tm:p + n + m + tn + tm + tsz], 'big')
            ranges.append([p, p + n + m + sz, tc / 1000.0])
        p += n + m + sz
    return ranges


def mp4_ranges(path):
    d = open(path, 'rb').read()
    p, ranges, start, t = 0, [], None, 0
    while p < len(d):
        sz, typ = struct.unpack('>I4s', d[p:p + 8])
        if typ == b'moof':
            if not ranges:
                ranges.append([0, p, -1])
            start = p
        if typ == b'mdat' and start is not None:
            ranges.append([start, p + sz, round(t * 0.5, 3)])
            t += 1
            start = None
        p += sz
    return ranges


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    j = lambda *a: os.path.join(out, *a)
    encode(j('vp9.webm'), 'webm', 'libvpx-vp9', vopts={'deadline': 'good', 'cpu-used': '4', 'crf': '30', 'b': '0'})
    encode(j('av1.mp4'), 'mp4', 'libsvtav1', options={'movflags': '+faststart'}, vopts={'preset': '8', 'crf': '40'})
    encode(j('vp9-mse.webm'), 'webm', 'libvpx-vp9', options={'live': '1', 'cluster_time_limit': '500'},
           vopts={'deadline': 'good', 'cpu-used': '4', 'crf': '30', 'b': '0'})
    encode(j('av1-frag.mp4'), 'mp4', 'libsvtav1',
           options={'movflags': '+frag_keyframe+empty_moov+default_base_moof'}, vopts={'preset': '8', 'crf': '40'})
    for name in ('vp9.webm', 'av1.mp4', 'vp9-mse.webm', 'av1-frag.mp4'):
        n = ref_sums(j(name), j(name.rsplit('.', 1)[0] + '.txt'))
        print(name, os.path.getsize(j(name)), 'bytes,', n, 'frames')
    json.dump(ebml_ranges(j('vp9-mse.webm')), open(j('vp9-mse.json'), 'w'))
    json.dump(mp4_ranges(j('av1-frag.mp4')), open(j('av1-frag.json'), 'w'))
    print(open(j('vp9-mse.json')).read())
    print(open(j('av1-frag.json')).read())


if __name__ == '__main__':
    main()
