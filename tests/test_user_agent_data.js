const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {test} = require('node:test');
const source = fs.readFileSync('src/js/prelude/08-window.js', 'utf8');
function data(ua, platform) {
    const context = {N: {userAgent: () => ua, platform: () => platform}};
    vm.runInNewContext(source.slice(source.indexOf('const __uaChrome'), source.indexOf('const navigator =')) + '\nthis.result = userAgentData;', context);
    return context.result;
}
test('Android mobile hints agree across properties, JSON and async hints', async () => {
    const ua = data('Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Mobile Safari/537.36', 'Linux armv8l');
    assert.equal(ua.mobile, true);
    assert.equal(ua.platform, 'Android');
    const json = JSON.parse(JSON.stringify(ua));
    assert.equal(json.mobile, true);
    assert.equal(json.platform, 'Android');
    assert.equal(json.brands.find(b => b.brand === 'Chromium').version, '150');
    assert.deepEqual(JSON.parse(JSON.stringify(await ua.getHighEntropyValues(['model', 'platformVersion']))), json);
    // A caller cannot change subsequent hints by editing a returned dictionary.
    ua.brands[0].version = '1';
    assert.equal(ua.brands[0].version, '150');
    assert.equal(Reflect.set(ua, 'mobile', false), false);
});
test('desktop Chrome keeps desktop hints', () => {
    for (const [platform, expected] of [['Linux x86_64', 'Linux'], ['MacIntel', 'macOS']]) {
        const ua = data('Mozilla/5.0 Chrome/150.0.0.0 Safari/537.36', platform);
        assert.equal(ua.mobile, false);
        assert.equal(ua.platform, expected);
        assert.equal(ua.toJSON().platform, expected);
    }
});
test('Firefox and Lumen profiles do not advertise Chromium client hints', () => {
    assert.equal(data('Mozilla/5.0 (Android 10; Mobile; rv:150.0) Gecko/20100101 Firefox/150.0', 'Linux armv8l'), undefined);
    assert.equal(data('Mozilla/5.0 (Linux; Android 10; K) Lumen/1.0 Mobile Safari/537.36', 'Linux armv8l'), undefined);
});
