import { twice } from './worker-module-b.js';
const v = await Promise.resolve(21);
self.onmessage = e => postMessage({ v: twice(v), echo: e.data, meta: typeof import.meta.url });
