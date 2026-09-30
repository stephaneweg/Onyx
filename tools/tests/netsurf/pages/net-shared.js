/* net-worker.html's shared worker: a count kept between its connections */
let count = 0, conns = 0;
onconnect = ev => {
	const port = ev.ports[0];
	conns++;
	port.onmessage = m => { count += m.data; port.postMessage('shared count ' + count + ' conns ' + conns); };
};
