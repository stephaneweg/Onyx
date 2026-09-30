/* tools/tests/netsurf/layoutdiff.js -- run in a page (NetSurf: NS_INJECT + F5; Chromium:
 * layoutdiff.sh's playwright script): each displayed element's border box, one line each
 * "LB <path> <x> <y> <w> <h>" (the page's coordinates; the path: tag#id.class up to 5 levels
 * with :nth-child where needed), for layoutdiff.sh to compare */
(function () {
	var out = [];
	function name(e) {
		var s = e.tagName.toLowerCase();
		if (e.id) s += '#' + e.id;
		var c = e.getAttribute('class');
		if (c) s += '.' + c.trim().split(/\s+/).slice(0, 2).join('.');
		var p = e.parentNode;
		if (p && p.children) {
			var k = 0, i = 0;
			for (var j = 0; j < p.children.length; j++) {
				if (p.children[j].tagName === e.tagName) { k++; if (p.children[j] === e) i = k; }
			}
			if (k > 1) s += ':' + i;
		}
		return s;
	}
	function path(e) {
		var a = [];
		for (var n = e, d = 0; n && n.nodeType === 1 && d < 5; n = n.parentNode, d++) {
			a.unshift(name(n));
			if (n.id) break;
		}
		return a.join('>');
	}
	var sx = window.scrollX || 0, sy = window.scrollY || 0;
	var all = document.body ? document.body.getElementsByTagName('*') : [];
	for (var i = 0; i < all.length; i++) {
		var e = all[i], t = e.tagName.toLowerCase();
		if (t === 'script' || t === 'style' || t === 'noscript' || t === 'template') continue;
		if (e.closest && e.closest('svg') && t !== 'svg') continue;
		var r = e.getBoundingClientRect();
		if (!r || (r.width === 0 && r.height === 0)) continue;
		out.push('LB ' + path(e) + ' ' + Math.round(r.left + sx) + ' ' + Math.round(r.top + sy) +
			' ' + Math.round(r.width) + ' ' + Math.round(r.height));
	}
	console.log(out.join('\n'));
})();
