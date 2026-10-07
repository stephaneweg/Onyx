// strhash.js: is a table keyed by strings linear (jsc strhash.js)? Ten times the keys should take
// about ten times the time; much more means the keys collide (a string hash that is not one).
function time(name, f) {
	var t0 = Date.now(), r = f();
	print(name + ': ' + (Date.now() - t0) + ' ms' + (r !== undefined ? ' (' + r + ')' : ''));
}
function keys(n, prefix) { var a = new Array(n); for (var i = 0; i < n; i++) a[i] = prefix + i; return a; }
[2000, 20000, 200000].forEach(function (n) {
	var k;
	time(n + ' strings made', function () { k = keys(n, 'k'); });
	time(n + ' Map.set with them', function () { var m = new Map(); for (var i = 0; i < n; i++) m.set(k[i], i); return m.size; });
	time(n + ' Set.add with them', function () { var s = new Set(); for (var i = 0; i < n; i++) s.add(k[i]); return s.size; });
	time(n + ' object properties', function () { var o = {}; for (var i = 0; i < n; i++) o[k[i]] = i; return Object.keys(o).length; });
	var long_ = keys(n, 'a-rather-long-prefix-for-the-key-');
	time(n + ' Map.set, 35-character keys', function () { var m = new Map(); for (var i = 0; i < n; i++) m.set(long_[i], i); return m.size; });
});
time('200000 number to string', function () { var n = 0; for (var i = 0; i < 200000; i++) n += String(i).length; return n; });
time('200000 concatenations resolved', function () { var n = 0; for (var i = 0; i < 200000; i++) n += ('k' + i).charCodeAt(0); return n; });
time('200000 substrings', function () { var s = 'abcdefghijklmnopqrstuvwxyz', n = 0; for (var i = 0; i < 200000; i++) n += s.substring(i % 20, 26).length; return n; });
