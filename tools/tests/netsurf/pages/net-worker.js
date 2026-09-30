/* net-worker.html's dedicated worker */
importScripts('net-worker-lib.js');
self.onmessage = ev => {
	const d = ev.data;
	if (d === 'scope') {
		postMessage('scope ' + [typeof document, typeof window, typeof self, self === globalThis,
			typeof importScripts, self instanceof WorkerGlobalScope, typeof DedicatedWorkerGlobalScope,
			typeof HTMLElement, location.pathname.split('/').pop(), name].join(' '));
	} else if (d === 'lib') {
		postMessage('lib ' + libDouble(21));
	} else if (d === 'sync-import') {
		importScripts('net-worker-lib2.js');	/* (not fetched before the script ran) */
		postMessage('sync ' + lib2());
	} else if (d === 'timer') {
		setTimeout(() => postMessage('timer fired'), 10);
	} else if (d === 'fetch') {
		fetch('js-fetch.json').then(r => r.json()).then(j => postMessage('fetch ' + JSON.stringify(j)),
			e => postMessage('fetch failed ' + e));
	} else if (d === 'throw') {
		throw new TypeError('boom');
	} else if (d === 'bc') {
		const bc = new BroadcastChannel('net-test');
		bc.postMessage('from the worker');
		bc.close();
	} else if (d === 'close') {
		postMessage('closing');
		close();
		postMessage('after close (not delivered)');
	} else if (d && typeof d === 'object') {
		/* the structured clone: sent back as it came */
		postMessage({ echo: d, kinds: [d.map instanceof Map, d.set instanceof Set, d.date instanceof Date,
			d.re instanceof RegExp, d.u8 instanceof Uint8Array, d.blob instanceof Blob, d.err instanceof RangeError,
			d.self === d, d.big === 12345678901234567890n] });
	} else {
		postMessage('echo ' + d);
	}
};
postMessage('ready');
