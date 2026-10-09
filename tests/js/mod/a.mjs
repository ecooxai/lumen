import { b } from './b.mjs';
export const a = b * 2;
export const meta = import.meta.url;
export const resolved = import.meta.resolve('./c.mjs');
