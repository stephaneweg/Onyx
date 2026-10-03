#!/usr/bin/env python3
"""
tools/webkit/tests/mkvideotests.py -- the media engine's test pages (WebKit's MediaPlayerPrivateOnyx on
user/av), made from the tiny clips of tools/tests/av/clips (96 x 64, 2 s, 25 pictures a second):

  video-file.html   <video src=...>: the files next to the page (vp9.webm, av1.mp4), fetched by the
                    engine's own loader; each plays, loops once by a seek to 0, and is paused
  video-mse.html    Media Source Extensions: the clip is in the page (base64: no fetch, the page works
                    from file://), appended segment by segment as a player would; then a seek

Each page writes what happens (the events, the times, the sizes) on itself and in the console, and ends
with a line "video test: PASS" or "video test: FAIL ..." (tools/webkit/test-webkit.sh reads kmsg).

  python3 tools/webkit/tests/mkvideotests.py        (writes the pages and copies the clips beside them)

Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence.
"""
import base64, json, os, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
CLIPS = os.path.join(HERE, '..', '..', 'tests', 'av', 'clips')

COMMON = r"""
var out = document.getElementById('out'), failed = [];
function say(s) { console.log('video test: ' + s); out.textContent += s + '\n'; }
function check(ok, what) { if (!ok) failed.push(what); say((ok ? 'ok   ' : 'FAIL ') + what); }
function finish() { say(failed.length ? 'FAIL ' + failed.join('; ') : 'PASS'); }
window.onerror = function (m, f, l) { check(false, 'script error ' + m + ' @' + l); finish(); };
function watch(v, name) {
	['loadstart', 'loadedmetadata', 'loadeddata', 'canplay', 'canplaythrough', 'playing', 'waiting', 'seeking', 'seeked', 'ended', 'pause', 'stalled', 'durationchange', 'resize'].forEach(function (e) {
		v.addEventListener(e, function () { say(name + ': ' + e + ' (t ' + v.currentTime.toFixed(2) + ', ready ' + v.readyState + ', ' + v.videoWidth + 'x' + v.videoHeight + ', duration ' + v.duration + ')'); });
	});
	v.addEventListener('error', function () { check(false, name + ': error ' + (v.error ? v.error.code + ' ' + v.error.message : '?')); });
}
// The picture's middle pixel, through a canvas: something was drawn
function pixel(v) {
	var c = document.createElement('canvas'); c.width = 96; c.height = 64;
	var g = c.getContext('2d'); g.drawImage(v, 0, 0, 96, 64);
	try { var d = g.getImageData(48, 32, 1, 1).data; return d[0] + ',' + d[1] + ',' + d[2] + ',' + d[3]; } catch (e) { return 'tainted'; }
}
function once(v, e) { return new Promise(function (ok) { v.addEventListener(e, function f() { v.removeEventListener(e, f); ok(); }); }); }
function sleep(ms) { return new Promise(function (ok) { setTimeout(ok, ms); }); }
function within(p, ms, what) { return Promise.race([p, sleep(ms).then(function () { throw new Error('timeout: ' + what); })]); }
"""

HEAD = """<!DOCTYPE html>
<!-- %s (made by mkvideotests.py: edit that, not this) -->
<html><head><meta charset="utf-8"><title>%s</title>
<style>body{font:13px sans-serif;margin:10px} video{background:#222;margin:4px;width:288px;height:192px} pre{font:12px monospace}</style>
</head><body>
"""

FILE_BODY = """<video id="a" src="vp9.webm" muted playsinline></video>
<video id="b" src="av1.mp4" muted playsinline></video>
<video id="c" src="vp9.webm" controls muted></video>
<pre id="out"></pre>
<script>
%s
say('canPlayType: webm vp9 "' + document.createElement('video').canPlayType('video/webm; codecs="vp9"') + '", mp4 av01 "'
	+ document.createElement('video').canPlayType('video/mp4; codecs="av01.0.00M.08"') + '", mp4 avc1 "'
	+ document.createElement('video').canPlayType('video/mp4; codecs="avc1.42E01E"') + '"');
async function one(id) {
	var v = document.getElementById(id);
	watch(v, id);
	if (v.readyState < 1) await within(once(v, 'loadedmetadata'), 15000, id + ' loadedmetadata');
	check(v.videoWidth == 96 && v.videoHeight == 64, id + ': the size ' + v.videoWidth + 'x' + v.videoHeight);
	check(v.duration > 1.9 && v.duration < 2.2, id + ': the duration ' + v.duration);
	var t0 = performance.now();
	await v.play();
	await within(once(v, 'ended'), 15000, id + ' ended');
	var took = (performance.now() - t0) / 1000;
	check(took > 1.7 && took < 3.5, id + ': played to the end in ' + took.toFixed(2) + ' s');
	check(v.buffered.length == 1 && v.buffered.end(0) > 1.9, id + ': buffered ' + (v.buffered.length ? v.buffered.start(0).toFixed(2) + '-' + v.buffered.end(0).toFixed(2) : 'nothing'));
	var q = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality() : null;
	say(id + ': pictures ' + (q ? q.totalVideoFrames + ' decoded, ' + q.droppedVideoFrames + ' dropped' : '?') + ', the middle pixel ' + pixel(v));
	v.currentTime = 1;
	await within(once(v, 'seeked'), 15000, id + ' seeked');
	check(Math.abs(v.currentTime - 1) < 0.1, id + ': after the seek, at ' + v.currentTime.toFixed(2));
	await v.play();
	await sleep(400);
	v.pause();
	check(v.currentTime > 1.05 && v.currentTime < 1.8 && v.paused, id + ': played from there, paused at ' + v.currentTime.toFixed(2));
}
(async function () {
	try { await one('a'); await one('b'); } catch (e) { check(false, '' + e); }
	finish();
})();
</script>
</body></html>
"""

MSE_BODY = """<video id="v" muted playsinline></video>
<pre id="out"></pre>
<script>
%s
var CLIP = '%s', SEGMENTS = %s, MIME = 'video/webm; codecs="vp9"';
function bytes(from, to) {
	var s = atob(CLIP), a = new Uint8Array(to - from);
	for (var i = from; i < to; i++) a[i - from] = s.charCodeAt(i);
	return a;
}
function ranges(r) { var s = []; for (var i = 0; i < r.length; i++) s.push(r.start(i).toFixed(2) + '-' + r.end(i).toFixed(2)); return s.join(' ') || 'nothing'; }
(async function () {
	try {
		var v = document.getElementById('v');
		watch(v, 'mse');
		check(!!window.MediaSource, 'MediaSource is there');
		check(MediaSource.isTypeSupported(MIME), 'isTypeSupported ' + MIME);
		check(!MediaSource.isTypeSupported('video/mp4; codecs="avc1.42E01E"'), 'H.264 is said not to be supported');
		var ms = new MediaSource();
		// (a blob: address is refused to a page from file://, whose origin is null: the object itself there)
		if (location.protocol == 'file:') v.srcObject = ms; else v.src = URL.createObjectURL(ms);
		await within(once(ms, 'sourceopen'), 10000, 'sourceopen');
		var sb = ms.addSourceBuffer(MIME);
		async function append(i) {
			sb.appendBuffer(bytes(SEGMENTS[i][0], SEGMENTS[i][1]));
			await within(once(sb, 'updateend'), 10000, 'updateend ' + i);
			say('appended segment ' + i + ': buffered ' + ranges(sb.buffered));
		}
		await append(0);					// the initialization segment
		await append(1);
		if (v.readyState < 1) await within(once(v, 'loadedmetadata'), 10000, 'loadedmetadata');
		check(v.videoWidth == 96 && v.videoHeight == 64, 'the size ' + v.videoWidth + 'x' + v.videoHeight);
		check(sb.buffered.length == 1 && sb.buffered.end(0) > 0.4, 'the first media segment is buffered: ' + ranges(sb.buffered));
		var t0 = performance.now();
		await v.play();
		await sleep(250);
		await append(2);
		await append(3);
		await append(4);
		ms.endOfStream();
		check(ms.duration > 1.9 && ms.duration < 2.2, 'the duration after endOfStream ' + ms.duration);
		await within(once(v, 'ended'), 15000, 'ended');
		var took = (performance.now() - t0) / 1000;
		check(took > 1.7 && took < 3.5, 'played to the end in ' + took.toFixed(2) + ' s');
		var q = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality() : null;
		say('pictures ' + (q ? q.totalVideoFrames + ' decoded, ' + q.droppedVideoFrames + ' dropped' : '?') + ', the middle pixel ' + pixel(v));
		v.currentTime = 0.7;
		await within(once(v, 'seeked'), 15000, 'seeked');
		check(Math.abs(v.currentTime - 0.7) < 0.1, 'after the seek, at ' + v.currentTime.toFixed(2));
		await v.play();
		await sleep(500);
		v.pause();
		check(v.currentTime > 0.75 && v.currentTime < 1.6, 'played from there, paused at ' + v.currentTime.toFixed(2));
		// remove(): the first second goes, the rest stays
		sb.remove(0, 1);
		await within(once(sb, 'updateend'), 10000, 'updateend of remove');
		check(sb.buffered.length == 1 && sb.buffered.start(0) > 0.9, 'after remove(0, 1): buffered ' + ranges(sb.buffered));
	} catch (e) { check(false, '' + e); }
	finish();
})();
</script>
</body></html>
"""

def main():
	for clip in ('vp9.webm', 'av1.mp4'):
		shutil.copyfile(os.path.join(CLIPS, clip), os.path.join(HERE, clip))
	with open(os.path.join(HERE, 'video-file.html'), 'w', newline='\n') as f:
		f.write(HEAD % ('video-file.html: <video src>, the engine\'s file mode', 'video file') + FILE_BODY % COMMON)
	with open(os.path.join(CLIPS, 'vp9-mse.webm'), 'rb') as f:
		clip = base64.b64encode(f.read()).decode()
	with open(os.path.join(CLIPS, 'vp9-mse.json')) as f:
		segments = [[s[0], s[1]] for s in json.load(f)]
	with open(os.path.join(HERE, 'video-mse.html'), 'w', newline='\n') as f:
		f.write(HEAD % ('video-mse.html: Media Source Extensions, the engine\'s queue mode', 'video MSE') + MSE_BODY % (COMMON, clip, json.dumps(segments)))
	print('video-file.html, video-mse.html, vp9.webm, av1.mp4 written in', HERE)

if __name__ == '__main__':
	main()
