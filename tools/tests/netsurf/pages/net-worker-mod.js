/* net-worker.html's module worker */
import { triple } from './net-worker-dep.js';
self.onmessage = ev => postMessage('module ' + triple(ev.data) + ' ' + (typeof importScripts));
