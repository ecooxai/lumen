// Runs in the actual dedicated worker. Failed RED probes remain explicit rows;
// their unexpected deliveries are counted rather than hiding the full report.
(() => {
    'use strict';
    const rows = runCloneBrandRegression(), Exception = DOMException;
    if (rows.length !== 42) throw new Error('Incomplete synchronous private-brand cases');
    function test(name, body) {
        try {
            if (body() !== true) throw new Error('Unexpected worker serialization result');
            rows.push({name, status: 'PASS'});
        } catch (error) {
            rows.push({name, status: 'FAIL', message: String(error && error.stack || error)});
        }
    }
    function cloneError(body) {
        try { body(); return false; }
        catch (error) { return error instanceof Exception && error.name === 'DataCloneError' && error.code === 25; }
    }
    for (const name of ['URL', 'URLSearchParams']) {
        const create = () => new globalThis[name](name === 'URL' ? 'https://worker.test/?q=one' : 'q=one');
        test(name + ' is rejected by the native worker-to-owner serializer', () =>
            cloneError(() => postMessage({kind: 'lumen-clone-rejection-probe', value: create()})));
        test(name + ' is rejected by a worker MessagePort', () => {
            const channel = new MessageChannel();
            try { return cloneError(() => channel.port1.postMessage({value: create()})); }
            finally { channel.port1.close(); channel.port2.close(); }
        });
        test(name + ' is rejected by a worker BroadcastChannel', () => {
            const channel = new BroadcastChannel('lumen-worker-clone-brand');
            try { return cloneError(() => channel.postMessage({value: create()})); }
            finally { channel.close(); }
        });
    }
    const wasmBytes = [0,97,115,109,1,0,0,0,1,5,1,96,0,1,127,3,2,1,0,7,9,1,5,118,97,108,117,101,0,0,10,6,1,4,0,65,7,11];
    test('worker native clone preserves compiled WebAssembly module side data', () => {
        const original = new WebAssembly.Module(new Uint8Array(wasmBytes));
        const copy = structuredClone(original);
        return copy instanceof WebAssembly.Module && copy !== original && new WebAssembly.Instance(copy).exports.value() === 7;
    });
    test('worker native clone preserves shared-buffer backing ownership', () => {
        const original = new SharedArrayBuffer(8), copy = structuredClone(original);
        new Uint8Array(copy)[0] = 91;
        return crossOriginIsolated === true && copy instanceof SharedArrayBuffer && copy !== original &&
            copy.byteLength === 8 && new Uint8Array(original)[0] === 91;
    });
    if (rows.length !== 50) throw new Error('Incomplete worker serialization cases');
    let leakedIncoming = 0, answered = false;
    addEventListener('message', event => {
        const data = event.data;
        if (data && data.kind === 'lumen-clone-rejection-probe') { leakedIncoming++; return; }
        if (!data || data.kind !== 'lumen-clone-ping' || answered) throw new Error('Unexpected worker message protocol');
        answered = true;
        test('real incoming worker message preserves module shared bytes and graph identity', () => {
            if (!(data.module instanceof WebAssembly.Module) || new WebAssembly.Instance(data.module).exports.value() !== 7 ||
                !(data.shared instanceof SharedArrayBuffer) || data.shared.byteLength !== 8 ||
                new Uint8Array(data.shared)[0] !== 17 || data.graph.self !== data.graph ||
                data.graph.first !== data.graph.second || data.graph.first.value !== 9) return false;
            new Uint8Array(data.shared)[0] = 23;
            return true;
        });
        test('failed owner-to-worker platform serialization does not enqueue a message', () => leakedIncoming === 0);
        postMessage({kind: 'lumen-clone-results', expected: 52, rows,
                     module: data.module, shared: data.shared});
        close();
    });
    postMessage({kind: 'lumen-clone-ready', completed: rows.length, expected: 52});
})();
