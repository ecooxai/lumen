// Actual Window/message/child-realm routes, separate from the shared checks.
globalThis.runCloneWindowRegression = function (child) {
    'use strict';
    const rows = [], ParentException = DOMException, ChildException = child.DOMException;
    function test(name, body) {
        try {
            if (body() !== true) throw new Error('Unexpected public API result');
            rows.push({ name, status: 'PASS' });
        } catch (error) {
            rows.push({ name, status: 'FAIL', message: String(error && error.stack || error) });
        }
    }
    function cloneError(body, Interface = ParentException) {
        try { body(); return false; }
        catch (error) {
            return error instanceof Interface && error.name === 'DataCloneError' && error.code === 25;
        }
    }
    for (const name of ['URL', 'URLSearchParams']) {
        const input = name === 'URL' ? 'https://clone.test/path?q=one' : 'q=one';
        const local = () => new globalThis[name](input);
        const foreign = () => new child[name](input);
        test(name + ' is rejected by directed Window postMessage', () =>
            cloneError(() => window.postMessage({value: local()}, '*')));
        test(name + ' is rejected by the same-isolate MessagePort route', () => {
            const channel = new MessageChannel();
            try { return cloneError(() => channel.port1.postMessage({value: local()})); }
            finally { channel.port1.close(); channel.port2.close(); }
        });
        test(name + ' is rejected by BroadcastChannel serialization', () => {
            const channel = new BroadcastChannel('lumen-clone-brand');
            try { return cloneError(() => channel.postMessage({value: local()})); }
            finally { channel.close(); }
        });
        test(name + ' native Window and Broadcast routes retain the original exception interface', () => {
            const saved = globalThis.DOMException, channel = new BroadcastChannel('lumen-clone-intrinsic');
            try {
                globalThis.DOMException = function () { throw new Error('Mutable public interface used'); };
                const windowResult = cloneError(() => window.postMessage({value: local()}, '*'));
                const broadcastResult = cloneError(() => channel.postMessage({value: local()}));
                return windowResult && broadcastResult;
            } finally { globalThis.DOMException = saved; channel.close(); }
        });
        test(name + ' is rejected before history state is committed', () => {
            const before = history.length, href = location.href;
            return cloneError(() => history.pushState({value: local()}, '', '#rejected-clone')) &&
                history.length === before && location.href === href;
        });
        test(name + ' is rejected on the separate frame message-with-port route', () => {
            const channel = new MessageChannel();
            try { return cloneError(() => child.postMessage({value: local()}, '*', [channel.port1]), ChildException); }
            finally { channel.port1.close(); channel.port2.close(); }
        });
        test(name + ' authentic foreign instance is rejected in the parent realm', () =>
            cloneError(() => structuredClone(foreign())));
        test(name + ' authentic parent instance is rejected in the child realm', () =>
            cloneError(() => child.structuredClone(local()), ChildException));
        test(name + ' foreign prototype imitation remains ordinary cloneable data', () => {
            const object = Object.create(child[name].prototype); object.payload = name;
            const copy = structuredClone(object);
            return Object.getPrototypeOf(copy) === Object.prototype && copy.payload === name;
        });
    }
    return rows;
};

globalThis.runCloneDetachedRegression = function (saved) {
    'use strict';
    const rows = [];
    const error = (body, Interface) => {
        try { body(); return false; }
        catch (caught) { return caught instanceof Interface && caught.name === 'DataCloneError' && caught.code === 25; }
    };
    function test(name, body) {
        try {
            if (body() !== true) throw new Error('Unexpected detached-realm result');
            rows.push({name, status: 'PASS'});
        } catch (caught) {
            rows.push({name, status: 'FAIL', message: String(caught && caught.stack || caught)});
        }
    }
    test('retained child clone rejects a parent URL with its original exception realm', () =>
        error(() => saved.clone(new URL('https://clone.test/')), saved.Exception));
    test('parent clone rejects a genuine URL whose owner frame is inactive', () =>
        error(() => structuredClone(saved.url), DOMException));
    test('parent clone rejects parameters whose owner frame is inactive', () =>
        error(() => structuredClone(saved.params), DOMException));
    test('retained child clone still copies ordinary data safely', () => {
        const original = {value: 'kept'}, copy = saved.clone(original);
        return copy !== original && copy.value === 'kept' && Object.getPrototypeOf(copy) === saved.ObjectPrototype;
    });
    return rows;
};
