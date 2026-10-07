// strget.js: where the time of a string-keyed table goes (jsc strget.js): the writes, the reads with
// the same string objects, the reads with new equal strings (hashed and compared), by size.
function time(name, f) {
	var t0 = Date.now(), r = f();
	print(name + ': ' + (Date.now() - t0) + ' ms' + (r !== undefined ? ' (' + r + ')' : ''));
}
[20000, 200000].forEach(function (n) {
	var k = new Array(n), k2 = new Array(n), m = new Map(), o = {};
	time(n + ' keys made twice', function () { for (var i = 0; i < n; i++) { k[i] = 'k' + i; k2[i] = 'k' + i; } });
	time(n + ' Map.set', function () { for (var i = 0; i < n; i++) m.set(k[i], i); });
	time(n + ' Map.get, the same strings', function () { var h = 0; for (var i = 0; i < n; i++) h += m.get(k[i]) & 1; return h; });
	time(n + ' Map.get, equal new strings', function () { var h = 0; for (var i = 0; i < n; i++) h += m.get(k2[i]) & 1; return h; });
	time(n + ' Map.has, absent strings', function () { var h = 0; for (var i = 0; i < n; i++) h += m.has('z' + i) ? 1 : 0; return h; });
	time(n + ' object set', function () { for (var i = 0; i < n; i++) o[k[i]] = i; });
	time(n + ' object get, the same strings', function () { var h = 0; for (var i = 0; i < n; i++) h += o[k[i]] & 1; return h; });
	time(n + ' object get, equal new strings', function () { var h = 0; for (var i = 0; i < n; i++) h += o[k2[i]] & 1; return h; });
	time(n + ' object get, the same again', function () { var h = 0; for (var i = 0; i < n; i++) h += o[k2[i]] & 1; return h; });
	time(n + ' string compare ==, equal new strings', function () { var h = 0; for (var i = 0; i < n; i++) h += k[i] == k2[i] ? 1 : 0; return h; });
});
