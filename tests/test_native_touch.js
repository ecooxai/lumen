const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
const root = path.resolve(__dirname, '../src/js/prelude');
const events = fs.readFileSync(path.join(root, '01-events.js'), 'utf8');
const extra = fs.readFileSync(path.join(root, '07z-extra.js'), 'utf8');
const touch = fs.readFileSync(path.join(root, '07zz-touch.js'), 'utf8');
function fixture() {
    return vm.runInNewContext(`(function() {
        const N = { now: () => 123, isNode: () => false, log: () => {} };
        const G = { scrollX: 5, scrollY: 200 };
        ${events.slice(events.indexOf("'use strict';"), events.indexOf('class AbortSignal'))}
        ${extra.slice(extra.indexOf('const copyInit'), extra.indexOf('class CompositionEvent'))}
        ${touch}
        return { target: new EventTarget(), second: new EventTarget(), nativeTouch,
            nativeTouchInit, PointerEvent, TouchEvent, MouseEvent };
    })()`, {});
}
test('contact starts immediately, release has empty active lists and retained identity', () => {
    const f = fixture(), seen = [];
    for (const type of ['pointerover', 'pointerenter', 'pointerdown', 'touchstart',
        'pointerup', 'pointerout', 'pointerleave', 'touchend'])
        f.target.addEventListener(type, e => seen.push(e));
    assert.equal(f.nativeTouch(f.target, 'start', '9:88', 10, 20, .75), 8);
    assert.equal(seen.length, 4); // before any release arrives
    const down = seen[2], start = seen[3];
    assert.equal(down.pointerType, 'touch');
    assert.equal(down.pressure, .75);
    assert.equal(down.buttons, 1);
    assert.equal(start.isTrusted, true);
    assert.equal(start.touches.length, 1);
    assert.equal(start.touches.item(0).target, f.target);
    assert.equal(start.touches.item(0).pageY, 220);
    assert.equal(start.touches.item(8), null);
    assert.equal(f.nativeTouch(null, 'end', '9:88', 11, 22, 0), 8);
    const end = seen.at(-1);
    assert.equal(end.touches.length, 0);
    assert.equal(end.targetTouches.length, 0);
    assert.equal(end.changedTouches[0].identifier, start.changedTouches[0].identifier);
    assert.equal(end.changedTouches[0].clientX, 11);
    assert.equal(seen[4].pointerId, down.pointerId);
    assert.equal(seen[4].pressure, 0);
    const click = new f.PointerEvent('click', f.nativeTouchInit('click', {}));
    assert.equal(click.pointerId, down.pointerId);
    assert.equal(click.sourceCapabilities.firesTouchEvents, true);
});
test('touch cancellation suppresses scroll and duplicate mouse/click defaults', () => {
    const f = fixture();
    f.target.addEventListener('touchstart', e => e.preventDefault());
    assert.equal(f.nativeTouch(f.target, 'start', '1', 10, 20, 1), 15);
    assert.equal(f.nativeTouch(null, 'move', '1', 10, 80, 1), 15);
    assert.equal(f.nativeTouch(null, 'end', '1', 10, 80, 0), 15);
    assert.equal(f.nativeTouchInit('click', {}).pointerType, undefined);
});
test('pointerdown cancellation only suppresses compatibility mouse events', () => {
    const f = fixture();
    f.target.addEventListener('pointerdown', e => e.preventDefault());
    assert.equal(f.nativeTouch(f.target, 'start', '1', 10, 20, 1), 10);
    assert.equal(f.nativeTouch(null, 'end', '1', 10, 20, 0), 10);
    assert.equal(f.nativeTouchInit('click', {}).pointerType, 'touch');
});
test('scroll takeover sends one pointercancel and keeps touch target stable', () => {
    const f = fixture(), seen = [];
    for (const type of ['pointermove', 'pointercancel', 'pointerup', 'touchmove', 'touchend'])
        f.target.addEventListener(type, e => seen.push(e));
    f.nativeTouch(f.target, 'start', '1', 10, 20, 1);
    f.nativeTouch(f.second, 'move', '1', 10, 50, 1);
    assert.equal(seen[0].target, f.target);
    f.nativeTouch(null, 'scroll', '1', 10, 50, 1);
    f.nativeTouch(null, 'scroll', '1', 10, 80, 1);
    f.nativeTouch(null, 'move', '1', 10, 100, 1);
    assert.equal(seen.at(-1).cancelable, false);
    assert.equal(f.nativeTouch(null, 'end', '1', 10, 100, 0) & 4, 4);
    assert.equal(seen.filter(e => e.type === 'pointercancel').length, 1);
    assert.equal(seen.filter(e => e.type === 'pointerup').length, 0);
});
test('multiple contacts have separate targets and cancellation clears state', () => {
    const f = fixture(); let a, b, ended;
    f.target.addEventListener('touchstart', e => { a = e; });
    f.second.addEventListener('touchstart', e => { b = e; });
    f.second.addEventListener('touchend', e => { ended = e; });
    f.nativeTouch(f.target, 'start', '1', 10, 20, 1);
    f.nativeTouch(f.second, 'start', '2', 20, 30, 1);
    assert.equal(a.touches.length, 1); // old event is a snapshot
    assert.equal(b.touches.length, 2);
    assert.equal(b.targetTouches.length, 1);
    f.nativeTouch(null, 'end', '2', 20, 30, 0);
    assert.equal(ended.touches.length, 1);
    let canceled = 0;
    f.target.addEventListener('touchcancel', e => { canceled++; assert.equal(e.touches.length, 0); });
    f.nativeTouch(null, 'reset', '', 0, 0, 0);
    assert.equal(canceled, 1);
    assert.equal(f.nativeTouch(null, 'end', '1', 10, 20, 0), 0);
});
test('passive listeners cannot block native scrolling; touchend can suppress tap', () => {
    const f = fixture();
    f.target.addEventListener('touchmove', e => e.preventDefault(), { passive: true });
    f.target.addEventListener('touchend', e => e.preventDefault());
    f.nativeTouch(f.target, 'start', '1', 10, 20, 1);
    assert.equal(f.nativeTouch(null, 'move', '1', 10, 30, 1), 8);
    assert.equal(f.nativeTouch(null, 'end', '1', 10, 30, 0), 14);
});
