/* Real worker ready/ping/final exchange, with explicit unique result rows. */
(async () => {
    const rows = await runFileReaderRegression('worker');
    if (rows.length !== 113 || new Set(rows.map(row => row.name)).size !== 113)
        throw new Error('Incomplete worker reader regressions');
    let received = false;
    addEventListener('message', event => {
        if (received) throw new Error('Repeated reader regression ping');
        received = true;
        const good = event.data && event.data.kind === 'lumen-reader-ping' && event.data.value === 'after-reads';
        rows.push({name: 'worker still receives real messages after reader tasks', status: good ? 'PASS' : 'FAIL'});
        postMessage({kind: 'lumen-reader-results', expected: 114, rows});
    });
    postMessage({kind: 'lumen-reader-ready', completed: 113, expected: 114});
})().catch(error => {
    postMessage({kind: 'lumen-reader-driver-error', message: String(error && error.stack || error)});
});
