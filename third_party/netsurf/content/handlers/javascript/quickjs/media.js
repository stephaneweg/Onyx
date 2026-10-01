/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: <video> and <audio> (HTMLMediaElement), Media Source Extensions (MediaSource,
 * SourceBuffer, SourceBufferList), TimeRanges, MediaError, VideoPlaybackQuality,
 * requestVideoFrameCallback, navigator.mediaCapabilities, the Fullscreen API, and the clicks of
 * the native controls -- on the natives of qjs_media.c (N.md*: a player each, user/av).
 *
 * The element's state is here (networkState, readyState, paused, seeking, the pending play()
 * promises); the player's turns (qjs_media.c's poll) call back with its state, and the events
 * are worked out from the changes (loadedmetadata, loadeddata, canplay, canplaythrough, play,
 * playing, waiting, timeupdate, seeking, seeked, pause, ended, durationchange, resize,
 * volumechange, ratechange, progress, error). A src that is not a MediaSource is fetched by
 * ranges (1 MB, ~30 s ahead of the playback) and fed to the player's demuxer where it asks.
 * qjs.c runs this function with the natives after dom.js / html5.js / canvas.js / net.js.
 */
(function (N) {
'use strict';

const G = globalThis;
const ST = Symbol('media');
const CHUNK = 1 << 20;
const AV_EUNSUP = -3, AV_EFULL = -4;

function def(obj, props) {
	for (const k of Object.keys(props)) {
		const d = Object.getOwnPropertyDescriptor(props, k);
		d.enumerable = false;
		d.configurable = true;
		Object.defineProperty(obj, k, d);
	}
}
function domError(msg, name) { return new G.DOMException(msg, name); }
function task(fn) { G.setTimeout(fn, 0); }
function fire(target, type, init) {
	const e = new G.Event(type, init || {});
	target.dispatchEvent(e);
	return e;
}
function debug() { return G.__onyxMediaDebug === true; }

/* an on<type> property as an event listener of its own */
function handlerProperty(proto, type) {
	const key = Symbol('on' + type);
	Object.defineProperty(proto, 'on' + type, { configurable: true, enumerable: true,
		get() { return this[key] ? this[key].fn : null; },
		set(fn) {
			if (this[key]) this.removeEventListener(type, this[key].l);
			this[key] = null;
			if (typeof fn === 'function') {
				const l = ev => fn.call(this, ev);
				this[key] = { fn, l };
				this.addEventListener(type, l);
			}
		} });
}

/* ---- TimeRanges, MediaError, VideoPlaybackQuality ---------------------------------------------- */

class TimeRanges {
	constructor(flat) { this._r = flat || []; }
	get length() { return this._r.length >> 1; }
	start(i) {
		i = i >>> 0;
		if (i >= this.length) throw domError('index ' + i + ' out of range', 'IndexSizeError');
		return this._r[2 * i];
	}
	end(i) {
		i = i >>> 0;
		if (i >= this.length) throw domError('index ' + i + ' out of range', 'IndexSizeError');
		return this._r[2 * i + 1];
	}
	get [Symbol.toStringTag]() { return 'TimeRanges'; }
}
G.TimeRanges = TimeRanges;

class MediaError {
	constructor(code, message) { this._c = code; this._m = message || ''; }
	get code() { return this._c; }
	get message() { return this._m; }
	get [Symbol.toStringTag]() { return 'MediaError'; }
}
def(MediaError, { MEDIA_ERR_ABORTED: 1, MEDIA_ERR_NETWORK: 2, MEDIA_ERR_DECODE: 3, MEDIA_ERR_SRC_NOT_SUPPORTED: 4 });
def(MediaError.prototype, { MEDIA_ERR_ABORTED: 1, MEDIA_ERR_NETWORK: 2, MEDIA_ERR_DECODE: 3, MEDIA_ERR_SRC_NOT_SUPPORTED: 4 });
G.MediaError = MediaError;

class VideoPlaybackQuality {
	constructor(total, dropped) {
		this.creationTime = G.performance ? G.performance.now() : Date.now();
		this.totalVideoFrames = total;
		this.droppedVideoFrames = dropped;
		this.corruptedVideoFrames = 0;
		this.totalFrameDelay = 0;
	}
}
G.VideoPlaybackQuality = VideoPlaybackQuality;

/* an empty track list (audioTracks, videoTracks, textTracks) */
class TrackList extends G.EventTarget {
	constructor() { super(); this._l = []; }
	get length() { return this._l.length; }
	getTrackById(id) { return this._l.find(t => t.id === id) || null; }
	[Symbol.iterator]() { return this._l[Symbol.iterator](); }
}
for (const t of ['change', 'addtrack', 'removetrack'])
	handlerProperty(TrackList.prototype, t);
class TextTrack extends G.EventTarget {
	constructor(kind, label, language) {
		super();
		this.kind = kind || 'subtitles'; this.label = label || ''; this.language = language || '';
		this.id = ''; this.mode = 'disabled'; this.cues = { length: 0 }; this.activeCues = { length: 0 };
		this._cues = [];
	}
	addCue(c) { this._cues.push(c); this.cues = Object.assign([], this._cues); }
	removeCue(c) { this._cues = this._cues.filter(x => x !== c); this.cues = Object.assign([], this._cues); }
}
handlerProperty(TextTrack.prototype, 'cuechange');
for (const n of ['AudioTrackList', 'VideoTrackList', 'TextTrackList'])
	G[n] = class extends TrackList {};
G.TextTrack = TextTrack;
if (!G.VTTCue)
	G.VTTCue = class VTTCue extends G.EventTarget {
		constructor(start, end, text) { super(); this.startTime = +start; this.endTime = +end; this.text = String(text); this.id = ''; }
	};

/* ---- the media element's state ------------------------------------------------------------- */

const EMPTY = 0, IDLE = 1, LOADING = 2, NO_SOURCE = 3;

function st(el) {
	let S = el[ST];
	if (!S) {
		S = {
			h: null, open: false, networkState: EMPTY, readyState: 0, paused: true, ended: false,
			seeking: false, error: null, currentSrc: '', loadId: 0, mse: null, srcObject: null,
			time: 0, duration: NaN, lastTU: 0, lastProgress: 0, defaultRate: 1, rate: 1,
			volume: 1, muted: el.hasAttribute('muted'), pending: [], autoplaying: true,
			vw: 0, vh: 0, loader: null, loadeddata: false, boxSeen: false, presented: 0,
			decoded: 0, dropped: 0, rvfc: new Map(), rvfcId: 0, played: 0, startAt: 0,
			controlsTimer: 0, textTracks: new G.TextTrackList(), audioTracks: new G.AudioTrackList(),
			videoTracks: new G.VideoTrackList(), wantPlay: false,
		};
		Object.defineProperty(el, ST, { value: S, configurable: true });
		listenControls(el);
	}
	return S;
}

function handle(el) {
	const S = st(el);
	if (!S.h) S.h = N.mdNew(el);
	return S.h;
}

/* the player's state came (qjs_media.c's turn) */
function onTick(el, a) {
	const S = st(el);
	if (!S.open) return;
	const [time, dur, ready, paused, ended, seeking, waiting, w, h, decoded, dropped, err, want, sound,
		hasA, hasV, decUs, presented, syncUs] = a;
	S.decodeUs = decUs;
	S.syncUs = syncUs;
	S.decoded = decoded;
	S.dropped = dropped;
	S.sound = sound;
	if (err && !S.error) {
		mediaError(el, err === AV_EUNSUP ? 4 : 3, err === AV_EUNSUP ? 'the media\'s codec is not supported' : 'the media could not be decoded');
		return;
	}
	/* the duration: the MediaSource's, else the container's */
	if (S.mse) {
		if (isNaN(S.mse._dur) && dur > 0 && S.mse._rs === 'open')
			S.mse._durationChange(dur);
	} else if (dur > 0 && dur !== S.duration) {
		S.duration = dur;
		if (S.readyState >= 1) fire(el, 'durationchange');
	}
	if ((w !== S.vw || h !== S.vh) && w > 0) {
		S.vw = w;
		S.vh = h;
		if (S.readyState >= 1) fire(el, 'resize');
	}
	setReady(el, ready);
	if (!S.open) return;
	if (S.seeking && !seeking && S.seekingFired) {
		S.seeking = false;
		S.time = time;
		fire(el, 'timeupdate');
		fire(el, 'seeked');
	} else if (!S.seeking) {
		S.time = time;
	}
	if (ended && !S.ended && !S.paused) {
		if (el.loop) {
			el.currentTime = 0;
			N.mdPlay(S.h);
		} else {
			S.ended = true;
			fire(el, 'timeupdate');
			S.paused = true;
			fire(el, 'pause');
			fire(el, 'ended');
			if (debug()) console.log('media: ended at ' + time.toFixed(3));
		}
	}
	const now = Date.now();
	if (!S.paused && !S.seeking && now - S.lastTU >= 250) {
		S.lastTU = now;
		fire(el, 'timeupdate');
	}
	if (S.loader && !S.loader.done && now - S.lastProgress >= 350) {
		S.lastProgress = now;
		fire(el, 'progress');
	}
	if (presented !== S.presented) {
		S.presented = presented;
		frameCallbacks(el, time, w, h, presented);
	}
	if (S.loader) pumpLoader(el);
}

function setReady(el, r) {
	const S = st(el), old = S.readyState;
	if (r === old) return;
	if (old === 0 && r >= 1) {
		S.readyState = 1;
		if (!S.mse && S.duration > 0) fire(el, 'durationchange');
		if (S.vw) fire(el, 'resize');
		fire(el, 'loadedmetadata');
		if (S.startAt > 0) {
			const t = S.startAt;
			S.startAt = 0;
			el.currentTime = t;
		}
	}
	if (r >= 2 && !S.loadeddata) {
		S.loadeddata = true;
		S.readyState = 2;
		fire(el, 'loadeddata');
	}
	S.readyState = r;
	if (old < 3 && r >= 3) {
		fire(el, 'canplay');
		if (!S.paused && S.playFired) {
			fire(el, 'playing');
			resolvePending(S);
		}
		/* autoplay */
		if (S.autoplaying && S.paused && el.autoplay) {
			S.autoplaying = false;
			S.paused = false;
			S.playFired = true;
			fire(el, 'play');
			N.mdPlay(S.h);
			fire(el, 'playing');
		}
	}
	if (old < 4 && r === 4) fire(el, 'canplaythrough');
	if (old >= 3 && r < 3 && !S.paused && !S.ended && !S.seeking) {
		fire(el, 'timeupdate');
		fire(el, 'waiting');
	}
}

function resolvePending(S) {
	const l = S.pending;
	S.pending = [];
	for (const p of l) p.res(undefined);
}
function rejectPending(S, name, msg) {
	const l = S.pending;
	S.pending = [];
	for (const p of l) p.rej(domError(msg, name));
}

function mediaError(el, code, msg) {
	const S = st(el);
	S.error = new MediaError(code, msg);
	if (debug()) console.log('media: error ' + code + ' ' + msg);
	if (S.loader) S.loader.done = true;
	S.networkState = code === 4 ? NO_SOURCE : IDLE;
	rejectPending(S, code === 4 ? 'NotSupportedError' : 'AbortError', msg);
	fire(el, 'error');
	if (code !== 4) fire(el, 'suspend');
}

/* ---- the load algorithm --------------------------------------------------------------------- */

function detach(el) {
	const S = st(el);
	if (S.mse) {
		const ms = S.mse;
		S.mse = null;
		ms._detach();
	}
	if (S.loader) S.loader.done = true;
	S.loader = null;
	if (S.h) {
		N.mdClose(S.h);
		N.mdClear(S.h);
	}
	S.open = false;
}

function loadMedia(el) {
	const S = st(el);
	S.loadId++;
	rejectPending(S, 'AbortError', 'The play() request was interrupted by a new load request.');
	if (S.networkState === LOADING || S.networkState === IDLE) fire(el, 'abort');
	if (S.networkState !== EMPTY) {
		fire(el, 'emptied');
		detach(el);
		S.readyState = 0;
		S.loadeddata = false;
		S.paused = true;
		S.seeking = false;
		S.ended = false;
		S.time = 0;
		S.vw = S.vh = 0;
		if (!isNaN(S.duration)) {
			S.duration = NaN;
			fire(el, 'durationchange');
		}
	}
	S.rate = S.defaultRate;
	S.error = null;
	S.autoplaying = true;
	selectResource(el);
}

function sourceChildren(el) {
	return Array.prototype.filter.call(el.children || [], c => c.localName === 'source');
}

function selectResource(el) {
	const S = st(el), id = S.loadId;
	S.networkState = NO_SOURCE;
	task(() => {
		if (id !== S.loadId) return;
		let url = null, type = '';
		if (S.srcObject) {
			url = '';
		} else if (el.hasAttribute('src')) {
			url = el.src;
			if (!el.getAttribute('src')) {
				S.networkState = NO_SOURCE;
				mediaError(el, 4, 'MEDIA_ELEMENT_ERROR: Empty src attribute');
				return;
			}
		} else {
			for (const s of sourceChildren(el)) {
				const t = s.getAttribute('type');
				if (t && el.canPlayType(t) === '') continue;
				if (!s.getAttribute('src')) continue;
				url = s.src;
				type = t || '';
				break;
			}
		}
		if (url === null) {
			S.networkState = EMPTY;
			return;
		}
		S.networkState = LOADING;
		S.currentSrc = url;
		fire(el, 'loadstart');
		const ms = S.srcObject instanceof MediaSource ? S.srcObject : msURLs.get(String(url).replace(/#.*$/, ''));
		if (ms) {
			attachMSE(el, ms);
		} else if (S.srcObject) {
			mediaError(el, 4, 'this srcObject is not supported');
		} else if (el.preload === 'none' && !S.wantPlay && !el.autoplay) {
			S.networkState = IDLE;
			S.deferred = { url, type };
			fire(el, 'suspend');
		} else {
			startFetch(el, url, type);
		}
	});
}

function openPlayer(el) {
	const S = st(el);
	const h = handle(el);
	if (!N.mdOpen(h)) return false;
	S.open = true;
	N.mdVolume(h, S.volume, S.muted ? 1 : 0);
	N.mdRate(h, S.rate);
	N.mdWatch(h, a => onTick(el, a), S.rvfc.size > 0);
	if (!S.paused) N.mdPlay(h);
	return true;
}

function attachMSE(el, ms) {
	const S = st(el);
	if (ms._rs !== 'closed') {
		mediaError(el, 4, 'the MediaSource is already attached');
		return;
	}
	if (!openPlayer(el)) {
		mediaError(el, 3, 'no memory for a player');
		return;
	}
	S.mse = ms;
	ms._attach(el, S.h);
}

/* ---- a src fetched by ranges ------------------------------------------------------------------ */

function startFetch(el, url, type) {
	const S = st(el);
	if (!openPlayer(el)) {
		mediaError(el, 3, 'no memory for a player');
		return;
	}
	const src = N.mdAddSource(S.h, '');
	if (src < 0) {
		mediaError(el, 4, 'the media could not be opened');
		return;
	}
	S.loader = { url, src, total: -1, busy: false, done: false, got: 0, id: S.loadId, type, seq: 0 };
	pumpLoader(el);
}

function pumpLoader(el) {
	const S = st(el), L = S.loader;
	if (!L || L.busy || L.done || L.id !== S.loadId) return;
	const want = N.mdWant(S.h, L.src);
	if (want < 0) {
		L.done = true;
		S.networkState = IDLE;
		fire(el, 'progress');
		fire(el, 'suspend');
		return;
	}
	if (L.total >= 0 && want >= L.total) {
		N.mdFeedEnd(S.h, L.src);
		L.done = true;
		S.networkState = IDLE;
		fire(el, 'progress');
		fire(el, 'suspend');
		return;
	}
	/* far enough ahead: later (a later turn calls again) */
	if (L.got > 0 && N.mdAhead(S.h) > 30 && !S.seeking) return;
	L.busy = true;
	const seq = L.seq;
	const end = want + (G.__onyxMediaChunk > 0 ? G.__onyxMediaChunk : CHUNK) - 1;	/* (the tests: smaller pieces) */
	const headers = /^(blob|data):/i.test(L.url) ? {} : { Range: 'bytes=' + want + '-' + end };
	G.fetch(L.url, { headers }).then(r => {
		if (!r.ok && r.status !== 206) throw new TypeError('HTTP ' + r.status);
		let total = -1;
		const cr = r.headers.get('Content-Range');
		if (r.status === 206 && cr) {
			const m = /\/(\d+)\s*$/.exec(cr);
			if (m) total = +m[1];
		}
		return r.arrayBuffer().then(buf => ({ status: r.status, buf, total }));
	}).then(({ status, buf, total }) => {
		if (L.id !== S.loadId || L.done || L.seq !== seq) return;
		L.busy = false;
		if (status !== 206) {
			/* the whole resource (no ranges: a data: / blob: URL, or the server's choice) */
			N.mdFeed(S.h, L.src, 0, buf);
			L.total = buf.byteLength;
			L.got += buf.byteLength;
			N.mdFeedEnd(S.h, L.src);
			L.done = true;
			S.networkState = IDLE;
			fire(el, 'progress');
			fire(el, 'suspend');
			return;
		}
		L.total = total;
		L.got += buf.byteLength;
		if (N.mdFeed(S.h, L.src, want, buf) < 0 && S.readyState === 0) {
			mediaError(el, 4, 'the media\'s format is not supported');
			return;
		}
		if (buf.byteLength === 0) {
			N.mdFeedEnd(S.h, L.src);
			L.done = true;
			return;
		}
		pumpLoader(el);
	}).catch(e => {
		if (L.id !== S.loadId) return;
		L.busy = false;
		L.done = true;
		if (debug()) console.log('media: fetch failed: ' + e);
		if (S.readyState === 0) mediaError(el, L.got ? 2 : 4, 'the media could not be loaded');
		else N.mdFeedEnd(S.h, L.src);
	});
}

/* ---- requestVideoFrameCallback -------------------------------------------------------------- */

function frameCallbacks(el, time, w, h, presented) {
	const S = st(el);
	if (!S.rvfc.size) return;
	const cbs = Array.from(S.rvfc.values());
	S.rvfc.clear();
	N.mdFrames(S.h, false);
	const now = G.performance ? G.performance.now() : Date.now();
	const meta = { presentationTime: now, expectedDisplayTime: now, width: w, height: h,
		mediaTime: time, presentedFrames: presented, processingDuration: 0 };
	for (const cb of cbs) {
		try { cb(now, meta); } catch (e) { if (debug()) console.log('media: rVFC: ' + e); }
	}
}

/* ---- the native controls ------------------------------------------------------------------------ */

const CTRL_H = 36;
function listenControls(el) {
	const show = () => {
		const S = el[ST];
		if (!S || !el.controls || !S.h) return;
		N.mdControls(S.h, true);
		G.clearTimeout(S.controlsTimer);
		S.controlsTimer = G.setTimeout(() => { if (S.h) N.mdControls(S.h, false); }, 3000);
	};
	el.addEventListener('mousemove', show);
	el.addEventListener('mouseleave', () => { const S = el[ST]; if (S && S.h && el.controls) N.mdControls(S.h, false); });
	el.addEventListener('click', e => {
		if (!el.controls || e.defaultPrevented) return;
		const r = el.getBoundingClientRect(), w = r.width, h = r.height;
		const x = e.clientX - r.left, y = e.clientY - r.top;
		const S = st(el);
		show();
		const isVideo = el.localName === 'video';
		if (isVideo && y < h - CTRL_H) {
			el.paused ? el.play().catch(() => {}) : el.pause();
			return;
		}
		const px0 = w >= 300 ? 150 : 40, px1 = w - 80;
		if (x < 36) el.paused ? el.play().catch(() => {}) : el.pause();
		else if (x >= px0 - 6 && x <= px1 + 6 && px1 > px0 + 10) {
			const d = el.duration;
			if (d > 0 && isFinite(d)) el.currentTime = Math.max(0, Math.min(1, (x - px0) / (px1 - px0))) * d;
		} else if (x >= w - 76 && x < w - 40) el.muted = !el.muted;
		else if (x >= w - 40 && isVideo) {
			if (fsElement === el) exitFullscreen(); else enterFullscreen(el).catch(() => {});
		}
		void S;
	});
	el.addEventListener('dblclick', () => {
		if (!el.controls || el.localName !== 'video') return;
		if (fsElement === el) exitFullscreen(); else enterFullscreen(el).catch(() => {});
	});
}

/* ---- HTMLMediaElement --------------------------------------------------------------------------- */

const ME = G.HTMLMediaElement.prototype;
const elSetAttribute = G.Element.prototype.setAttribute;
const elRemoveAttribute = G.Element.prototype.removeAttribute;
const srcDesc = Object.getOwnPropertyDescriptor(ME, 'src');

def(ME, {
	load() { loadMedia(this); },
	play() {
		const S = st(this);
		if (S.error && S.error.code === 4)
			return Promise.reject(domError('The element has no supported sources.', 'NotSupportedError'));
		const p = new Promise((res, rej) => S.pending.push({ res, rej }));
		S.wantPlay = true;
		if (S.networkState === EMPTY) loadMedia(this);
		else if (S.deferred) {
			const d = S.deferred;
			S.deferred = null;
			startFetch(this, d.url, d.type);
		}
		if (S.ended) {
			S.ended = false;
			if (S.open) this.currentTime = 0;
		}
		if (S.paused) {
			S.paused = false;
			S.autoplaying = false;
			S.playFired = false;
			task(() => {
				S.playFired = true;
				fire(this, 'play');
				if (S.readyState <= 2) fire(this, 'waiting');
				else {
					fire(this, 'playing');
					resolvePending(S);
				}
			});
		} else if (S.readyState >= 3) {
			task(() => resolvePending(S));
		}
		if (S.open) N.mdPlay(S.h);
		return p;
	},
	pause() {
		const S = st(this);
		if (S.networkState === EMPTY) loadMedia(this);
		S.autoplaying = false;
		if (!S.paused) {
			S.paused = true;
			if (S.open) N.mdPause(S.h);
			task(() => {
				fire(this, 'timeupdate');
				fire(this, 'pause');
				rejectPending(S, 'AbortError', 'The play() request was interrupted by a call to pause().');
			});
		}
	},
	canPlayType(t) { return ['', 'maybe', 'probably'][N.mdType(String(t), 0)] || ''; },
	fastSeek(t) { this.currentTime = t; },
	get currentTime() {
		const S = st(this);
		if (S.seeking || !S.open) return S.seeking || S.readyState ? S.time : S.startAt;
		const a = N.mdState(S.h);
		return a ? a[0] : S.time;
	},
	set currentTime(v) {
		const S = st(this);
		v = +v;
		if (!isFinite(v)) throw new TypeError('The provided double value is non-finite.');
		if (S.readyState === 0 || !S.open) {
			S.startAt = v;
			return;
		}
		const d = this.duration;
		if (d >= 0 && v > d) v = d;
		if (v < 0) v = 0;
		S.seeking = true;
		S.ended = false;
		S.time = v;
		N.mdSeek(S.h, v);
		if (S.loader) {
			/* a file: a piece on its way is from before the seek (its demuxer now wants
			 * bytes elsewhere: dropped), and it may want bytes it dropped */
			S.loader.seq++;
			S.loader.busy = false;
			if (S.loader.done && S.loader.total >= 0) S.loader.done = false;
			pumpLoader(this);
		}
		S.seekingFired = false;
		task(() => {
			S.seekingFired = true;
			fire(this, 'seeking');
		});
		if (debug()) console.log('media: seek ' + v.toFixed(3));
	},
	get duration() {
		const S = st(this);
		if (S.mse) return S.mse._rs === 'closed' ? NaN : S.mse._dur;
		return S.readyState ? S.duration : NaN;
	},
	get paused() { return st(this).paused; },
	get ended() { return st(this).ended; },
	get seeking() { return st(this).seeking; },
	get error() { return st(this).error; },
	get readyState() { return st(this).readyState; },
	get networkState() { return st(this).networkState; },
	get currentSrc() { return st(this).currentSrc; },
	get srcObject() { return st(this).srcObject; },
	set srcObject(v) { st(this).srcObject = v || null; loadMedia(this); },
	get src() { return srcDesc && srcDesc.get ? srcDesc.get.call(this) : (this.getAttribute('src') || ''); },
	set src(v) {
		elSetAttribute.call(this, 'src', String(v));
		loadMedia(this);
	},
	setAttribute(name, value) {
		elSetAttribute.call(this, name, value);
		if (String(name).toLowerCase() === 'src') loadMedia(this);
		else if (String(name).toLowerCase() === 'muted' && !st(this).h) st(this).muted = true;
	},
	removeAttribute(name) {
		elRemoveAttribute.call(this, name);
		if (String(name).toLowerCase() === 'controls' && st(this).h) N.mdControls(st(this).h, false);
	},
	get buffered() {
		const S = st(this);
		return new TimeRanges(S.open ? N.mdBuffered(S.h, -1) : []);
	},
	get seekable() {
		const S = st(this);
		if (S.mse && S.mse._live) return new TimeRanges(S.mse._live.slice());
		const d = this.duration;
		if (!(S.readyState >= 1) || isNaN(d)) return new TimeRanges([]);
		if (d === Infinity) {
			const b = S.open ? N.mdBuffered(S.h, -1) : [];
			return new TimeRanges(b.length ? [b[0], b[b.length - 1]] : []);
		}
		return new TimeRanges([0, d]);
	},
	get played() {
		const S = st(this);
		return new TimeRanges(S.readyState ? [0, Math.max(S.time, 0)] : []);
	},
	get volume() { return st(this).volume; },
	set volume(v) {
		const S = st(this);
		v = +v;
		if (!(v >= 0 && v <= 1)) throw domError('The volume provided (' + v + ') is outside the range [0, 1].', 'IndexSizeError');
		if (v === S.volume) return;
		S.volume = v;
		if (S.h) N.mdVolume(S.h, v, S.muted ? 1 : 0);
		task(() => fire(this, 'volumechange'));
	},
	get muted() { return st(this).muted; },
	set muted(v) {
		const S = st(this);
		v = !!v;
		if (v === S.muted) return;
		S.muted = v;
		if (S.h) N.mdVolume(S.h, S.volume, v ? 1 : 0);
		task(() => fire(this, 'volumechange'));
	},
	get defaultMuted() { return this.hasAttribute('muted'); },
	set defaultMuted(v) { v ? elSetAttribute.call(this, 'muted', '') : elRemoveAttribute.call(this, 'muted'); },
	get playbackRate() { return st(this).rate; },
	set playbackRate(v) {
		const S = st(this);
		v = +v;
		if (!isFinite(v)) throw new TypeError('The provided double value is non-finite.');
		if (v === S.rate) return;
		S.rate = v;
		if (S.h && v > 0) N.mdRate(S.h, v);
		task(() => fire(this, 'ratechange'));
	},
	get defaultPlaybackRate() { return st(this).defaultRate; },
	set defaultPlaybackRate(v) { st(this).defaultRate = +v; task(() => fire(this, 'ratechange')); },
	get preservesPitch() { return false; },
	set preservesPitch(v) {},
	get textTracks() { return st(this).textTracks; },
	get audioTracks() { return st(this).audioTracks; },
	get videoTracks() { return st(this).videoTracks; },
	addTextTrack(kind, label, lang) { const t = new TextTrack(kind, label, lang); st(this).textTracks._l.push(t); return t; },
	get mediaKeys() { return null; },
	setMediaKeys() { return Promise.reject(domError('Encrypted media is not supported.', 'NotSupportedError')); },
	get sinkId() { return ''; },
	setSinkId() { return Promise.reject(domError('Audio output devices are not supported.', 'NotSupportedError')); },
	captureStream() { throw domError('captureStream is not supported.', 'NotSupportedError'); },
	get disableRemotePlayback() { return this.hasAttribute('disableremoteplayback'); },
	set disableRemotePlayback(v) { v ? elSetAttribute.call(this, 'disableremoteplayback', '') : elRemoveAttribute.call(this, 'disableremoteplayback'); },
	get remote() { return { state: 'disconnected', watchAvailability() { return Promise.reject(domError('', 'NotSupportedError')); }, prompt() { return Promise.reject(domError('', 'NotSupportedError')); }, addEventListener() {}, removeEventListener() {} }; },
	get controlsList() { return new G.DOMTokenList(this, 'controlslist'); },
	/* (Chrome's counters, read by players) */
	get webkitDecodedFrameCount() { return st(this).decoded; },
	get webkitDroppedFrameCount() { return st(this).dropped; },
	get webkitAudioDecodedByteCount() { return 0; },
	get webkitVideoDecodedByteCount() { return 0; },
});
for (const k of ['preload', 'crossOrigin'])
	if (!Object.getOwnPropertyDescriptor(ME, k))
		Object.defineProperty(ME, k, { configurable: true, enumerable: true,
			get() { const v = this.getAttribute(k.toLowerCase()); return k === 'preload' ? (v === 'none' || v === 'metadata' || v === 'auto' ? v : (v === '' ? 'auto' : 'metadata')) : v; },
			set(v) { elSetAttribute.call(this, k.toLowerCase(), String(v)); } });
for (const [k, v] of [['NETWORK_EMPTY', 0], ['NETWORK_IDLE', 1], ['NETWORK_LOADING', 2], ['NETWORK_NO_SOURCE', 3],
		['HAVE_NOTHING', 0], ['HAVE_METADATA', 1], ['HAVE_CURRENT_DATA', 2], ['HAVE_FUTURE_DATA', 3], ['HAVE_ENOUGH_DATA', 4]]) {
	Object.defineProperty(ME, k, { value: v });
	Object.defineProperty(G.HTMLMediaElement, k, { value: v });
}
for (const t of ['loadstart', 'progress', 'suspend', 'emptied', 'stalled', 'loadedmetadata', 'loadeddata',
		'canplay', 'canplaythrough', 'playing', 'waiting', 'seeking', 'seeked', 'timeupdate',
		'durationchange', 'ratechange', 'volumechange', 'encrypted', 'waitingforkey'])
	handlerProperty(ME, t);

const VE = G.HTMLVideoElement.prototype;
def(VE, {
	get videoWidth() { return st(this).vw; },
	get videoHeight() { return st(this).vh; },
	getVideoPlaybackQuality() { const S = st(this); return new VideoPlaybackQuality(S.decoded, S.dropped); },
	requestVideoFrameCallback(cb) {
		if (typeof cb !== 'function') throw new TypeError('requestVideoFrameCallback: not a function');
		const S = st(this);
		const id = ++S.rvfcId;
		S.rvfc.set(id, cb);
		if (S.h) N.mdFrames(S.h, true);
		return id;
	},
	cancelVideoFrameCallback(id) { st(this).rvfc.delete(id); },
	requestPictureInPicture() { return Promise.reject(domError('Picture-in-Picture is not supported.', 'NotSupportedError')); },
	get disablePictureInPicture() { return this.hasAttribute('disablepictureinpicture'); },
	set disablePictureInPicture(v) { v ? elSetAttribute.call(this, 'disablepictureinpicture', '') : elRemoveAttribute.call(this, 'disablepictureinpicture'); },
	get playsInline() { return this.hasAttribute('playsinline'); },
	set playsInline(v) { v ? elSetAttribute.call(this, 'playsinline', '') : elRemoveAttribute.call(this, 'playsinline'); },
	get webkitSupportsFullscreen() { return true; },
	get webkitDisplayingFullscreen() { return fsElement === this; },
	webkitEnterFullscreen() { enterFullscreen(this).catch(() => {}); },
	webkitExitFullscreen() { if (fsElement === this) exitFullscreen(); },
	get width() { return parseInt(this.getAttribute('width'), 10) || 0; },
	set width(v) { elSetAttribute.call(this, 'width', String(v >>> 0)); },
	get height() { return parseInt(this.getAttribute('height'), 10) || 0; },
	set height(v) { elSetAttribute.call(this, 'height', String(v >>> 0)); },
});
if (!Object.getOwnPropertyDescriptor(VE, 'poster'))
	Object.defineProperty(VE, 'poster', { configurable: true, enumerable: true,
		get() { const v = this.getAttribute('poster'); if (!v) return ''; try { return new G.URL(v, G.document.baseURI).href; } catch (e) { return v; } },
		set(v) { elSetAttribute.call(this, 'poster', String(v)); } });

G.Audio = function Audio(src) {
	const a = G.document.createElement('audio');
	a.preload = 'auto';
	if (src !== undefined) a.src = src;
	return a;
};
G.Audio.prototype = G.HTMLAudioElement.prototype;

/* a <video> / <audio> the parser made (qjs_media.c, when its box is made): its resource
 * selected (a src, <source> children), autoplay */
Object.defineProperty(G, '__onyxMediaBox', { configurable: true, value(el) {
	const S = st(el);
	if (S.networkState !== EMPTY || S.boxSeen && !el.hasAttribute('src')) return;
	S.boxSeen = true;
	if (el.hasAttribute('src') || sourceChildren(el).length) loadMedia(el);
} });
G.document && G.document.addEventListener && G.document.addEventListener('DOMContentLoaded', () => {
	for (const el of G.document.querySelectorAll('video, audio'))
		G.__onyxMediaBox(el);
});

/* ---- Media Source Extensions ------------------------------------------------------------------ */

class SourceBufferList extends G.EventTarget {
	constructor() { super(); this._l = []; }
	get length() { return this._l.length; }
	_add(sb) {
		if (this._l.includes(sb)) return;
		this._l.push(sb);
		this._sync();
	}
	_remove(sb) {
		const i = this._l.indexOf(sb);
		if (i < 0) return;
		this._l.splice(i, 1);
		this._sync();
	}
	_sync() {
		for (let i = 0; i < this._l.length + 1; i++) {
			if (i < this._l.length) Object.defineProperty(this, i, { value: this._l[i], configurable: true, enumerable: true });
			else delete this[i];
		}
	}
	[Symbol.iterator]() { return this._l[Symbol.iterator](); }
	get [Symbol.toStringTag]() { return 'SourceBufferList'; }
}
for (const t of ['addsourcebuffer', 'removesourcebuffer'])
	handlerProperty(SourceBufferList.prototype, t);

const msURLs = new Map();

class MediaSource extends G.EventTarget {
	constructor() {
		super();
		this._rs = 'closed';
		this._dur = NaN;
		this._el = null;
		this._h = null;
		this._live = null;
		this._sbs = new SourceBufferList();
		this._active = new SourceBufferList();
	}
	static isTypeSupported(type) {
		const r = N.mdType(String(type), 1) > 0;
		if (debug()) console.log('media: isTypeSupported(' + type + ') ' + r);
		return r;
	}
	static get canConstructInDedicatedWorker() { return false; }
	get readyState() { return this._rs; }
	get sourceBuffers() { return this._sbs; }
	get activeSourceBuffers() { return this._active; }
	get duration() { return this._rs === 'closed' ? NaN : this._dur; }
	set duration(v) {
		v = +v;
		if (isNaN(v) || v < 0) throw new TypeError('The provided double value (' + v + ') is invalid.');
		if (this._rs !== 'open') throw domError('The MediaSource\'s readyState is not \'open\'.', 'InvalidStateError');
		if (this._sbs._l.some(s => s._updating)) throw domError('A SourceBuffer is updating.', 'InvalidStateError');
		this._durationChange(v);
	}
	get handle() { return this; }
	_durationChange(v) {
		if (v === this._dur) return;
		this._dur = v;
		if (this._h) N.mdDuration(this._h, isFinite(v) ? v : 0);
		const el = this._el;
		if (el) task(() => fire(el, 'durationchange'));
	}
	_attach(el, h) {
		this._el = el;
		this._h = h;
		this._rs = 'open';
		task(() => fire(this, 'sourceopen'));
	}
	_detach() {
		for (const sb of this._sbs._l.slice()) sb._removed = true;
		this._sbs._l = [];
		this._sbs._sync();
		this._active._l = [];
		this._active._sync();
		this._el = null;
		this._h = null;
		this._dur = NaN;
		if (this._rs !== 'closed') {
			this._rs = 'closed';
			task(() => fire(this, 'sourceclose'));
		}
	}
	addSourceBuffer(type) {
		type = String(type);
		if (!type) throw new TypeError('The type provided is empty.');
		if (!MediaSource.isTypeSupported(type))
			throw domError('The type provided (\'' + type + '\') is unsupported.', 'NotSupportedError');
		if (this._rs !== 'open') throw domError('The MediaSource\'s readyState is not \'open\'.', 'InvalidStateError');
		const id = N.mdAddSource(this._h, type);
		if (id === AV_EFULL) throw domError('This MediaSource has reached the limit of SourceBuffer objects it can handle.', 'QuotaExceededError');
		if (id < 0) throw domError('The type provided (\'' + type + '\') is unsupported.', 'NotSupportedError');
		const sb = new SourceBuffer(this, id, type);
		this._sbs._add(sb);
		task(() => fire(this._sbs, 'addsourcebuffer'));
		if (debug()) console.log('media: addSourceBuffer(' + type + ') ' + id);
		return sb;
	}
	removeSourceBuffer(sb) {
		if (!(sb instanceof SourceBuffer) || !this._sbs._l.includes(sb))
			throw domError('The SourceBuffer provided is not contained in this MediaSource.', 'NotFoundError');
		if (sb._updating) {
			sb._updating = false;
			task(() => { fire(sb, 'abort'); fire(sb, 'updateend'); });
		}
		N.mdRemoveSource(this._h, sb._id);
		sb._removed = true;
		this._sbs._remove(sb);
		this._active._remove(sb);
		task(() => fire(this._sbs, 'removesourcebuffer'));
	}
	endOfStream(err) {
		if (this._rs !== 'open') throw domError('The MediaSource\'s readyState is not \'open\'.', 'InvalidStateError');
		if (this._sbs._l.some(s => s._updating)) throw domError('A SourceBuffer is updating.', 'InvalidStateError');
		this._rs = 'ended';
		task(() => fire(this, 'sourceended'));
		if (err === 'network' || err === 'decode') {
			if (this._el) mediaError(this._el, err === 'network' ? 2 : 3, 'MediaSource.endOfStream(' + err + ')');
			return;
		}
		/* the duration: the highest end buffered */
		let top = 0;
		for (const sb of this._sbs._l) {
			const b = N.mdBuffered(this._h, sb._id);
			if (b.length && b[b.length - 1] > top) top = b[b.length - 1];
		}
		if (top > 0) this._durationChange(top);
		N.mdEos(this._h, 1);
	}
	_reopen() {
		if (this._rs === 'ended') {
			this._rs = 'open';
			N.mdEos(this._h, 0);
			task(() => fire(this, 'sourceopen'));
		}
	}
	setLiveSeekableRange(start, end) {
		if (this._rs !== 'open') throw domError('The MediaSource\'s readyState is not \'open\'.', 'InvalidStateError');
		if (start < 0 || start > end) throw new TypeError('Invalid range');
		this._live = [+start, +end];
	}
	clearLiveSeekableRange() {
		if (this._rs !== 'open') throw domError('The MediaSource\'s readyState is not \'open\'.', 'InvalidStateError');
		this._live = null;
	}
	get [Symbol.toStringTag]() { return 'MediaSource'; }
}
for (const t of ['sourceopen', 'sourceended', 'sourceclose'])
	handlerProperty(MediaSource.prototype, t);

class SourceBuffer extends G.EventTarget {
	constructor(ms, id, type) {
		super();
		this._ms = ms;
		this._id = id;
		this._type = type;
		this._updating = false;
		this._removed = false;
		this._mode = 'segments';
		this._aws = 0;
		this._awe = Infinity;
		this._gotInit = false;
		this._audio = new G.AudioTrackList();
		this._video = new G.VideoTrackList();
		this._text = new G.TextTrackList();
	}
	_check() {
		if (this._removed) throw domError('This SourceBuffer has been removed from the parent media source.', 'InvalidStateError');
		if (this._updating) throw domError('This SourceBuffer is still processing an \'appendBuffer\' or \'remove\' operation.', 'InvalidStateError');
	}
	get updating() { return this._updating; }
	get mode() { return this._mode; }
	set mode(v) {
		v = String(v);
		if (v !== 'segments' && v !== 'sequence') return;
		this._check();
		this._ms._reopen();
		this._mode = v;
		N.mdMode(this._ms._h, this._id, v === 'sequence' ? 1 : 0);
	}
	get buffered() {
		if (this._removed) throw domError('This SourceBuffer has been removed from the parent media source.', 'InvalidStateError');
		return new TimeRanges(N.mdBuffered(this._ms._h, this._id));
	}
	get timestampOffset() { return this._removed ? 0 : N.mdOffset(this._ms._h, this._id); }
	set timestampOffset(v) {
		v = +v;
		if (!isFinite(v)) throw new TypeError('The provided double value is non-finite.');
		this._check();
		this._ms._reopen();
		N.mdOffset(this._ms._h, this._id, v);
	}
	get appendWindowStart() { return this._aws; }
	set appendWindowStart(v) {
		v = +v;
		this._check();
		if (!(v >= 0) || v >= this._awe) throw new TypeError('appendWindowStart out of range');
		this._aws = v;
		N.mdWindow(this._ms._h, this._id, this._aws, this._awe === Infinity ? 1e12 : this._awe);
	}
	get appendWindowEnd() { return this._awe; }
	set appendWindowEnd(v) {
		v = +v;
		this._check();
		if (isNaN(v) || v <= this._aws) throw new TypeError('appendWindowEnd out of range');
		this._awe = v;
		N.mdWindow(this._ms._h, this._id, this._aws, this._awe === Infinity ? 1e12 : this._awe);
	}
	get audioTracks() { return this._audio; }
	get videoTracks() { return this._video; }
	get textTracks() { return this._text; }
	appendBuffer(data) {
		if (!(data instanceof ArrayBuffer) && !ArrayBuffer.isView(data))
			throw new TypeError('Failed to execute \'appendBuffer\' on \'SourceBuffer\': The provided value is not of type \'(ArrayBuffer or ArrayBufferView)\'.');
		this._check();
		const ms = this._ms;
		if (!ms._h) throw domError('The MediaSource is not attached.', 'InvalidStateError');
		ms._reopen();
		const r = N.mdAppend(ms._h, this._id, data);
		if (r === AV_EFULL)
			throw domError('Failed to execute \'appendBuffer\' on \'SourceBuffer\': The SourceBuffer is full, and cannot free space to append additional buffers.', 'QuotaExceededError');
		this._updating = true;
		task(() => fire(this, 'updatestart'));
		task(() => {
			if (!this._updating) return;	/* (aborted) */
			if (r === 0 && !this._gotInit) {
				const init = N.mdInit(ms._h, this._id);
				if (init) {
					this._gotInit = true;
					if (debug()) console.log('media: init segment: ' + JSON.stringify(init));
					if (init.some(t => !t[6])) {
						this._appendError('the initialization segment\'s codecs are not supported');
						return;
					}
					ms._active._add(this);
					/* the first initialization segment: the duration (the container's, else
					 * +Infinity) */
					if (isNaN(ms._dur)) {
						const d = N.mdContainerDuration(ms._h);
						ms._durationChange(d > 0 ? d : Infinity);
					}
					const el = ms._el;
					if (el) task(() => { if (st(el).readyState === 0 && st(el).mse === ms) setReady(el, 1); });
				}
			}
			if (r < 0) {
				this._appendError('the segment could not be parsed (' + r + ')');
				return;
			}
			this._updating = false;
			fire(this, 'update');
			fire(this, 'updateend');
		});
	}
	_appendError(why) {
		if (debug()) console.log('media: append error: ' + why);
		N.mdAbort(this._ms._h, this._id);
		this._updating = false;
		fire(this, 'error');
		fire(this, 'updateend');
		if (this._ms._rs === 'open') this._ms.endOfStream('decode');
	}
	abort() {
		if (this._removed) throw domError('This SourceBuffer has been removed from the parent media source.', 'InvalidStateError');
		if (this._ms._rs !== 'open') throw domError('The MediaSource\'s readyState is not \'open\'.', 'InvalidStateError');
		if (this._updating) {
			this._updating = false;
			task(() => { fire(this, 'abort'); fire(this, 'updateend'); });
		}
		N.mdAbort(this._ms._h, this._id);
		this._aws = 0;
		this._awe = Infinity;
		N.mdWindow(this._ms._h, this._id, 0, 1e12);
	}
	remove(start, end) {
		start = +start;
		end = +end;
		this._check();
		const d = this._ms._dur;
		if (isNaN(d) || start < 0 || start > d || !(end > start))
			throw new TypeError('Failed to execute \'remove\' on \'SourceBuffer\': The start or end provided is out of range.');
		this._ms._reopen();
		this._updating = true;
		task(() => fire(this, 'updatestart'));
		task(() => {
			if (!this._updating) return;
			N.mdRemove(this._ms._h, this._id, start, end);
			this._updating = false;
			fire(this, 'update');
			fire(this, 'updateend');
		});
	}
	changeType(type) {
		type = String(type);
		if (!type) throw new TypeError('The type provided is empty.');
		this._check();
		if (!MediaSource.isTypeSupported(type))
			throw domError('The type provided (\'' + type + '\') is unsupported.', 'NotSupportedError');
		this._ms._reopen();
		N.mdChangeType(this._ms._h, this._id, type);
		this._type = type;
		this._gotInit = false;
	}
	get [Symbol.toStringTag]() { return 'SourceBuffer'; }
}
for (const t of ['updatestart', 'update', 'updateend', 'error', 'abort'])
	handlerProperty(SourceBuffer.prototype, t);

G.MediaSource = MediaSource;
G.SourceBuffer = SourceBuffer;
G.SourceBufferList = SourceBufferList;
G.MediaSourceHandle = MediaSource;
G.WebKitMediaSource = undefined;
delete G.WebKitMediaSource;

/* blob: URLs of a MediaSource (video.src = URL.createObjectURL(ms)) */
const createURL = G.URL.createObjectURL, revokeURL = G.URL.revokeObjectURL;
def(G.URL, {
	createObjectURL(obj) {
		if (obj instanceof MediaSource) {
			const u = 'blob:' + G.location.origin + '/' + G.crypto.randomUUID();
			msURLs.set(u, obj);
			return u;
		}
		return createURL.call(this, obj);
	},
	revokeObjectURL(u) {
		msURLs.delete(String(u));
		return revokeURL.call(this, u);
	},
});

/* ---- MediaCapabilities -------------------------------------------------------------------------- */

function capType(c, extra) {
	let t = String(c.contentType || '');
	if (extra) {
		if (c.width) t += '; width=' + (c.width >>> 0);
		if (c.height) t += '; height=' + (c.height >>> 0);
		if (c.framerate) t += '; framerate=' + (+c.framerate);
		if (c.transferFunction && c.transferFunction !== 'srgb') t += '; eotf=' + c.transferFunction;
	}
	return t;
}
const mediaCapabilities = {
	decodingInfo(cfg) {
		if (!cfg || typeof cfg !== 'object' || (!cfg.video && !cfg.audio))
			return Promise.reject(new TypeError('The configuration needs a video or an audio configuration.'));
		const mse = cfg.type === 'media-source' ? 1 : 0;
		let supported = true, smooth = true;
		if (cfg.video) {
			const r = N.mdSmooth(capType(cfg.video, true), mse);
			supported = supported && r[0] > 0;
			smooth = smooth && r[1];
		}
		if (cfg.audio) {
			const r = N.mdSmooth(capType(cfg.audio, false), mse);
			supported = supported && r[0] > 0;
		}
		if (cfg.keySystemConfiguration) supported = false;
		smooth = supported && smooth;
		return Promise.resolve({ supported, smooth, powerEfficient: false, keySystemAccess: null, configuration: cfg });
	},
	encodingInfo() { return Promise.resolve({ supported: false, smooth: false, powerEfficient: false }); },
};
if (G.navigator) {
	Object.defineProperty(G.navigator, 'mediaCapabilities', { configurable: true, enumerable: true, get: () => mediaCapabilities });
	/* EME: no key systems (DRM content cannot play) */
	G.navigator.requestMediaKeySystemAccess = () =>
		Promise.reject(domError('Unsupported keySystem or supportedConfigurations.', 'NotSupportedError'));
}
G.MediaCapabilities = function MediaCapabilities() { throw new TypeError('Illegal constructor'); };

/* ---- the Fullscreen API -------------------------------------------------------------------------- */

let fsElement = null, fsSaved = null;
function fsChange(el) {
	for (const t of ['fullscreenchange', 'webkitfullscreenchange']) {
		if (el && el.isConnected !== false) fire(el, t, { bubbles: true, composed: true });
		else fire(G.document, t);
	}
}
function enterFullscreen(el) {
	if (!(el instanceof G.Element)) return Promise.reject(new TypeError('not an element'));
	if (fsElement === el) return Promise.resolve();
	if (fsElement) exitFullscreen(true);
	fsSaved = { el, style: el.getAttribute('style'), x: G.scrollX, y: G.scrollY };
	el.style.cssText = (el.style.cssText ? el.style.cssText + ';' : '') +
		'position:fixed !important;left:0 !important;top:0 !important;right:0 !important;bottom:0 !important;' +
		'width:100vw !important;height:100vh !important;max-width:none !important;max-height:none !important;' +
		'min-width:0 !important;min-height:0 !important;margin:0 !important;padding:0 !important;' +
		'border:0 !important;z-index:2147483647 !important;background-color:#000;transform:none !important;' +
		'box-sizing:border-box !important';
	fsElement = el;
	G.scrollTo(0, 0);
	if (N.fullscreen) N.fullscreen(1);
	task(() => fsChange(el));
	return Promise.resolve();
}
function exitFullscreen(quiet) {
	const el = fsElement, s = fsSaved;
	if (!el) return Promise.resolve();
	fsElement = null;
	fsSaved = null;
	if (s.style === null) el.removeAttribute('style');
	else el.setAttribute('style', s.style);
	if (N.fullscreen) N.fullscreen(0);
	G.scrollTo(s.x || 0, s.y || 0);
	if (!quiet) task(() => fsChange(el));
	return Promise.resolve();
}
def(G.Element.prototype, {
	requestFullscreen(opts) { return enterFullscreen(this); },
	webkitRequestFullscreen() { enterFullscreen(this); },
	webkitRequestFullScreen() { enterFullscreen(this); },
});
if (G.Document) {
	def(G.Document.prototype, {
		exitFullscreen() { return exitFullscreen(); },
		webkitExitFullscreen() { exitFullscreen(); },
		webkitCancelFullScreen() { exitFullscreen(); },
		get fullscreenElement() { return fsElement; },
		get webkitFullscreenElement() { return fsElement; },
		get webkitCurrentFullScreenElement() { return fsElement; },
		get fullscreenEnabled() { return true; },
		get webkitFullscreenEnabled() { return true; },
		get fullscreen() { return !!fsElement; },
		get webkitIsFullScreen() { return !!fsElement; },
	});
	for (const t of ['fullscreenchange', 'fullscreenerror', 'webkitfullscreenchange'])
		handlerProperty(G.Document.prototype, t);
}
for (const t of ['fullscreenchange', 'fullscreenerror'])
	handlerProperty(G.Element.prototype, t);
if (G.document && G.document.addEventListener)
	G.document.addEventListener('keydown', e => {
		if (fsElement && (e.key === 'Escape' || e.keyCode === 27)) exitFullscreen();
	}, true);

/* (the tests: the codecs this build has) */
Object.defineProperty(G, '__onyxMediaCodecs', { configurable: true, value: () => N.mdCodecs() });
/* (the tests: the decoding's cost, the A/V sync, whether the sound is heard) */
Object.defineProperty(G, '__onyxMediaStats', { configurable: true,
	value: el => el && el[ST] ? { decodeUs: el[ST].decodeUs || 0, syncUs: el[ST].syncUs || 0, sound: !!el[ST].sound } : null });
/* (the tests: the shown frame's time and the sum of its pixels' R, G, B) */
Object.defineProperty(G, '__onyxMediaFrame', { configurable: true,
	value: el => el && el[ST] && el[ST].h ? N.mdFrameInfo(el[ST].h) : null });

})
