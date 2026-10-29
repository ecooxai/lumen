const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

const root = path.resolve(__dirname, '..');
// the build splices src/js/shared/NAME.js in at each /*@shared NAME.js*/ marker (tools/generate_embedded.py)
const events = fs.readFileSync(path.join(root, 'src/js/prelude/01-events.js'), 'utf8')
    .replace(/\/\*@shared ((?!abort\.js)[\w.-]+)\*\//g, (_, name) => fs.readFileSync(path.join(root, 'src/js/shared', name), 'utf8'));
const windowSource = fs.readFileSync(path.join(root, 'src/js/prelude/08-window.js'), 'utf8');
const eventSource = events.slice(events.indexOf("'use strict';"), events.indexOf('/*@shared abort.js*/'));
const mediaSource = windowSource.slice(windowSource.indexOf('const mqls ='), windowSource.indexOf('function getComputedStyle'));
const visualStart = windowSource.includes('class VisualViewport') ? 'class VisualViewport' : 'const visual =';
const visualSource = windowSource.slice(windowSource.indexOf(visualStart), windowSource.indexOf('if (navigator.maxTouchPoints)'));

function fixture() {
    let viewport = [412, 800, 0, 0, 2];
    let document;
    const errors = [];
    const eventTargets = new WeakSet();
    const native = {
        viewport: () => viewport.slice(),
        now: () => 100,
        etBrand: target => eventTargets.add(target),
        etBranded: target => eventTargets.has(target),
        isNode: target => target === document,
        parent: () => null,
        type: () => 9,
        log: (...args) => errors.push(args),
        media: query => query.includes('resolution') ? viewport[4] >= 3 : viewport[0] >= 600,
        setRealm: () => {},
        realmOf: () => null,
        fullyActive: () => true,
    };
    const result = vm.runInNewContext(`(function(N) {
        const G = {};
        ${eventSource}
        const document = new EventTarget();
        ${mediaSource}
        function fsResized() {}   // 06-document.js: fullscreenchange waits for resizes
        ${visualSource}
        Object.setPrototypeOf(G, EventTarget.prototype);
        return { G, document, Event, EventTarget, matchMedia,
            VisualViewport: typeof VisualViewport === 'undefined' ? null : VisualViewport,
            update: typeof viewportChanged === 'undefined' ? () => {} : viewportChanged };
    })`, {})(native);
    document = result.document;
    return { ...result, errors, setViewport: next => { viewport = next; } };
}

test('root scroll targets the document and bubbles to window without resize or visual pan', () => {
    const env = fixture(), order = [];
    env.G.addEventListener('scroll', () => order.push('capture'), true);
    env.document.addEventListener('scroll', event => {
        order.push('document');
        assert.equal(event.target, env.document);
        assert.equal(event.currentTarget, env.document);
        assert.equal(event.bubbles, true);
        assert.equal(event.cancelable, false);
        assert.equal(event.isTrusted, true);
        assert.equal(env.G.visualViewport.pageTop, 123);
    });
    env.G.addEventListener('scroll', event => {
        order.push('window');
        assert.equal(event.target, env.document);
        assert.equal(event.currentTarget, env.G);
    });
    env.G.onresize = env.G.visualViewport.onscroll = () => order.push('unexpected');
    env.setViewport([412, 800, 0, 123, 2]);
    env.update(false, true, false);
    assert.deepEqual(order, ['capture', 'document', 'window']);
    assert.deepEqual(env.errors, []);
});

test('scroll propagation honors stopPropagation and reports listener errors', () => {
    const env = fixture(), order = [];
    env.document.addEventListener('scroll', event => { order.push('document'); event.stopPropagation(); });
    env.document.addEventListener('scroll', () => { throw new Error('scroll listener'); });
    env.G.onscroll = () => order.push('window');
    env.update(false, true, false);
    assert.deepEqual(order, ['document']);
    assert.equal(env.errors.length, 1);
});

test('visual viewport is a stable EventTarget with read-only CSS geometry', () => {
    const env = fixture();
    const visual = env.G.visualViewport;
    assert.ok(visual instanceof env.EventTarget);
    assert.ok(visual instanceof env.VisualViewport);
    assert.throws(() => new env.VisualViewport(), /Illegal constructor/);
    assert.equal(Object.prototype.toString.call(visual), '[object VisualViewport]');
    assert.equal(visual.width, 412);
    assert.equal(visual.height, 800);
    assert.equal(visual.scale, 1);
    assert.equal(visual.offsetLeft, 0);
    assert.equal(visual.offsetTop, 0);
    assert.equal(visual.onresize, null);
    assert.equal(Reflect.set(visual, 'width', 123), false);
    assert.equal(Reflect.set(visual, 'pageTop', 123), false);
    env.setViewport([800, 412, 12, 123, 3]);
    assert.equal(env.G.visualViewport, visual);
    assert.equal(visual.width, 800);
    assert.equal(visual.height, 412);
    assert.equal(visual.pageLeft, 12);
    assert.equal(visual.pageTop, 123);
    assert.equal(visual.scale, 1);
});

test('resize dispatches window then visual then changed media lists', () => {
    const env = fixture();
    const order = [];
    const query = env.matchMedia('(min-width: 600px)');
    for (const [target, name] of [[env.G, 'window'], [env.G.visualViewport, 'visual']]) {
        target.addEventListener('resize', event => {
            order.push(name);
            assert.equal(event.target, target);
            assert.equal(event.currentTarget, target);
            assert.equal(event.bubbles, false);
            assert.equal(event.cancelable, false);
            assert.equal(event.isTrusted, true);
            assert.equal(env.G.visualViewport.width, 800);
        });
    }
    query.addEventListener('change', event => {
        order.push('media');
        assert.equal(event.target, query);
        assert.equal(event.matches, true);
        assert.equal(event.media, '(min-width: 600px)');
        assert.equal(event.isTrusted, true);
    });
    env.setViewport([800, 412, 0, 0, 2]);
    env.update(true);
    assert.deepEqual(order, ['window', 'visual', 'media']);
    assert.deepEqual(env.errors, []);
});

test('DPR changes notify resolution queries without resize', () => {
    const env = fixture();
    const changes = [];
    const query = env.matchMedia('(min-resolution: 3dppx)');
    env.G.onresize = () => changes.push('window');
    env.G.visualViewport.onresize = () => changes.push('visual');
    query.addListener(event => changes.push(event.matches));
    env.setViewport([412, 800, 0, 0, 3]);
    env.update(false);
    env.update(false);
    env.setViewport([412, 800, 0, 0, 2]);
    env.update(false);
    assert.deepEqual(changes, [true, false]);
});

test('visual handlers, once, removal, manual dispatch and listener errors', () => {
    const env = fixture();
    const visual = env.G.visualViewport;
    let calls = 0;
    const removed = () => { calls += 100; };
    visual.addEventListener('resize', removed);
    visual.removeEventListener('resize', removed);
    visual.onresize = () => { calls++; };
    visual.addEventListener('resize', () => { calls += 10; }, { once: true });
    visual.addEventListener('resize', () => { throw new Error('listener failure'); });
    env.update(true);
    env.update(true);
    assert.equal(calls, 12);
    assert.equal(env.errors.length, 2);
    assert.equal(visual.dispatchEvent(new env.Event('scroll')), true);
});

test('media list creation during notification is deferred to the next update', () => {
    const env = fixture();
    const changes = [];
    const first = env.matchMedia('(min-width: 600px)');
    let created;
    first.onchange = () => {
        changes.push('first');
        created = env.matchMedia('(min-width: 600px)');
        env.setViewport([412, 800, 0, 0, 2]);
        created.onchange = () => changes.push('created');
    };
    env.setViewport([800, 412, 0, 0, 2]);
    env.update(false);
    assert.deepEqual(changes, ['first']);
    first.onchange = null;
    env.update(false);
    assert.deepEqual(changes, ['first', 'created']);
});

test('engine binds, releases and calls viewport notification before animation frames', () => {
    const source = fs.readFileSync(path.join(root, 'src/js/js.cc'), 'utf8');
    assert.match(source, /get\("viewportChanged", c->viewportChanged\)/);
    assert.match(source, /c->viewportChanged\.Reset\(\)/);
    const update = source.slice(source.indexOf('static void viewport_update('), source.indexOf('static void tick_one(JsCtx *c);'));
    assert.match(update, /host\.sync/);
    assert.ok(update.indexOf('host.sync') < update.indexOf('jcall('));
    assert.match(source, /viewport_state\.update\(width, height, dpr, scroll_x, scroll_y\)/);
    const tick = source.slice(source.indexOf('static void tick_one(JsCtx *c) {'));
    assert.ok(tick.indexOf('viewport_update(c)') >= 0);
    assert.ok(tick.indexOf('viewport_update(c)') < tick.indexOf('if (!c->rafs.empty()'));
    assert.match(windowSource, /return \{[^\n]*viewportChanged/);
    assert.doesNotMatch(visualSource, /setInterval|setTimeout|requestAnimationFrame/);
});
