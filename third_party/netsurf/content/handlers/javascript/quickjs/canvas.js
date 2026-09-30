/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: <canvas> 2D -- CanvasRenderingContext2D, CanvasGradient, CanvasPattern, Path2D,
 * ImageData, TextMetrics, OffscreenCanvas, createImageBitmap, a DOMMatrix -- on the
 * natives of qjs_canvas.c (N.cv*: PlutoVG). The state (styles, font, the save stack) is
 * kept here; each drawing call is one native. qjs.c runs this function with the natives
 * once dom.js is set up: it completes dom.js' HTMLCanvasElement and HTMLImageElement.
 */
(function (N) {
'use strict';

const G = globalThis;
const CTX = Symbol('context2d');

function def(obj, props) {
	for (const k of Object.keys(props)) {
		const d = Object.getOwnPropertyDescriptor(props, k);
		d.enumerable = false;
		d.configurable = true;
		Object.defineProperty(obj, k, d);
	}
}

const num = v => typeof v === 'number' ? v : Number(v);
const finite = (...a) => a.every(v => Number.isFinite(num(v)));
const hex2 = v => (v < 16 ? '0' : '') + v.toString(16);

/* a colour (0xAARRGGBB) as the canvas gives it back: #rrggbb, or rgba() with the
 * shortest alpha that is the same byte */
function colourText(argb) {
	const a = (argb >>> 24) & 255, r = (argb >>> 16) & 255, g = (argb >>> 8) & 255, b = argb & 255;
	if (a === 255)
		return '#' + hex2(r) + hex2(g) + hex2(b);
	let s = '0';
	if (a !== 0) {
		for (let d = 1; d <= 6; d++) {
			s = (a / 255).toFixed(d);
			if (Math.round(parseFloat(s) * 255) === a) break;
		}
		s = String(parseFloat(s));
	}
	return 'rgba(' + r + ', ' + g + ', ' + b + ', ' + s + ')';
}

/* ---- DOMMatrix (2D), if the page has none ------------------------------------------ */

if (typeof G.DOMMatrix !== 'function') {
	class DOMMatrix {
		constructor(init) {
			let m = [1, 0, 0, 1, 0, 0];
			if (Array.isArray(init) && init.length >= 6)
				m = init.length === 16 ? [init[0], init[1], init[4], init[5], init[12], init[13]] : init.slice(0, 6);
			else if (init && typeof init === 'object')
				m = [init.a ?? init.m11 ?? 1, init.b ?? init.m12 ?? 0, init.c ?? init.m21 ?? 0,
					init.d ?? init.m22 ?? 1, init.e ?? init.m41 ?? 0, init.f ?? init.m42 ?? 0];
			[this.a, this.b, this.c, this.d, this.e, this.f] = m.map(Number);
		}
		get m11() { return this.a; } set m11(v) { this.a = v; }
		get m12() { return this.b; } set m12(v) { this.b = v; }
		get m21() { return this.c; } set m21(v) { this.c = v; }
		get m22() { return this.d; } set m22(v) { this.d = v; }
		get m41() { return this.e; } set m41(v) { this.e = v; }
		get m42() { return this.f; } set m42(v) { this.f = v; }
		get is2D() { return true; }
		get isIdentity() { return this.a === 1 && this.b === 0 && this.c === 0 && this.d === 1 && this.e === 0 && this.f === 0; }
		multiply(o) {
			o = new DOMMatrix(o);
			return new DOMMatrix([this.a * o.a + this.c * o.b, this.b * o.a + this.d * o.b,
				this.a * o.c + this.c * o.d, this.b * o.c + this.d * o.d,
				this.a * o.e + this.c * o.f + this.e, this.b * o.e + this.d * o.f + this.f]);
		}
		translate(x = 0, y = 0) { return this.multiply([1, 0, 0, 1, x, y]); }
		scale(sx = 1, sy = sx) { return this.multiply([sx, 0, 0, sy, 0, 0]); }
		rotate(deg = 0) { const r = deg * Math.PI / 180, c = Math.cos(r), s = Math.sin(r); return this.multiply([c, s, -s, c, 0, 0]); }
		inverse() {
			const det = this.a * this.d - this.b * this.c;
			if (!det) return new DOMMatrix([NaN, NaN, NaN, NaN, NaN, NaN]);
			return new DOMMatrix([this.d / det, -this.b / det, -this.c / det, this.a / det,
				(this.c * this.f - this.d * this.e) / det, (this.b * this.e - this.a * this.f) / det]);
		}
		transformPoint(p = {}) {
			const x = p.x || 0, y = p.y || 0;
			return { x: this.a * x + this.c * y + this.e, y: this.b * x + this.d * y + this.f, z: 0, w: 1 };
		}
		toString() { return 'matrix(' + [this.a, this.b, this.c, this.d, this.e, this.f].join(', ') + ')'; }
		static fromMatrix(o) { return new DOMMatrix(o); }
	}
	G.DOMMatrix = DOMMatrix;
	if (typeof G.DOMMatrixReadOnly !== 'function')
		G.DOMMatrixReadOnly = DOMMatrix;
}

/* ---- gradients, patterns --------------------------------------------------------------- */

class CanvasGradient {
	constructor(kind, a) { this._n = N.cvGradient(kind, ...a); }
	addColorStop(offset, color) {
		offset = num(offset);
		if (!(offset >= 0 && offset <= 1))
			throw new DOMException('offset out of range', 'IndexSizeError');
		const c = N.cvColor(String(color));
		if (c === null)
			throw new DOMException('bad colour', 'SyntaxError');
		N.cvStop(this._n, offset, c);
	}
}
class CanvasPattern {
	constructor(n) { this._n = n; }
	setTransform() {}
}

/* ---- ImageData, TextMetrics -------------------------------------------------------------- */

class ImageData {
	constructor(a, b, c) {
		if (a instanceof Uint8ClampedArray) {
			const w = b >>> 0, h = c === undefined ? a.length / 4 / w : c >>> 0;
			if (!w || a.length !== w * h * 4)
				throw new DOMException('bad size', 'IndexSizeError');
			this.data = a; this.width = w; this.height = h;
		} else {
			const w = a >>> 0, h = b >>> 0;
			if (!w || !h)
				throw new DOMException('bad size', 'IndexSizeError');
			this.width = w; this.height = h;
			this.data = new Uint8ClampedArray(w * h * 4);
		}
		this.colorSpace = 'srgb';
	}
}
class TextMetrics {}

/* ---- Path2D ------------------------------------------------------------------------------ */

class Path2D {
	constructor(p) {
		this._ops = [];
		if (p instanceof Path2D) this._ops = p._ops.slice();
		else if (typeof p === 'string') this._ops.push(['svg', p]);
	}
	addPath(p, m) {
		if (!(p instanceof Path2D)) throw new TypeError('not a Path2D');
		const t = m ? new G.DOMMatrix(m) : null;
		this._ops.push(['add', p._ops.slice(), t ? [t.a, t.b, t.c, t.d, t.e, t.f] : null]);
	}
}
const PATH_OPS = {
	moveTo: 'cvMove', lineTo: 'cvLine', quadraticCurveTo: 'cvQuad', bezierCurveTo: 'cvCubic',
	arc: 'cvArc', arcTo: 'cvArcTo', ellipse: 'cvEllipse', rect: 'cvRect', closePath: 'cvClose',
};
for (const k of Object.keys(PATH_OPS)) {
	Path2D.prototype[k] = function (...a) { this._ops.push([PATH_OPS[k], ...a]); };
}
Path2D.prototype.roundRect = function (x, y, w, h, r) { this._ops.push(['cvRoundRect', x, y, w, h, radius(r)]); };

/* the first radius of roundRect's list (one radius drawn) */
function radius(r) {
	if (r === undefined) return 0;
	if (Array.isArray(r)) r = r.length ? r[0] : 0;
	if (r && typeof r === 'object') r = r.x || 0;
	r = num(r);
	if (r < 0) throw new RangeError('negative radius');
	return r;
}

function replay(c, ops) {
	for (const op of ops) {
		if (op[0] === 'svg') N.cvSvgPath(c, op[1]);
		else if (op[0] === 'add') {
			if (op[2]) { N.cvSave(c); N.cvTransform(c, ...op[2], false); }
			replay(c, op[1]);
			if (op[2]) N.cvRestore(c);
		} else N[op[0]](c, ...op.slice(1));
	}
}

/* ---- the 2D context ------------------------------------------------------------------------ */

const CAPS = { butt: 0, round: 1, square: 2 };
const JOINS = { miter: 0, round: 1, bevel: 2 };
const OPS = ['source-over', 'source-in', 'source-out', 'source-atop', 'destination-over',
	'destination-in', 'destination-out', 'destination-atop', 'xor', 'copy'];
const BLENDS = new Set(['lighter', 'multiply', 'screen', 'overlay', 'darken', 'lighten',
	'color-dodge', 'color-burn', 'hard-light', 'soft-light', 'difference', 'exclusion',
	'hue', 'saturation', 'color', 'luminosity', 'plus-lighter']);
const BASELINES = { alphabetic: 0, top: 1, hanging: 2, middle: 3, ideographic: 4, bottom: 5 };

function initialState() {
	return {
		fill: 0xff000000, fillText: '#000000', stroke: 0xff000000, strokeText: '#000000',
		lineWidth: 1, lineCap: 'butt', lineJoin: 'miter', miterLimit: 10,
		dash: [], dashOffset: 0, alpha: 1, op: 'source-over',
		font: '10px sans-serif', fontSize: 10, textAlign: 'start', textBaseline: 'alphabetic',
		direction: 'ltr', shadowBlur: 0, shadowColor: 'rgba(0, 0, 0, 0)', shadowOffsetX: 0,
		shadowOffsetY: 0, smoothing: true, smoothingQuality: 'low', filter: 'none',
		letterSpacing: '0px', wordSpacing: '0px', fontKerning: 'auto',
	};
}

/* a CSS font shorthand: { size (px), bold, italic, family } or null */
function parseFont(s) {
	const m = /^\s*((?:(?:normal|italic|oblique|small-caps|bold|bolder|lighter|[1-9]00|ultra-condensed|extra-condensed|condensed|semi-condensed|semi-expanded|expanded|extra-expanded|ultra-expanded)\s+)*)([\d.]+)(px|pt|pc|em|rem|%|in|cm|mm|q)?(?:\s*\/\s*[^\s]+)?\s+(.+?)\s*$/i.exec(s);
	if (!m) return null;
	const pre = m[1].toLowerCase();
	let size = parseFloat(m[2]);
	switch ((m[3] || 'px').toLowerCase()) {
	case 'pt': size *= 4 / 3; break;
	case 'pc': size *= 16; break;
	case 'em': case 'rem': size *= 16; break;
	case '%': size *= 16 / 100; break;
	case 'in': size *= 96; break;
	case 'cm': size *= 96 / 2.54; break;
	case 'mm': size *= 96 / 25.4; break;
	case 'q': size *= 96 / 101.6; break;
	}
	return {
		size, family: m[4],
		bold: /\b(bold|bolder|[6-9]00)\b/.test(pre),
		italic: /\b(italic|oblique)\b/.test(pre),
	};
}

class CanvasRenderingContext2D {
	constructor(canvas, handle) {
		Object.defineProperty(this, '_c', { value: handle, writable: true });
		Object.defineProperty(this, '_st', { value: [], writable: true });
		this._s = initialState();
		this._canvas = canvas;
		this._font();
	}
	get canvas() { return this._canvas; }
	getContextAttributes() { return { alpha: true, desynchronized: false, colorSpace: 'srgb', willReadFrequently: false }; }
	isContextLost() { return false; }
	_reset() {
		this._s = initialState();
		this._st = [];
		this._font();
	}
	reset() {
		N.cvResize(this._c, this._canvas.width, this._canvas.height);
		this._reset();
	}

	/* state */
	save() { this._st.push(Object.assign({}, this._s, { dash: this._s.dash.slice() })); N.cvSave(this._c); }
	restore() {
		if (!this._st.length) return;
		const fontBefore = this._s.font;
		this._s = this._st.pop();
		N.cvRestore(this._c);
		if (this._s.font !== fontBefore) this._font();
	}

	get fillStyle() { return this._s.fillText; }
	set fillStyle(v) { this._paint(v, 'fill'); }
	get strokeStyle() { return this._s.strokeText; }
	set strokeStyle(v) { this._paint(v, 'stroke'); }
	_paint(v, which) {
		if (v instanceof CanvasGradient || v instanceof CanvasPattern) {
			this._s[which] = v._n;
			this._s[which + 'Text'] = v;
			return;
		}
		const c = N.cvColor(String(v));
		if (c === null) return;
		this._s[which] = c;
		this._s[which + 'Text'] = colourText(c);
	}

	get lineWidth() { return this._s.lineWidth; }
	set lineWidth(v) { v = num(v); if (v > 0 && Number.isFinite(v)) { this._s.lineWidth = v; N.cvLineWidth(this._c, v); } }
	get lineCap() { return this._s.lineCap; }
	set lineCap(v) { if (v in CAPS) { this._s.lineCap = v; N.cvLineCap(this._c, CAPS[v]); } }
	get lineJoin() { return this._s.lineJoin; }
	set lineJoin(v) { if (v in JOINS) { this._s.lineJoin = v; N.cvLineJoin(this._c, JOINS[v]); } }
	get miterLimit() { return this._s.miterLimit; }
	set miterLimit(v) { v = num(v); if (v > 0 && Number.isFinite(v)) { this._s.miterLimit = v; N.cvMiter(this._c, v); } }
	getLineDash() { return this._s.dash.slice(); }
	setLineDash(a) {
		a = Array.from(a || [], num);
		if (a.some(v => !(v >= 0) || !Number.isFinite(v))) return;
		if (a.length % 2) a = a.concat(a);
		this._s.dash = a;
		N.cvDash(this._c, a, this._s.dashOffset);
	}
	get lineDashOffset() { return this._s.dashOffset; }
	set lineDashOffset(v) { v = num(v); if (Number.isFinite(v)) { this._s.dashOffset = v; N.cvDash(this._c, this._s.dash, v); } }
	get globalAlpha() { return this._s.alpha; }
	set globalAlpha(v) { v = num(v); if (v >= 0 && v <= 1) { this._s.alpha = v; N.cvAlpha(this._c, v); } }
	get globalCompositeOperation() { return this._s.op; }
	set globalCompositeOperation(v) {
		v = String(v);
		const i = OPS.indexOf(v);
		if (i < 0 && !BLENDS.has(v)) return;
		this._s.op = v;
		N.cvOp(this._c, i < 0 ? 0 : i);
	}
	get font() { return this._s.font; }
	set font(v) {
		const f = parseFont(String(v));
		if (!f) return;
		this._s.font = String(v).trim();
		this._font(f);
	}
	_font(f) {
		f = f || parseFont(this._s.font) || parseFont('10px sans-serif');
		this._s.fontSize = f.size;
		N.cvFont(this._c, f.family, f.bold, f.italic, f.size);
	}
	get textAlign() { return this._s.textAlign; }
	set textAlign(v) { if (['start', 'end', 'left', 'right', 'center'].includes(v)) this._s.textAlign = v; }
	get textBaseline() { return this._s.textBaseline; }
	set textBaseline(v) { if (v in BASELINES) this._s.textBaseline = v; }
	get direction() { return this._s.direction; }
	set direction(v) { if (['ltr', 'rtl', 'inherit'].includes(v)) this._s.direction = v; }
	get imageSmoothingEnabled() { return this._s.smoothing; }
	set imageSmoothingEnabled(v) { this._s.smoothing = !!v; }
	get imageSmoothingQuality() { return this._s.smoothingQuality; }
	set imageSmoothingQuality(v) { if (['low', 'medium', 'high'].includes(v)) this._s.smoothingQuality = v; }

	/* transforms */
	translate(x, y) { if (finite(x, y)) N.cvTransform(this._c, 1, 0, 0, 1, x, y, false); }
	scale(x, y) { if (finite(x, y)) N.cvTransform(this._c, x, 0, 0, y, 0, 0, false); }
	rotate(a) { if (finite(a)) { const c = Math.cos(a), s = Math.sin(a); N.cvTransform(this._c, c, s, -s, c, 0, 0, false); } }
	transform(a, b, c, d, e, f) { N.cvTransform(this._c, a, b, c, d, e, f, false); }
	setTransform(a, b, c, d, e, f) {
		if (a === undefined) return this.resetTransform();
		if (typeof a === 'object') { const m = new G.DOMMatrix(a); return N.cvTransform(this._c, m.a, m.b, m.c, m.d, m.e, m.f, true); }
		N.cvTransform(this._c, a, b, c, d, e, f, true);
	}
	resetTransform() { N.cvTransform(this._c, 1, 0, 0, 1, 0, 0, true); }
	getTransform() { return new G.DOMMatrix(N.cvGetTransform(this._c)); }

	/* paths */
	beginPath() { N.cvBegin(this._c); }
	closePath() { N.cvClose(this._c); }
	moveTo(x, y) { N.cvMove(this._c, x, y); }
	lineTo(x, y) { N.cvLine(this._c, x, y); }
	quadraticCurveTo(a, b, x, y) { N.cvQuad(this._c, a, b, x, y); }
	bezierCurveTo(a, b, c, d, x, y) { N.cvCubic(this._c, a, b, c, d, x, y); }
	arc(x, y, r, a0, a1, ccw) { N.cvArc(this._c, x, y, r, a0, a1, !!ccw); }
	arcTo(x1, y1, x2, y2, r) { N.cvArcTo(this._c, x1, y1, x2, y2, r); }
	ellipse(x, y, rx, ry, rot, a0, a1, ccw) { N.cvEllipse(this._c, x, y, rx, ry, rot, a0, a1, !!ccw); }
	rect(x, y, w, h) { N.cvRect(this._c, x, y, w, h); }
	roundRect(x, y, w, h, r) { N.cvRoundRect(this._c, x, y, w, h, radius(r)); }

	/* a Path2D drawn as the current path, the current one kept */
	_with(p, fn) {
		if (!(p instanceof Path2D)) return fn();
		N.cvPathPush(this._c);
		try { replay(this._c, p._ops); return fn(); } finally { N.cvPathPop(this._c); }
	}
	fill(a, b) {
		const p = a instanceof Path2D ? a : null, rule = (p ? b : a) === 'evenodd' ? 1 : 0;
		this._with(p, () => N.cvFill(this._c, this._s.fill, rule));
	}
	stroke(p) { this._with(p, () => N.cvStroke(this._c, this._s.stroke)); }
	clip(a, b) {
		const p = a instanceof Path2D ? a : null, rule = (p ? b : a) === 'evenodd' ? 1 : 0;
		this._with(p, () => N.cvClip(this._c, rule));
	}
	isPointInPath(a, b, c, d) {
		const p = a instanceof Path2D ? a : null;
		const [x, y, r] = p ? [b, c, d] : [a, b, c];
		if (!finite(x, y)) return false;
		return this._with(p, () => N.cvInPath(this._c, x, y, r === 'evenodd' ? 1 : 0));
	}
	isPointInStroke(a, b, c) {
		const p = a instanceof Path2D ? a : null;
		const [x, y] = p ? [b, c] : [a, b];
		if (!finite(x, y)) return false;
		return this._with(p, () => N.cvInStroke(this._c, x, y));
	}
	fillRect(x, y, w, h) { N.cvFillRect(this._c, this._s.fill, x, y, w, h); }
	strokeRect(x, y, w, h) { N.cvStrokeRect(this._c, this._s.stroke, x, y, w, h); }
	clearRect(x, y, w, h) { N.cvClearRect(this._c, x, y, w, h); }
	drawFocusIfNeeded() {}
	scrollPathIntoView() {}

	/* gradients, patterns */
	createLinearGradient(x0, y0, x1, y1) {
		if (!finite(x0, y0, x1, y1)) throw new TypeError('non-finite');
		return new CanvasGradient(0, [x0, y0, x1, y1, 0, 0]);
	}
	createRadialGradient(x0, y0, r0, x1, y1, r1) {
		if (!finite(x0, y0, r0, x1, y1, r1)) throw new TypeError('non-finite');
		if (r0 < 0 || r1 < 0) throw new DOMException('negative radius', 'IndexSizeError');
		return new CanvasGradient(1, [x0, y0, r0, x1, y1, r1]);
	}
	createConicGradient(a, x, y) { return new CanvasGradient(2, [a, x, y, 0, 0, 0]); }
	createPattern(img, rep) {
		const src = imageSource(img);
		if (!src) return null;
		const r = rep === null || rep === undefined || rep === '' ? 'repeat' : String(rep);
		if (!['repeat', 'repeat-x', 'repeat-y', 'no-repeat'].includes(r))
			throw new DOMException('bad repetition', 'SyntaxError');
		const n = N.cvPattern(src, r === 'no-repeat' ? 0 : 1);
		return n ? new CanvasPattern(n) : null;
	}

	/* images */
	drawImage(img, ...a) {
		const src = imageSource(img);
		if (!src) return;
		const size = N.cvSourceSize(src);
		if (!size) return;
		const [w, h] = size;
		let sx = 0, sy = 0, sw = w, sh = h, dx, dy, dw = w, dh = h;
		if (a.length >= 8) [sx, sy, sw, sh, dx, dy, dw, dh] = a;
		else if (a.length >= 4) [dx, dy, dw, dh] = a;
		else if (a.length >= 2) [dx, dy] = a;
		else throw new TypeError('drawImage: not enough arguments');
		N.cvDrawImage(this._c, src, sx, sy, sw, sh, dx, dy, dw, dh);
	}
	createImageData(a, b) {
		if (a instanceof ImageData) return new ImageData(a.width, a.height);
		return new ImageData(Math.abs(a | 0), Math.abs(b | 0));
	}
	getImageData(x, y, w, h) {
		x |= 0; y |= 0; w |= 0; h |= 0;
		if (!w || !h) throw new DOMException('zero size', 'IndexSizeError');
		if (w < 0) { x += w; w = -w; }
		if (h < 0) { y += h; h = -h; }
		return new ImageData(new Uint8ClampedArray(N.cvGetImageData(this._c, x, y, w, h)), w, h);
	}
	putImageData(img, dx, dy, x = 0, y = 0, w, h) {
		if (!(img instanceof ImageData)) throw new TypeError('not an ImageData');
		if (w === undefined) w = img.width;
		if (h === undefined) h = img.height;
		if (w < 0) { x += w; w = -w; }
		if (h < 0) { y += h; h = -h; }
		N.cvPutImageData(this._c, img.data, img.width, img.height, dx | 0, dy | 0, x | 0, y | 0, w | 0, h | 0);
	}

	/* text */
	_align() {
		const a = this._s.textAlign, rtl = this._s.direction === 'rtl';
		if (a === 'left' || (a === 'start' && !rtl) || (a === 'end' && rtl)) return 0;
		if (a === 'center') return 2;
		return 1;
	}
	fillText(t, x, y, max) { N.cvText(this._c, this._s.fill, String(t), x, y, this._align(), BASELINES[this._s.textBaseline], max === undefined ? 0 : max, false); }
	strokeText(t, x, y, max) { N.cvText(this._c, this._s.stroke, String(t), x, y, this._align(), BASELINES[this._s.textBaseline], max === undefined ? 0 : max, true); }
	measureText(t) {
		t = String(t);
		const m = N.cvMeasure(this._c, t) || [t.length * this._s.fontSize * 0.5, this._s.fontSize * 0.8, this._s.fontSize * 0.2, 0, 0, 0, 0];
		const r = new TextMetrics();
		const off = [0, 1, 0.5][this._align()] * m[0];
		const shift = [0, -m[1], -m[1] * 0.8, -(m[1] - m[2]) / 2, m[2], m[2]][BASELINES[this._s.textBaseline]];
		r.width = m[0];
		r.actualBoundingBoxLeft = m[3] + off;
		r.actualBoundingBoxRight = m[4] - off;
		r.actualBoundingBoxAscent = m[5] + shift;
		r.actualBoundingBoxDescent = m[6] - shift;
		r.fontBoundingBoxAscent = m[1] + shift;
		r.fontBoundingBoxDescent = m[2] - shift;
		r.emHeightAscent = r.fontBoundingBoxAscent;
		r.emHeightDescent = r.fontBoundingBoxDescent;
		r.alphabeticBaseline = shift;
		r.hangingBaseline = m[1] * 0.8 + shift;
		r.ideographicBaseline = -m[2] + shift;
		return r;
	}

	/* shadows and filters: kept, not drawn */
	get shadowBlur() { return this._s.shadowBlur; }
	set shadowBlur(v) { v = num(v); if (v >= 0 && Number.isFinite(v)) this._s.shadowBlur = v; }
	get shadowColor() { return this._s.shadowColor; }
	set shadowColor(v) { const c = N.cvColor(String(v)); if (c !== null) this._s.shadowColor = colourText(c); }
	get shadowOffsetX() { return this._s.shadowOffsetX; }
	set shadowOffsetX(v) { if (finite(v)) this._s.shadowOffsetX = num(v); }
	get shadowOffsetY() { return this._s.shadowOffsetY; }
	set shadowOffsetY(v) { if (finite(v)) this._s.shadowOffsetY = num(v); }
	get filter() { return this._s.filter; }
	set filter(v) { this._s.filter = String(v); }
	get letterSpacing() { return this._s.letterSpacing; }
	set letterSpacing(v) { this._s.letterSpacing = String(v); }
	get wordSpacing() { return this._s.wordSpacing; }
	set wordSpacing(v) { this._s.wordSpacing = String(v); }
	get fontKerning() { return this._s.fontKerning; }
	set fontKerning(v) { this._s.fontKerning = String(v); }
}

/* a drawImage / createPattern source: a native canvas, a loaded Image, an <img> */
function imageSource(img) {
	if (img === null || img === undefined) return null;
	if (img._bitmapSource !== undefined) return imageSource(img._bitmapSource);	/* ImageBitmap */
	if (img instanceof OffscreenCanvas) return img._ctx ? img._ctx._c : null;
	if (img instanceof G.HTMLCanvasElement) return img[CTX] ? img[CTX]._c : null;
	if (img instanceof CanvasRenderingContext2D) return img._c;
	if (G.HTMLImageElement && img instanceof G.HTMLImageElement) return img._cvimg || img;
	if (G.SVGElement && img instanceof G.SVGElement) return img;
	if (G.HTMLVideoElement && img instanceof G.HTMLVideoElement) return null;
	throw new TypeError('drawImage: not an image');
}

/* ---- <canvas> ------------------------------------------------------------------------------- */

function blankURL(w, h) {
	const n = N.cvNew(null, w, h);
	return n ? N.cvDataURL(n) : 'data:,';
}
function toBlob(url) {
	const i = url.indexOf(',');
	return new G.Blob([G.atob(url.slice(i + 1))], { type: 'image/png' });
}

const CE = G.HTMLCanvasElement && G.HTMLCanvasElement.prototype;
if (CE) {
	const dim = (el, name, dflt) => {
		const v = parseInt(el.getAttribute(name), 10);
		return v >= 0 ? v : dflt;
	};
	def(CE, {
		getContext(type) {
			if (String(type).toLowerCase() !== '2d') return null;
			if (!this[CTX]) {
				const h = N.cvNew(this, this.width, this.height);
				if (!h) return null;
				Object.defineProperty(this, CTX, { value: new CanvasRenderingContext2D(this, h), configurable: true });
			}
			return this[CTX];
		},
		toDataURL() { return this[CTX] ? N.cvDataURL(this[CTX]._c) : blankURL(this.width, this.height); },
		toBlob(cb) { const b = toBlob(this.toDataURL()); setTimeout(() => cb(b), 0); },
		transferControlToOffscreen() { throw new DOMException('not supported', 'NotSupportedError'); },
		captureStream() { throw new DOMException('not supported', 'NotSupportedError'); },
		get width() { return dim(this, 'width', 300); },
		set width(v) {
			this.setAttribute('width', String(num(v) >>> 0));
			if (this[CTX]) { N.cvResize(this[CTX]._c, this.width, this.height); this[CTX]._reset(); }
		},
		get height() { return dim(this, 'height', 150); },
		set height(v) {
			this.setAttribute('height', String(num(v) >>> 0));
			if (this[CTX]) { N.cvResize(this[CTX]._c, this.width, this.height); this[CTX]._reset(); }
		},
	});
}

/* ---- OffscreenCanvas, createImageBitmap ------------------------------------------------------- */

class OffscreenCanvas {
	constructor(w, h) { this._w = num(w) >>> 0; this._h = num(h) >>> 0; this._ctx = null; }
	get width() { return this._w; }
	set width(v) { this._w = num(v) >>> 0; if (this._ctx) { N.cvResize(this._ctx._c, this._w, this._h); this._ctx._reset(); } }
	get height() { return this._h; }
	set height(v) { this._h = num(v) >>> 0; if (this._ctx) { N.cvResize(this._ctx._c, this._w, this._h); this._ctx._reset(); } }
	getContext(type) {
		if (String(type).toLowerCase() !== '2d') return null;
		if (!this._ctx) {
			const h = N.cvNew(null, this._w, this._h);
			if (!h) return null;
			this._ctx = new CanvasRenderingContext2D(this, h);
		}
		return this._ctx;
	}
	convertToBlob() { return Promise.resolve(toBlob(this._ctx ? N.cvDataURL(this._ctx._c) : blankURL(this._w, this._h))); }
	transferToImageBitmap() {
		const c = new OffscreenCanvas(this._w, this._h);
		c.getContext('2d').drawImage(this, 0, 0);
		if (this._ctx) this._ctx.clearRect(0, 0, this._w, this._h);
		return { width: this._w, height: this._h, _bitmapSource: c, close() {} };
	}
}

function createImageBitmap(src) {
	try {
		const s = imageSource(src);
		const size = s && N.cvSourceSize(s);
		if (!size) return Promise.reject(new DOMException('image not ready', 'InvalidStateError'));
		return Promise.resolve({ width: size[0], height: size[1], _bitmapSource: src, close() {} });
	} catch (e) {
		return Promise.reject(e);
	}
}

/* ---- images a script loads: new Image() / createElement('img'), never in the page --------------- */

const IP = G.HTMLImageElement && G.HTMLImageElement.prototype;
if (IP) {
	const src = Object.getOwnPropertyDescriptor(IP, 'src');
	const width = Object.getOwnPropertyDescriptor(IP, 'width');
	const height = Object.getOwnPropertyDescriptor(IP, 'height');
	const nw = Object.getOwnPropertyDescriptor(IP, 'naturalWidth');
	const nh = Object.getOwnPropertyDescriptor(IP, 'naturalHeight');
	const load = img => {
		if (img.isConnected) return;
		const url = img.getAttribute('src');
		if (!url) return;
		img._cvdone = false;
		img._cvimg = N.cvLoadImage(url, (ok, w, h) => {
			img._cvdone = true;
			img._cvw = w;
			img._cvh = h;
			img.dispatchEvent(new G.Event(ok ? 'load' : 'error'));
		});
	};
	def(IP, {
		get src() { return src.get.call(this); },
		set src(v) { src.set.call(this, v); load(this); },
		get complete() { return this._cvdone !== false; },
		get naturalWidth() { return this._cvimg ? (this._cvw || 0) : nw ? nw.get.call(this) : 0; },
		get naturalHeight() { return this._cvimg ? (this._cvh || 0) : nh ? nh.get.call(this) : 0; },
		get width() { return this._cvimg && !this.hasAttribute('width') ? (this._cvw || 0) : width.get.call(this); },
		set width(v) { width.set.call(this, v); },
		get height() { return this._cvimg && !this.hasAttribute('height') ? (this._cvh || 0) : height.get.call(this); },
		set height(v) { height.set.call(this, v); },
	});
}

Object.assign(G, {
	CanvasRenderingContext2D, OffscreenCanvasRenderingContext2D: CanvasRenderingContext2D,
	CanvasGradient, CanvasPattern, Path2D, ImageData, TextMetrics, OffscreenCanvas,
	createImageBitmap, ImageBitmap: class ImageBitmap {},
});
})
