// probe.js: a diagnosis script for SD:/etc/web-probe.js (Web runs it in every page before the page's
// scripts). It reports to the console (SD:/etc/web-console sends it to kmsg) the script errors, the
// rejected promises, the resources that failed, and the state of the document after 10, 25 and 45 s.
(function () {
	var top_ = window.top === window;
	var tag = 'probe ' + (top_ ? 'top' : 'frame') + ' ' + location.host + ': ';
	function say(s) { try { console.log(tag + s); } catch (e) {} }
	say('start, UA ' + navigator.userAgent + ', visibility ' + document.visibilityState);
	addEventListener('error', function (e) {
		if (e.target && e.target !== window) say('resource error ' + (e.target.tagName || '?') + ' ' + (e.target.src || e.target.href || ''));
		else say('script error: ' + e.message + ' @ ' + (e.filename || '').slice(-60) + ':' + e.lineno + ':' + e.colno + (e.error && e.error.stack ? ' stack ' + ('' + e.error.stack).slice(0, 400) : ''));
	}, true);
	addEventListener('unhandledrejection', function (e) {
		var r = e.reason; say('rejection: ' + (r && r.message ? r.message : r) + (r && r.stack ? ' stack ' + ('' + r.stack).slice(0, 400) : ''));
	});
	// What the page's own code reports as an error
	['error', 'warn'].forEach(function (k) {
		var o = console[k];
		console[k] = function () {
			try { say('console.' + k + ': ' + Array.prototype.map.call(arguments, function (a) { return a && a.stack ? a.message + ' ' + ('' + a.stack).slice(0, 300) : '' + a; }).join(' ').slice(0, 600)); } catch (e) {}
			return o.apply(console, arguments);
		};
	});
	if (!top_) return;
	function state(when) {
		var d = document, all = d.getElementsByTagName('*');
		var undefinedCE = 0, names = {};
		for (var i = 0; i < all.length; i++) {
			var n = all[i].localName;
			if (n.indexOf('-') > 0 && !customElements.get(n)) { undefinedCE++; names[n] = 1; }
		}
		say(when + ': readyState ' + d.readyState + ', ' + all.length + ' elements, ' + d.scripts.length + ' scripts, body text ' + (d.body ? d.body.innerText.length : -1)
			+ ' chars, custom elements not defined ' + undefinedCE + ' [' + Object.keys(names).slice(0, 8).join(' ') + ']');
		var app = d.querySelector('ytd-app');
		if (app || window.ytcfg) say(when + ': ytd-app ' + (app ? 'present, ' + app.children.length + ' children, defined ' + !!customElements.get('ytd-app') : 'absent')
			+ ', ytInitialData ' + typeof window.ytInitialData + ', ytcfg ' + typeof window.ytcfg + ', Polymer ' + typeof window.Polymer
			+ ', yt ' + typeof window.yt + ', ytplayer ' + typeof window.ytplayer);
	}
	[10, 25, 45].forEach(function (t) { setTimeout(function () { state(t + ' s'); }, t * 1000); });
})();
