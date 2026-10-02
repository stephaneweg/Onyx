# Test videos for the Media Player (and Jet Browser)

Copy them by hand into `SD:/Videos/` on the card (e.g. `SD:/Videos/Films/`). They are NOT part of `sdcard/`
(no package).

The **Sintel trailer** (52 s, sound) — © copyright Blender Foundation | durian.blender.org, licensed under
**Creative Commons Attribution 3.0** (https://creativecommons.org/licenses/by/3.0/). Source:
https://media.w3.org/2010/05/sintel/trailer.mp4, converted with ffmpeg.

| File | Container | Picture / sound | Decoded by |
|---|---|---|---|
| `Sintel - VP9 Opus 480p.webm` | WebM | VP9 / Opus | libvpx / libopus |
| `Sintel - VP8 Opus 360p.webm` | WebM | VP8 / Opus | libvpx / libopus |
| `Sintel - VP9 Opus 480p.mp4` | MP4 | VP9 / Opus | libvpx / libopus |
| `Sintel - AV1 Opus 360p.mp4` | MP4 | AV1 / Opus | dav1d / libopus |
| `Sintel - VP9 FLAC 480p.mkv` | Matroska | VP9 / FLAC | libvpx / built in |
| `Sintel - VP9 MP3 480p.mkv` | Matroska | VP9 / MP3 | libvpx / minimp3 |
| `Sintel - VP9 PCM 480p.mkv` | Matroska | VP9 / PCM 16-bit | libvpx / built in |
| `Sintel - H264 AAC 480p.mp4` | MP4 | H.264 / AAC (the original) | FFmpeg |
| `Sintel - H264 AAC 480p.ts` | MPEG-TS | H.264 / AAC | FFmpeg (its demuxer too) |
| `Sintel - H264 AC3 480p.mkv` | Matroska | H.264 / AC-3 | FFmpeg |
| `Sintel - H265 AAC 480p.mp4` | MP4 | H.265 (HEVC) / AAC | FFmpeg |
| `Sintel - Xvid MP3 360p.avi` | AVI | MPEG-4 Part 2 (Xvid) / MP3 | FFmpeg (its demuxer too) |
| `Sintel - WMV WMA 360p.wmv` | ASF | WMV 8 / WMA 2 | FFmpeg (its demuxer too) |
| `Sintel - Sorenson MP3 360p.flv` | FLV | Sorenson H.263 / MP3 | FFmpeg (its demuxer too) |
| `Sintel - MPEG-2 MP2 360p.mpg` | MPEG-PS | MPEG-2 / MP2 | FFmpeg (its demuxer too) |
| `Sintel - Theora Vorbis 360p.ogv` | Ogg | Theora / Vorbis | FFmpeg (its demuxer too) |

Each was checked with `user/av` on the PC (tools/tests/av/fftest.c): every frame and the 52 s of sound decoded,
played in the file mode, a seek half way.
