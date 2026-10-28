(() => {
    const rows = runURLIDLRegression();
    if (rows.length !== 83) throw new Error('Incomplete synchronous URL/DOMException cases');
    addEventListener('message', event => {
        if (!event.data || event.data.kind !== 'lumen-url-idl-ping') return;
        try {
            const value = new URL(event.data.relative, event.data.base);
            const error = new DOMException('message', 'SyntaxError');
            if (value.href !== 'https://worker.test/after?q=kept' || error.code !== 12 ||
                new URLSearchParams(DOMException).get('DATA_CLONE_ERR') !== '25')
                throw new Error('URL/DOMException behavior changed during real message delivery');
            rows.push({name: 'worker URL/DOMException interfaces survive actual message delivery', status: 'PASS'});
        } catch (error) {
            rows.push({name: 'worker URL/DOMException interfaces survive actual message delivery', status: 'FAIL',
                       message: String(error && error.stack || error)});
        }
        postMessage({kind: 'lumen-url-idl-results', rows, expected: 84});
        close();
    }, {once: true});
    postMessage({kind: 'lumen-url-idl-ready', completed: rows.length, expected: 84});
})();
