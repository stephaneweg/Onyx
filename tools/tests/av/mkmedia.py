#!/usr/bin/env python3
"""
tools/tests/av/mkmedia.py -- synthetic test media for the media library and Jet Browser's
<video> / <audio> / MSE tests (nothing copyrighted, nothing downloaded: made here).

    python3 tools/tests/av/mkmedia.py <outdir>

The video is uncompressed I420 (Matroska V_UNCOMPRESSED, MP4 'i420': the library's test
codec), W x H at 25 fps: a grey ramp, a white bar at x = (3 i) mod W (frame i), a full-white
"flash" frame every 0.5 s (i % 12 == 0 ... see FLASH), chroma that turns with i. The audio is a
440 Hz sine with a 1 kHz burst at every 0.5 s (A/V sync: the flash and the burst share a time).

Files:
  clip.webm        Matroska: video + PCM s16 stereo 48 kHz, 2 s, Cues + SeekHead (seekable)
  clip.mp4         MP4, moov first: i420 + sowt (PCM s16 LE)
  clip-moovend.mp4 MP4, moov after the mdat
  frag.mp4         fragmented MP4 (MSE): init + 0.5 s fragments; frag.json: the byte ranges
  mse.webm         WebM for MSE: init segment + one cluster each 0.5 s; mse.json: the ranges
  tone.wav         PCM s16 mono 22050 Hz, 1 s
  tone.flac        FLAC (this file's encoder: fixed / LPC predictors, Rice partitions,
                   stereo decorrelation), 44.1 kHz stereo 16-bit, 1.5 s; tone-ref.raw: its PCM
  frames.txt       per frame: index, pts (us), the sum of its I420 bytes
"""
import json, math, os, struct, sys

W, H, FPS, SECONDS = 96, 64, 25, 2
AR, ACH = 48000, 2
FLASH_EVERY = 0.5

def is_key(i):
    """the first frame of each 0.5 s: a random access point (the containers flag it)"""
    return i == 0 or int(i / FPS / 0.5) != int((i - 1) / FPS / 0.5)

def frame_yuv(i):
    cw, ch = (W + 1) // 2, (H + 1) // 2
    y = bytearray(W * H)
    flash = (i * 1.0 / FPS) % FLASH_EVERY < 1e-9
    bar = (3 * i) % W
    for r in range(H):
        for c in range(W):
            v = 235 if flash else (16 + (c * 200) // W)
            if c == bar or c == bar + 1:
                v = 235 if not flash else 16
            y[r * W + c] = v
    u = bytes(int(128 + 40 * math.cos(i / 5.0) + (c % 7)) & 255 for _ in range(ch) for c in range(cw))
    v = bytes(int(128 + 40 * math.sin(i / 5.0)) & 255 for _ in range(cw * ch))
    return bytes(y) + u + v

def rgb_sum(fr):
    """the sum of R + G + B of a frame converted as user/av/av_yuv.c does (BT.601, limited)"""
    cw, ch = (W + 1) // 2, (H + 1) // 2
    Y, U, V = fr[:W * H], fr[W * H:W * H + cw * ch], fr[W * H + cw * ch:]
    sat = lambda v: 32767 if v > 32767 else -32768 if v < -32768 else v
    o8 = lambda v: min(255, max(0, (v + 32) >> 6))
    s = 0
    for r in range(H):
        for c in range(W):
            yy = ((Y[r * W + c] * 149) >> 1) - 1192
            uu = U[(r >> 1) * cw + (c >> 1)] - 128
            vv = V[(r >> 1) * cw + (c >> 1)] - 128
            s += o8(sat(yy + 102 * vv)) + o8(sat(yy - (25 * uu + 52 * vv))) + o8(sat(yy + 129 * uu))
    return s

def audio_pcm(seconds, rate, ch):
    out = bytearray()
    n = int(seconds * rate)
    for k in range(n):
        t = k / rate
        s = 0.3 * math.sin(2 * math.pi * 440 * t)
        if (t % FLASH_EVERY) < 0.02:          # the burst
            s += 0.5 * math.sin(2 * math.pi * 1000 * t)
        v = int(max(-1, min(1, s)) * 32767)
        out += struct.pack('<h', v) * ch
    return bytes(out)

# ---------------------------------------------------------------- Matroska
def vint_size(n):
    return b'\x01' + n.to_bytes(7, 'big')
def el(id_, data):
    return id_.to_bytes((id_.bit_length() + 7) // 8, 'big') + vint_size(len(data)) + data
def uint(id_, v):
    n = max(1, (v.bit_length() + 7) // 8)
    return el(id_, v.to_bytes(n, 'big'))
def flt(id_, v):
    return el(id_, struct.pack('>d', v))
def string(id_, s):
    return el(id_, s.encode())

def mkv_tracks(with_audio=True):
    vid = el(0xAE, uint(0xD7, 1) + uint(0x73C5, 1) + uint(0x83, 1) + string(0x86, 'V_UNCOMPRESSED') +
             uint(0x23E383, 1000000000 // FPS) +
             el(0xE0, uint(0xB0, W) + uint(0xBA, H) + el(0x2EB524, b'I420')))
    tr = vid
    if with_audio:
        aud = el(0xAE, uint(0xD7, 2) + uint(0x73C5, 2) + uint(0x83, 2) + string(0x86, 'A_PCM/INT/LIT') +
                 el(0xE1, flt(0xB5, float(AR)) + uint(0x9F, ACH) + uint(0x6264, 16)))
        tr += aud
    return el(0x1654AE6B, tr)

def mkv_header(doc='webm'):
    return el(0x1A45DFA3, uint(0x4286, 1) + uint(0x42F7, 1) + uint(0x42F2, 4) + uint(0x42F3, 8) +
              string(0x4282, doc) + uint(0x4287, 4) + uint(0x4285, 2))

def mkv_info(dur_s):
    return el(0x1549A966, uint(0x2AD7B1, 1000000) + flt(0x4489, dur_s * 1000.0) +
              string(0x4D80, 'mkmedia') + string(0x5741, 'mkmedia'))

def simple_block(track, rel_ms, key, data):
    return el(0xA3, bytes([0x80 | track]) + struct.pack('>h', rel_ms) + bytes([0x80 if key else 0]) + data)

def mkv_clusters(frames, pcm, cluster_s=0.5, with_audio=True):
    """-> list of (start_ms, cluster bytes)"""
    out = []
    nclusters = int(math.ceil(SECONDS / cluster_s))
    abytes_per_ms = AR * ACH * 2 // 1000
    for c in range(nclusters):
        t0 = int(c * cluster_s * 1000)
        t1 = int((c + 1) * cluster_s * 1000)
        body = uint(0xE7, t0)
        blocks = []
        for i, f in enumerate(frames):
            ms = i * 1000 // FPS
            if t0 <= ms < t1:
                blocks.append((ms, 0, simple_block(1, ms - t0, is_key(i), f)))
        if with_audio:
            # audio blocks of 20 ms
            for ms in range(t0, min(t1, SECONDS * 1000), 20):
                chunk = pcm[ms * abytes_per_ms:(ms + 20) * abytes_per_ms]
                blocks.append((ms, 1, simple_block(2, ms - t0, True, chunk)))
        blocks.sort(key=lambda b: (b[0], b[1]))
        for b in blocks:
            body += b[2]
        out.append((t0, el(0x1F43B675, body)))
    return out

def write_webm(path, frames, pcm):
    """a seekable file: SeekHead -> Cues (at the end), one cue a cluster"""
    clusters = mkv_clusters(frames, pcm)
    info, tracks = mkv_info(SECONDS), mkv_tracks()
    def seekhead(cues_pos):
        return el(0x114D9B74, el(0x4DBB, el(0x53AB, b'\x1c\x53\xbb\x6b') + el(0x53AC, cues_pos.to_bytes(8, 'big'))))
    pos = len(seekhead(0)) + len(info) + len(tracks)
    cues = b''
    for t0, cl in clusters:
        cues += el(0xBB, uint(0xB3, t0) + el(0xB7, uint(0xF7, 1) + el(0xF1, pos.to_bytes(8, 'big'))))
        pos += len(cl)
    body = seekhead(pos) + info + tracks + b''.join(cl for _, cl in clusters) + el(0x1C53BB6B, cues)
    with open(path, 'wb') as f:
        f.write(mkv_header() + el(0x18538067, body))

def write_mse_webm(path, frames, pcm):
    """init segment (EBML + Segment of unknown size + Info + Tracks) then clusters"""
    init = mkv_header() + (0x18538067).to_bytes(4, 'big') + b'\x01\xff\xff\xff\xff\xff\xff\xff' + mkv_info(SECONDS) + mkv_tracks()
    data = init
    ranges = [[0, len(init), -1]]
    for t0, cl in mkv_clusters(frames, pcm):
        ranges.append([len(data), len(data) + len(cl), t0 / 1000.0])
        data += cl
    with open(path, 'wb') as f:
        f.write(data)
    with open(path[:-5] + '.json', 'w') as f:
        json.dump(ranges, f)

# ---------------------------------------------------------------- MP4
def box(t, data):
    return struct.pack('>I', 8 + len(data)) + t.encode() + data
def fbox(t, ver, flags, data):
    return box(t, bytes([ver]) + flags.to_bytes(3, 'big') + data)

VTS, ATS = 25 * 1000, AR    # timescales
def mp4_trak_video(n, offsets=None, sizes=None, frag=False):
    tkhd = fbox('tkhd', 0, 3, struct.pack('>IIIII', 0, 0, 1, 0, n * 1000 // FPS) + b'\0' * 8 +
                struct.pack('>hhhh', 0, 0, 0, 0) + struct.pack('>9I', 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000) +
                struct.pack('>II', W << 16, H << 16))
    mdhd = fbox('mdhd', 0, 0, struct.pack('>IIII', 0, 0, VTS, 0 if frag else n * VTS // FPS) + b'\x55\xc4\0\0')
    hdlr = fbox('hdlr', 0, 0, b'\0' * 4 + b'vide' + b'\0' * 12 + b'video\0')
    entry = box('i420', b'\0' * 6 + struct.pack('>H', 1) + b'\0' * 16 + struct.pack('>HH', W, H) +
                struct.pack('>II', 0x480000, 0x480000) + b'\0' * 4 + struct.pack('>H', 1) + b'\0' * 32 +
                struct.pack('>Hh', 24, -1))
    stsd = fbox('stsd', 0, 0, struct.pack('>I', 1) + entry)
    if frag:
        stbl = box('stbl', stsd + fbox('stts', 0, 0, b'\0' * 4) + fbox('stsc', 0, 0, b'\0' * 4) +
                   fbox('stsz', 0, 0, b'\0' * 8) + fbox('stco', 0, 0, b'\0' * 4))
    else:
        stts = fbox('stts', 0, 0, struct.pack('>III', 1, n, VTS // FPS))
        keys = [i + 1 for i in range(n) if is_key(i)]
        stss = fbox('stss', 0, 0, struct.pack('>I', len(keys)) + b''.join(struct.pack('>I', k) for k in keys))
        stsz = fbox('stsz', 0, 0, struct.pack('>II', 0, n) + b''.join(struct.pack('>I', s) for s in sizes))
        stsc = fbox('stsc', 0, 0, struct.pack('>IIII', 1, 1, 1, 1))
        stco = fbox('stco', 0, 0, struct.pack('>I', n) + b''.join(struct.pack('>I', o) for o in offsets))
        stbl = box('stbl', stsd + stts + stss + stsz + stsc + stco)
    minf = box('minf', fbox('vmhd', 0, 1, b'\0' * 8) + box('dinf', fbox('dref', 0, 0, struct.pack('>I', 1) + fbox('url ', 0, 1, b''))) + stbl)
    return box('trak', tkhd + box('mdia', mdhd + hdlr + minf))

def mp4_trak_audio(nsamp_total, chunks=None, frag=False):
    # chunks: list of (offset, nsamples) ; sample = one PCM frame (stsz const = 4)
    tkhd = fbox('tkhd', 0, 3, struct.pack('>IIIII', 0, 0, 2, 0, nsamp_total * 1000 // AR) + b'\0' * 8 +
                struct.pack('>hhhh', 0, 0, 0x100, 0) + struct.pack('>9I', 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000) +
                struct.pack('>II', 0, 0))
    mdhd = fbox('mdhd', 0, 0, struct.pack('>IIII', 0, 0, ATS, 0 if frag else nsamp_total) + b'\x55\xc4\0\0')
    hdlr = fbox('hdlr', 0, 0, b'\0' * 4 + b'soun' + b'\0' * 12 + b'sound\0')
    entry = box('sowt', b'\0' * 6 + struct.pack('>H', 1) + b'\0' * 8 + struct.pack('>HHHH', ACH, 16, 0, 0) +
                struct.pack('>I', AR << 16))
    stsd = fbox('stsd', 0, 0, struct.pack('>I', 1) + entry)
    if frag:
        stbl = box('stbl', stsd + fbox('stts', 0, 0, b'\0' * 4) + fbox('stsc', 0, 0, b'\0' * 4) +
                   fbox('stsz', 0, 0, b'\0' * 8) + fbox('stco', 0, 0, b'\0' * 4))
    else:
        # one "sample" per 20 ms chunk (960 frames): simpler tables, each a packet
        n = len(chunks)
        stts = fbox('stts', 0, 0, struct.pack('>III', 1, n, AR * 20 // 1000))
        stsz = fbox('stsz', 0, 0, struct.pack('>II', AR * 20 // 1000 * ACH * 2, n))
        stsc = fbox('stsc', 0, 0, struct.pack('>IIII', 1, 1, 1, 1))
        stco = fbox('stco', 0, 0, struct.pack('>I', n) + b''.join(struct.pack('>I', o) for o, _ in chunks))
        stbl = box('stbl', stsd + stts + stsz + stsc + stco)
    minf = box('minf', fbox('smhd', 0, 0, b'\0' * 4) + box('dinf', fbox('dref', 0, 0, struct.pack('>I', 1) + fbox('url ', 0, 1, b''))) + stbl)
    return box('trak', tkhd + box('mdia', mdhd + hdlr + minf))

def mvhd(dur_ms):
    return fbox('mvhd', 0, 0, struct.pack('>IIII', 0, 0, 1000, dur_ms) + struct.pack('>IH', 0x10000, 0x100) + b'\0' * 10 +
                struct.pack('>9I', 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000) + b'\0' * 24 + struct.pack('>I', 3))

def write_mp4(path, frames, pcm, moov_first=True):
    ftyp = box('ftyp', b'isom' + struct.pack('>I', 512) + b'isomiso2mp41')
    achunk = AR * 20 // 1000 * ACH * 2
    # the mdat: frames and 20 ms audio chunks interleaved by time
    items = [(i * 1000 // FPS, 0, f) for i, f in enumerate(frames)]
    items += [(ms, 1, pcm[ms * achunk // 20:(ms + 20) * achunk // 20]) for ms in range(0, SECONDS * 1000, 20)]
    items.sort(key=lambda x: (x[0], x[1]))
    payload = b''.join(x[2] for x in items)
    def build(mdat_start):
        voff, vsz, aoff = [], [], []
        o = mdat_start + 8
        for ms, kind, data in items:
            if kind == 0:
                voff.append(o); vsz.append(len(data))
            else:
                aoff.append((o, len(data) // 4))
            o += len(data)
        return box('moov', mvhd(SECONDS * 1000) + mp4_trak_video(len(frames), voff, vsz) +
                   mp4_trak_audio(len(pcm) // 4, aoff))
    if moov_first:
        moov = build(0)
        moov = build(len(ftyp) + len(moov))
        data = ftyp + moov + box('mdat', payload)
    else:
        moov = build(len(ftyp))
        data = ftyp + box('mdat', payload) + moov
    with open(path, 'wb') as f:
        f.write(data)

def write_frag_mp4(path, frames, pcm):
    ftyp = box('ftyp', b'iso6' + struct.pack('>I', 0) + b'iso6mp41')
    mvex = box('mvex', fbox('trex', 0, 0, struct.pack('>IIIII', 1, 1, 0, 0, 0)) +
               fbox('trex', 0, 0, struct.pack('>IIIII', 2, 1, 0, 0, 0)))
    moov = box('moov', mvhd(0) + mp4_trak_video(0, frag=True) + mp4_trak_audio(0, frag=True) + mvex)
    init = ftyp + moov
    data = init
    ranges = [[0, len(init), -1]]
    achunk = AR * 20 // 1000 * ACH * 2
    seq = 1
    for c in range(int(SECONDS / 0.5)):
        t0 = c * 0.5
        vf = [(i, f) for i, f in enumerate(frames) if t0 <= i / FPS < t0 + 0.5]
        a0 = int(t0 * 1000); a1 = int((t0 + 0.5) * 1000)
        ach = [pcm[ms * achunk // 20:(ms + 20) * achunk // 20] for ms in range(a0, a1, 20)]
        vdata = b''.join(f for _, f in vf)
        adata = b''.join(ach)
        def moof_bytes(voff, aoff):
            # traf video: default-base-is-moof; trun with sizes, durations; first sample sync
            vtrun = fbox('trun', 0, 0x000001 | 0x000004 | 0x000100 | 0x000200,
                         struct.pack('>IiI', len(vf), voff, 0x02000000) +
                         b''.join(struct.pack('>II', VTS // FPS, len(f)) for _, f in vf))
            vtraf = box('traf', fbox('tfhd', 0, 0x020000 | 0x000020, struct.pack('>II', 1, 0x01010000)) +
                        fbox('tfdt', 1, 0, struct.pack('>Q', vf[0][0] * VTS // FPS)) + vtrun)
            atrun = fbox('trun', 0, 0x000001 | 0x000100 | 0x000200,
                         struct.pack('>Ii', len(ach), aoff) +
                         b''.join(struct.pack('>II', AR * 20 // 1000, len(x)) for x in ach))
            atraf = box('traf', fbox('tfhd', 0, 0x020000 | 0x000020, struct.pack('>II', 2, 0x02000000)) +
                        fbox('tfdt', 1, 0, struct.pack('>Q', a0 * AR // 1000)) + atrun)
            return box('moof', fbox('mfhd', 0, 0, struct.pack('>I', seq)) + vtraf + atraf)
        m = moof_bytes(0, 0)
        m = moof_bytes(len(m) + 8, len(m) + 8 + len(vdata))
        seg = m + box('mdat', vdata + adata)
        ranges.append([len(data), len(data) + len(seg), t0])
        data += seg
        seq += 1
    with open(path, 'wb') as f:
        f.write(data)
    with open(path[:-4] + '.json', 'w') as f:
        json.dump(ranges, f)

# ---------------------------------------------------------------- WAV
def write_wav(path):
    rate = 22050
    pcm = audio_pcm(1.0, rate, 1)
    hdr = b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVE' + b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16)
    with open(path, 'wb') as f:
        f.write(hdr + b'data' + struct.pack('<I', len(pcm)) + pcm)

# ---------------------------------------------------------------- FLAC (an encoder for the tests)
class BitW:
    def __init__(self): self.b = bytearray(); self.acc = 0; self.n = 0
    def put(self, v, k):
        if k == 0: return
        v &= (1 << k) - 1
        self.acc = (self.acc << k) | v; self.n += k
        while self.n >= 8:
            self.n -= 8; self.b.append((self.acc >> self.n) & 255)
        self.acc &= (1 << self.n) - 1
    def sput(self, v, k): self.put(v & ((1 << k) - 1), k)
    def unary(self, q):
        for _ in range(q): self.put(0, 1)
        self.put(1, 1)
    def align(self):
        if self.n: self.put(0, 8 - self.n)
    def bytes(self): return bytes(self.b)

def crc8(d):
    c = 0
    for x in d:
        c ^= x
        for _ in range(8): c = ((c << 1) ^ 0x07) & 255 if c & 0x80 else (c << 1) & 255
    return c
def crc16(d):
    c = 0
    for x in d:
        c ^= x << 8
        for _ in range(8): c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c

def write_residual(w, res, bs, order, porder, escape_part=-1):
    w.put(0, 2); w.put(porder, 4)
    parts = 1 << porder
    idx = 0
    for p in range(parts):
        n = (bs >> porder) - (order if p == 0 else 0)
        seg = res[idx:idx + n]; idx += n
        if p == escape_part:
            nb = max(1, max((abs(x).bit_length() + 1) for x in seg) if seg else 1)
            w.put(15, 4); w.put(nb, 5)
            for x in seg: w.sput(x, nb)
            continue
        # the best Rice parameter
        best, bk = None, 0
        for k in range(15):
            cost = sum((((x << 1) if x >= 0 else ((-x) << 1) - 1) >> k) + 1 + k for x in seg)
            if best is None or cost < best: best, bk = cost, k
        w.put(bk, 4)
        for x in seg:
            u = (x << 1) if x >= 0 else ((-x) << 1) - 1
            w.unary(u >> bk); w.put(u & ((1 << bk) - 1), bk)

def fixed_res(s, order):
    r = []
    for i in range(order, len(s)):
        if order == 0: p = 0
        elif order == 1: p = s[i-1]
        elif order == 2: p = 2*s[i-1] - s[i-2]
        elif order == 3: p = 3*s[i-1] - 3*s[i-2] + s[i-3]
        else: p = 4*s[i-1] - 6*s[i-2] + 4*s[i-3] - s[i-4]
        r.append(s[i] - p)
    return r

def lpc_coefs(s, order):
    # autocorrelation + Levinson-Durbin
    n = len(s)
    R = [sum(s[i] * s[i - l] for i in range(l, n)) for l in range(order + 1)]
    if R[0] == 0: return [0] * order
    a = [0.0] * (order + 1); a[0] = 1.0; e = float(R[0])
    for i in range(1, order + 1):
        acc = R[i] + sum(a[j] * R[i - j] for j in range(1, i))
        k = -acc / e if e else 0
        na = a[:]
        for j in range(1, i): na[j] = a[j] + k * a[i - j]
        na[i] = k; a = na; e *= (1 - k * k)
    return [-x for x in a[1:]]

def subframe(w, s, bps, kind, order=0, prec=12, wasted=0, escape_part=-1, porder=0):
    w.put(0, 1)
    if wasted:
        s = [x >> wasted for x in s]; bps -= wasted
    if kind == 'const':
        w.put(0, 6); w.put(1 if wasted else 0, 1)
        if wasted: w.unary(wasted - 1)
        w.sput(s[0], bps); return
    if kind == 'verbatim':
        w.put(1, 6); w.put(1 if wasted else 0, 1)
        if wasted: w.unary(wasted - 1)
        for x in s: w.sput(x, bps)
        return
    if kind == 'fixed':
        w.put(8 + order, 6); w.put(1 if wasted else 0, 1)
        if wasted: w.unary(wasted - 1)
        for x in s[:order]: w.sput(x, bps)
        write_residual(w, fixed_res(s, order), len(s), order, porder, escape_part)
        return
    # LPC
    c = lpc_coefs(s, order)
    prec = 10 if order <= 8 else 12      # (both of the decoder's paths: 32 and 64-bit sums)
    shift = 9
    q = [int(round(x * (1 << shift))) for x in c]
    lim = (1 << (prec - 1)) - 1
    q = [max(-lim - 1, min(lim, x)) for x in q]
    res = []
    for i in range(order, len(s)):
        pr = sum(q[j] * s[i - 1 - j] for j in range(order)) >> shift
        res.append(s[i] - pr)
    w.put(32 + order - 1, 6); w.put(1 if wasted else 0, 1)
    if wasted: w.unary(wasted - 1)
    for x in s[:order]: w.sput(x, bps)
    w.put(prec - 1, 4); w.sput(shift, 5)
    for x in q: w.sput(x, prec)
    write_residual(w, res, len(s), order, porder, escape_part)

def utf8num(n):
    if n < 0x80: return bytes([n])
    out = []
    for nb, first in ((2, 0xC0), (3, 0xE0), (4, 0xF0), (5, 0xF8), (6, 0xFC)):
        if n < (1 << (5 * nb + 1)) or nb == 6:
            b = []
            for _ in range(nb - 1): b.insert(0, 0x80 | (n & 0x3F)); n >>= 6
            return bytes([first | n] + b)

def write_flac(path, refpath):
    rate, bs, secs = 44100, 4096, 1.5
    n = int(rate * secs)
    L, R = [], []
    for k in range(n):
        t = k / rate
        l = int(12000 * math.sin(2 * math.pi * 440 * t) + 3000 * math.sin(2 * math.pi * 3300 * t))
        r = int(9000 * math.sin(2 * math.pi * 660 * t + 0.3) + (k * 7919 % 513) - 256)
        L.append(l); R.append(r)
    frames = b''
    # per frame: a different coding (exercises the decoder's paths)
    modes = [('fixed', 2, 'indep'), ('lpc', 8, 'ls'), ('lpc', 12, 'rs'), ('fixed', 4, 'ms'),
             ('verbatim', 0, 'indep'), ('lpc', 32, 'ms'), ('fixed', 0, 'indep'), ('lpc', 4, 'indep'),
             ('const', 0, 'indep'), ('fixed', 1, 'ls'), ('wasted', 2, 'indep'), ('escape', 3, 'indep'),
             ('lpc', 16, 'ms'), ('fixed', 3, 'rs'), ('lpc', 6, 'indep'), ('fixed', 2, 'ms'), ('lpc', 10, 'ls')]
    fi = 0
    for start in range(0, n, bs):
        l, r = L[start:start + bs], R[start:start + bs]
        cnt = len(l)
        kind, order, stereo = modes[fi % len(modes)]
        if kind == 'const':
            l = [1234] * cnt; r = [-77] * cnt
            for i in range(cnt): L[start + i] = 1234; R[start + i] = -77
        if kind == 'wasted':
            l = [x & ~3 for x in l]; r = [x & ~3 for x in r]
            for i in range(cnt): L[start + i] = l[i]; R[start + i] = r[i]
        hdr = bytearray([0xFF, 0xF8])
        bscode = 12 if cnt == 4096 else 7
        chan = {'indep': 1, 'ls': 8, 'rs': 9, 'ms': 10}[stereo]
        hdr.append((bscode << 4) | 9)          # 44.1 kHz
        hdr.append((chan << 4) | (4 << 1))      # 16 bits
        hdr += utf8num(fi)
        if bscode == 7: hdr += struct.pack('>H', cnt - 1)
        hdr.append(crc8(hdr))
        w = BitW()
        porder = 2 if cnt % 4 == 0 else 0
        if stereo == 'indep': chs = [(l, 16), (r, 16)]
        elif stereo == 'ls': chs = [(l, 16), ([a - b for a, b in zip(l, r)], 17)]
        elif stereo == 'rs': chs = [([a - b for a, b in zip(l, r)], 17), (r, 16)]
        else: chs = [([(a + b) >> 1 for a, b in zip(l, r)], 16), ([a - b for a, b in zip(l, r)], 17)]
        for s, bps in chs:
            if kind == 'verbatim': subframe(w, s, bps, 'verbatim')
            elif kind == 'const': subframe(w, s, bps, 'const' if len(set(s)) == 1 else 'verbatim')
            elif kind == 'wasted': subframe(w, s, bps, 'fixed', order=2, wasted=order, porder=porder)
            elif kind == 'escape': subframe(w, s, bps, 'fixed', order=order, escape_part=1, porder=porder)
            elif kind == 'fixed': subframe(w, s, bps, 'fixed', order=order, porder=porder)
            else: subframe(w, s, bps, 'lpc', order=order, porder=porder)
        w.align()
        body = bytes(hdr) + w.bytes()
        frames += body + struct.pack('>H', crc16(body))
        fi += 1
    # STREAMINFO: block sizes, frame sizes (0: unknown), rate / channels - 1 / bits - 1 / samples, MD5 (0)
    si = struct.pack('>HH', bs, bs) + (0).to_bytes(3, 'big') + (0).to_bytes(3, 'big')
    si += (rate << 44 | 1 << 41 | 15 << 36 | n).to_bytes(8, 'big')
    si += b'\0' * 16
    meta = bytes([0x80]) + len(si).to_bytes(3, 'big') + si
    with open(path, 'wb') as f:
        f.write(b'fLaC' + meta + frames)
    with open(refpath, 'wb') as f:
        f.write(b''.join(struct.pack('<hh', a, b) for a, b in zip(L, R)))

def main():
    global SECONDS
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    nfr = SECONDS * FPS
    frames = [frame_yuv(i) for i in range(nfr)]
    pcm = audio_pcm(SECONDS, AR, ACH)
    with open(os.path.join(out, 'frames.txt'), 'w') as f:
        for i, fr in enumerate(frames):
            f.write('%d %d %d %d\n' % (i, i * 1000000 // FPS, sum(fr), rgb_sum(fr)))
    write_webm(os.path.join(out, 'clip.webm'), frames, pcm)
    write_mse_webm(os.path.join(out, 'mse.webm'), frames, pcm)
    write_mp4(os.path.join(out, 'clip.mp4'), frames, pcm, True)
    write_mp4(os.path.join(out, 'clip-moovend.mp4'), frames, pcm, False)
    write_frag_mp4(os.path.join(out, 'frag.mp4'), frames, pcm)
    write_wav(os.path.join(out, 'tone.wav'))
    write_flac(os.path.join(out, 'tone.flac'), os.path.join(out, 'tone-ref.raw'))
    # long.webm / long.mp4: 10 s of the same (4 MB: loaded by ranges, seeking where nothing came yet)

    SECONDS = 10
    frames = [frames[i % len(frames)] for i in range(SECONDS * FPS)]
    pcm = audio_pcm(SECONDS, AR, ACH)
    write_webm(os.path.join(out, 'long.webm'), frames, pcm)
    write_mp4(os.path.join(out, 'long.mp4'), frames, pcm, True)
    SECONDS = 2

if __name__ == '__main__':
    main()
