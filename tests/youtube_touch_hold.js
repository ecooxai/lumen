const assert = require('node:assert/strict');
const vm = require('node:vm');
const script = process.argv[2];
let now = 0, next = 0, overlay = true;
const timers = new Map(), nodes = new Map();
const setTimeout = (fn, delay) => { const id = ++next; timers.set(id, {fn, at: now + delay}); return id; };
const clearTimeout = id => timers.delete(id);
function advance(ms) {
    now += ms;
    for (;;) {
        const ready = [...timers].find(([, task]) => task.at <= now);
        if (!ready) break;
        timers.delete(ready[0]); ready[1].fn();
    }
}
const document = {
    getElementById: id => id === 'player-control-overlay' ? (overlay ? {} : null) : nodes.get(id),
    createElement: tag => { assert.equal(tag, 'style'); return {remove() { nodes.delete(this.id); }}; },
    head: {appendChild(node) { nodes.set(node.id, node); }},
};
const context = vm.createContext({document, setTimeout, clearTimeout});
const tap = () => vm.runInContext(script, context);
tap(); assert.equal(nodes.size, 0, 'must not change compatibility click hit target');
advance(0); assert.equal(nodes.size, 1);
const style = nodes.get('__lumen_touch_vctl');
assert.match(style.textContent, /\.player-controls-content\{/);
assert.match(style.textContent, /:not\(:has\(\.ad-showing\)\)/);
assert.doesNotMatch(style.textContent, /pointer-events/);
advance(7000); assert.equal(nodes.size, 1);
tap(); advance(0); assert.equal(nodes.get('__lumen_touch_vctl'), style);
advance(1000); assert.equal(nodes.size, 1, 'new tap extends hold without old timeout removing it');
advance(6999); assert.equal(nodes.size, 1);
advance(1); assert.equal(nodes.size, 0, 'expires eight seconds after latest tap');
overlay = false; tap(); advance(0); assert.equal(nodes.size, 0);
assert.equal(timers.size, 0, 'no idle timer remains');
console.log('Mobile player hold defer, extension, expiry and absent overlay pass');
