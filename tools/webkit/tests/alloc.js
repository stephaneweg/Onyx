// alloc.js: how fast scripts allocate (jsc alloc.js): what the collector costs. Each line: a kind of
// allocation, its time; a desktop does each in 5 to 40 ms, the Pi should in 30 to 200.
function time(name, f) {
	var t0 = Date.now(), r = f();
	print(name + ': ' + (Date.now() - t0) + ' ms' + (r !== undefined ? ' (' + r + ')' : ''));
}
time('400k short strings kept', function () { var a = []; for (var i = 0; i < 400000; i++) a.push('k' + i); return a.length; });
time('400k short strings dropped', function () { var n = 0; for (var i = 0; i < 400000; i++) n += ('k' + i).length; return n; });
time('200k objects kept', function () { var a = []; for (var i = 0; i < 200000; i++) a.push({a: i, b: i + 1}); return a.length; });
time('2M objects dropped', function () { var n = 0; for (var i = 0; i < 2000000; i++) n += {a: i, b: i + 1}.b & 1; return n; });
time('Map 200k set (number keys)', function () { var m = new Map(); for (var i = 0; i < 200000; i++) m.set(i, i); return m.size; });
time('Map 200k set + get (string keys)', function () {
	var m = new Map(); for (var i = 0; i < 200000; i++) m.set('k' + i, i);
	var h = 0; for (var i = 0; i < 200000; i++) h += m.get('k' + i) & 1; return h;
});
time('object as a table, 200k string keys', function () { var o = {}; for (var i = 0; i < 200000; i++) o['k' + i] = i; var h = 0; for (var i = 0; i < 200000; i++) h += o['k' + i] & 1; return h; });
time('array of 1M numbers pushed', function () { var a = []; for (var i = 0; i < 1000000; i++) a.push(i * 1.5); return a.length; });
time('string built by 200k appends', function () { var s = ''; for (var i = 0; i < 200000; i++) s += i.toString(36); return s.length; });
time('closures 400k', function () {
	function mk(i) { return function (x) { return {v: x + i, f: function () { return this.v * 2; }}; }; }
	var fs = []; for (var i = 0; i < 2000; i++) fs.push(mk(i));
	var acc = 0; for (var r = 0; r < 200; r++) for (var i = 0; i < 2000; i++) acc += fs[i](r).f(); return acc;
});
time('loop 3M (no allocation)', function () { var s = 0; for (var i = 0; i < 3000000; i++) s += (i * 7) % 13; return s; });
