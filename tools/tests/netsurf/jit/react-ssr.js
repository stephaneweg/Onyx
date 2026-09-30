/* tools/tests/netsurf/jit/react-ssr.js -- React 18 renders 1000 table rows to a string, 10
 * times (the components of pages/perf-react.html's kind); prints the milliseconds of a render
 * (the best of the last 5) -- lower is better (bench.sh) */
var e = React.createElement;
function Row(p) {
	return e('tr', { className: p.i % 2 ? 'odd' : 'even' },
		e('td', null, 'row ' + p.i),
		e('td', null, e('a', { href: '#' + p.i, title: 'item ' + p.i }, 'value ' + p.v)),
		e('td', null, e('span', { style: { color: p.i % 3 ? 'red' : 'blue' } }, String(p.v * 2))));
}
function Table(p) {
	var rows = [];
	for (var i = 0; i < p.n; i++) rows.push(e(Row, { key: i, i: i, v: (i * p.seed) % 1000 }));
	return e('table', null, e('tbody', null, rows));
}
var best = Infinity, len = 0;
for (var k = 0; k < 10; k++) {
	var t = performance.now();
	len = ReactDOMServer.renderToString(e(Table, { n: 1000, seed: 7 + k })).length;
	t = performance.now() - t;
	if (k >= 5 && t < best) best = t;
}
print('ReactSSR: ' + best.toFixed(1) + ' ms (' + len + ' chars)');
