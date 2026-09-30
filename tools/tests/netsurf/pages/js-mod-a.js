import { twice, name } from './js-mod-b.js';
window.modA = 'ran';
console.log('module a ' + twice(2) + ' ' + name + ' meta ' + /js-mod-a\.js$/.test(import.meta.url));
import('./js-mod-c.js').then(m => console.log('module dynamic ' + m.c));
