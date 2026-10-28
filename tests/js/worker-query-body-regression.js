
(async () => {
    const rows = await runURLParamsBodyRegression('worker');
    if (rows.length !== 72) throw new Error('Incomplete synchronous query/body cases');
    addEventListener('message', async event => {
        if (!event.data || event.data.kind !== 'lumen-query-body-ping') return;
        try {
            const form = await new Response('q=%C2x&kept=1&kept=2', {
                headers: {'Content-Type': 'application/x-www-form-urlencoded'}
            }).formData();
            if (form.get('q') !== '\ufffdx' || form.getAll('kept').join(',') !== '1,2')
                throw new Error('Asynchronous form body decode differs');
            rows.push({name: 'worker message delivery can consume a query body asynchronously', status: 'PASS'});
        } catch (error) {
            rows.push({name: 'worker message delivery can consume a query body asynchronously', status: 'FAIL',
                       message: String(error && error.stack || error)});
        }
        postMessage({kind: 'lumen-query-body-results', rows, expected: 73});
        close();
    }, {once: true});
    postMessage({kind: 'lumen-query-body-ready', completed: rows.length, expected: 73});
})().catch(error => {
    postMessage({kind: 'lumen-query-body-driver-error', message: String(error && error.stack || error)});
    close();
});
