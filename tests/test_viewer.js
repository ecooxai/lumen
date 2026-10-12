const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync(process.argv[2], 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const handlers = {};
const media = {
    naturalWidth: 1600, naturalHeight: 900, style: {},
    addEventListener(name, callback) { handlers[name] = callback; }
};
const status = {};
const levels = [10, 25, 50, 75, 100, 150, 200, 300];
const buttons = levels.map(percent => ({
    attributes: { 'data-zoom': String(percent), 'aria-pressed': String(percent === 100) },
    getAttribute(name) { return this.attributes[name]; },
    setAttribute(name, value) { this.attributes[name] = String(value); },
    addEventListener(name, callback) { this[name] = callback; }
}));
const window = { devicePixelRatio: 2, addEventListener(name, callback) { handlers[name] = callback; } };
const document = {
    getElementById(name) { return { media, status }[name]; },
    querySelectorAll(selector) { assert.equal(selector, '[data-zoom]'); return buttons; }
};
vm.runInNewContext(script, { document, window });
assert.equal(media.style.width, '800px');
assert.equal(media.style.height, '450px');
levels.forEach((percent, index) => {
    buttons[index].click();
    assert.equal(media.style.width, `${800 * percent / 100}px`);
    assert.equal(media.style.height, `${450 * percent / 100}px`);
    buttons.forEach((button, selected) => assert.equal(button.attributes['aria-pressed'], String(selected === index)));
});
window.devicePixelRatio = 1;
handlers.resize();
assert.equal(media.style.width, '4800px');
media.naturalWidth = media.naturalHeight = 0;
media.videoWidth = 1920;
media.videoHeight = 1080;
handlers.loadedmetadata();
assert.equal(media.style.width, '5760px');
handlers.error();
assert.match(status.textContent, /could not be decoded/);
console.log('viewer script tests ok');
