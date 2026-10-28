// A real dedicated-worker driver, including a post-regression host round trip.
// No worker completion is accepted solely because source text was generated.
(async () => {
    const rows = await runBlobFileRegression('worker');
    const test = async (name, body) => {
        try { await body(); rows.push({name: 'worker bridge: ' + name, status: 'PASS'}); }
        catch (error) { rows.push({name: 'worker bridge: ' + name, status: 'FAIL', message: String(error && error.stack || error)}); }
    };
    await test('FileReaderSync consumes the genuine private Blob record', () => {
        const blob = new Blob(['sync'], {type: 'text/plain'});
        Object.defineProperties(blob, {size: {value: 0}, type: {get() { throw new Error('public MIME'); }}});
        Object.setPrototypeOf(blob, null);
        if (new FileReaderSync().readAsText(blob) !== 'sync') throw new Error('wrong synchronous bytes');
    });
    await test('FileReader consumes the genuine private Blob record', async () => {
        const blob = new Blob(['async'], {type: 'text/plain'});
        Object.defineProperty(blob, 'type', {get() { throw new Error('public MIME'); }});
        Object.setPrototypeOf(blob, null);
        const reader = new FileReader();
        const result = await new Promise((resolve, reject) => {
            reader.onload = () => resolve(reader.result); reader.onerror = () => reject(reader.error); reader.readAsText(blob);
        });
        if (result !== 'async') throw new Error('wrong asynchronous bytes');
    });
    if (rows.length !== 71 || new Set(rows.map(row => row.name)).size !== 71)
        throw new Error('incomplete or duplicate Blob/File regression rows');
    let replied = false;
    onmessage = event => {
        if (replied || !event.data || event.data.kind !== 'lumen-blob-file-ping' || event.data.value !== 'after-private-reads') {
            postMessage({kind: 'lumen-blob-file-error', message: 'invalid or duplicate post-regression ping'}); return;
        }
        replied = true;
        rows.push({name: 'worker protocol: real thread handles a message after private Blob reads', status: 'PASS'});
        postMessage({kind: 'lumen-blob-file-results', expected: 72, rows});
    };
    postMessage({kind: 'lumen-blob-file-ready', completed: rows.length, expected: 72});
})().catch(error => postMessage({kind: 'lumen-blob-file-error', message: String(error && error.stack || error)}));
