const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const { test } = require('node:test');
const source = fs.readFileSync('src/js/prelude/07b-net.js', 'utf8');
function harness() {
    const calls = [], ids = [];
    const context = { URL, location: { origin: 'https://page.test' }, N: {
        fetch(...args) { calls.push(args); return calls.length; }
    }};
    vm.runInNewContext(source.slice(0, source.indexOf('const fromFlat')) + '\nthis.send = corsSend;', context);
    return { calls, ids, send: (...args) => context.send(args[0], id => ids.push(id), ...args.slice(1)) };
}
const cors = ['Access-Control-Allow-Origin', 'https://page.test', 'Access-Control-Allow-Headers', 'X-Stream'];
test('successful preflight preserves streaming callbacks and request body', () => {
    const h = harness(), body = new Uint8Array([1, 2, 3]), head = () => {}, chunk = () => {}, done = () => {};
    h.send(false, 'POST', 'https://api.test/stream', ['X-Stream', '1'], body, done, { head, chunk });
    assert.equal(h.calls.length, 1);
    assert.equal(h.calls[0][0], 'OPTIONS');
    h.calls[0][4](204, 'No Content', 'https://api.test/stream', cors, null, false);
    assert.equal(h.calls.length, 2);
    const request = h.calls[1];
    assert.equal(request[0], 'POST');
    assert.equal(request[3], body);
    assert.equal(request[4], done);
    assert.equal(request[5], head);
    assert.equal(request[6], chunk);
    assert.deepEqual(h.ids, [1, 2]);
});
test('same-origin streaming does not require a preflight', () => {
    const h = harness(), head = () => {}, chunk = () => {};
    h.send(false, 'POST', 'https://page.test/stream', ['X-Stream', '1'], null, () => {}, { head, chunk });
    assert.equal(h.calls.length, 1);
    assert.equal(h.calls[0][0], 'POST');
    assert.equal(h.calls[0][5], head);
    assert.equal(h.calls[0][6], chunk);
});
test('failed preflight never starts the actual request', () => {
    for (const [status, headers, error] of [[403, cors, false], [204, [], false], [0, [], true]]) {
        const h = harness(), completed = [];
        h.send(false, 'POST', 'https://api.test/stream', ['X-Stream', '1'], null, (...a) => completed.push(a), {});
        h.calls[0][4](status, '', 'https://api.test/stream', headers, null, error);
        assert.equal(h.calls.length, 1);
        assert.equal(completed.length, 1);
        assert.equal(completed[0][0], 0);
        assert.equal(completed[0][5], true);
    }
});
test('credentialed preflight still requires explicit credentials permission', () => {
    const h = harness(), completed = [];
    h.send(true, 'POST', 'https://api.test/stream', ['X-Stream', '1'], null, (...a) => completed.push(a), {});
    h.calls[0][4](204, '', 'https://api.test/stream', cors, null, false);
    assert.equal(h.calls.length, 1);
    assert.equal(completed[0][5], true);
});
