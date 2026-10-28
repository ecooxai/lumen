'use strict';

// A real dedicated-worker regression. It does not import, replace, or patch WPT.
// The parent replies to `ready` with {kind: 'lumen-eventtarget-ping'} and checks
// every returned row plus the final result count. A timeout is a failure.
(() => {
    const rows = [];
    const add = EventTarget.prototype.addEventListener;
    const remove = EventTarget.prototype.removeEventListener;
    const dispatch = EventTarget.prototype.dispatchEvent;
    let sequence = 0;
    const eventName = () => 'lumen-worker-eventtarget-' + (++sequence);
    const assert = (condition, message) => {
        if (!condition) throw new Error(message);
    };
    const throwsTypeError = action => {
        let thrown;
        try { action(); } catch (error) { thrown = error; }
        assert(thrown instanceof TypeError, 'Expected a TypeError');
    };
    const test = (name, action) => {
        try {
            action();
            rows.push({name, status: 'PASS'});
        } catch (error) {
            rows.push({name, status: 'FAIL', message: String(error)});
        }
    };

    test('Bare worker-global add, dispatch and remove use the global target', () => {
        const type = eventName();
        let count = 0, observedThis, observedTarget, observedCurrent;
        function listener(event) {
            count++;
            observedThis = this;
            observedTarget = event.target;
            observedCurrent = event.currentTarget;
        }
        try {
            addEventListener(type, listener);
            const event = new Event(type);
            assert(dispatchEvent(event) === true, 'Dispatch return value');
            assert(count === 1, 'Bare listener must run exactly once');
            assert(observedThis === self && observedTarget === self && observedCurrent === self,
                   'Listener receiver and targets must be the worker global');
            assert(event.currentTarget === null, 'currentTarget must clear after dispatch');
            removeEventListener(type, listener);
            dispatchEvent(new Event(type));
            assert(count === 1, 'Bare removal must remove the global listener');
        } finally {
            remove.call(self, type, listener);
        }
    });

    test('Detached EventTarget operations with undefined receiver use the global', () => {
        const type = eventName();
        let count = 0;
        const listener = () => count++;
        try {
            add(type, listener);
            dispatch(new Event(type));
            remove(type, listener);
            dispatch(new Event(type));
            assert(count === 1, 'Detached operations must share the global listener list');
        } finally {
            remove.call(self, type, listener);
        }
    });

    test('Explicit null receiver uses the worker global', () => {
        const type = eventName();
        let count = 0;
        const listener = () => count++;
        try {
            add.call(null, type, listener);
            dispatch.call(null, new Event(type));
            remove.call(null, type, listener);
            dispatch.call(self, new Event(type));
            assert(count === 1, 'Null receivers must resolve to the real global');
        } finally {
            remove.call(self, type, listener);
        }
    });

    test('Explicit global receiver remains valid', () => {
        const type = eventName();
        let count = 0;
        const listener = () => count++;
        try {
            add.call(self, type, listener);
            dispatch.call(self, new Event(type));
            remove.call(self, type, listener);
            dispatch.call(self, new Event(type));
            assert(count === 1, 'Explicit global operations must work');
        } finally {
            remove.call(self, type, listener);
        }
    });

    test('Borrowed operations preserve a constructed EventTarget receiver', () => {
        const target = new EventTarget();
        const type = eventName();
        let count = 0, observedThis, observedTarget, observedCurrent;
        function listener(event) {
            count++;
            observedThis = this;
            observedTarget = event.target;
            observedCurrent = event.currentTarget;
        }
        add.call(target, type, listener);
        dispatch.call(self, new Event(type));
        assert(count === 0, 'A local listener must not be registered on the global');
        dispatch.call(target, new Event(type));
        assert(count === 1 && observedThis === target && observedTarget === target &&
               observedCurrent === target, 'Borrowing must preserve the actual target');
        remove.call(target, type, listener);
        dispatch.call(target, new Event(type));
        assert(count === 1, 'Borrowed removal must remove only that target listener');
    });

    test('Constructed EventTargets retain separate listener lists', () => {
        const first = new EventTarget(), second = new EventTarget();
        const type = eventName();
        let a = 0, b = 0;
        first.addEventListener(type, () => a++);
        second.addEventListener(type, () => b++);
        first.dispatchEvent(new Event(type));
        assert(a === 1 && b === 0, 'First dispatch must stay on the first target');
        second.dispatchEvent(new Event(type));
        assert(a === 1 && b === 1, 'Second dispatch must stay on the second target');
    });

    test('EventTarget subclass instances implement the interface', () => {
        class Target extends EventTarget {}
        const target = new Target(), type = eventName();
        let count = 0;
        target.addEventListener(type, () => count++);
        target.dispatchEvent(new Event(type));
        assert(count === 1, 'A constructed subclass must remain a valid EventTarget');
    });

    test('A constructed EventTarget remains valid after its prototype changes', () => {
        const target = new EventTarget(), type = eventName();
        let count = 0;
        Object.setPrototypeOf(target, null);
        const listener = () => count++;
        add.call(target, type, listener);
        dispatch.call(target, new Event(type));
        remove.call(target, type, listener);
        dispatch.call(target, new Event(type));
        assert(count === 1, 'Interface identity must not depend on the prototype chain');
    });

    test('A null callback is allowed on valid global and local receivers', () => {
        const target = new EventTarget(), type = eventName();
        add.call(self, type, null);
        add.call(target, type, null);
        remove.call(self, type, null);
        remove.call(target, type, null);
        assert(dispatch.call(self, new Event(type)) && dispatch.call(target, new Event(type)),
               'Null callbacks must leave dispatch uncancelled');
    });

    test('Global receiver normalization preserves once and cancellation behavior', () => {
        const type = eventName();
        let count = 0;
        const listener = event => { count++; event.preventDefault(); };
        try {
            add(type, listener, {once: true});
            assert(dispatch(new Event(type, {cancelable: true})) === false,
                   'A cancelling listener must make dispatch return false');
            assert(dispatch(new Event(type, {cancelable: true})) === true,
                   'A once listener must be removed before the next dispatch');
            assert(count === 1, 'A once listener must run exactly once');
        } finally {
            remove.call(self, type, listener);
        }
    });

    test('Global removal does not remove the same callback from a local target', () => {
        const target = new EventTarget(), type = eventName();
        let count = 0;
        const listener = () => count++;
        try {
            add.call(self, type, listener);
            add.call(target, type, listener);
            remove.call(null, type, listener);
            dispatch.call(self, new Event(type));
            dispatch.call(target, new Event(type));
            assert(count === 1, 'Removal must use the normalized receiver only');
        } finally {
            remove.call(self, type, listener);
            remove.call(target, type, listener);
        }
    });

    const invalidReceivers = [
        ['plain object', {}],
        ['EventTarget prototype', EventTarget.prototype],
        ['inherited EventTarget prototype', Object.create(EventTarget.prototype)],
        ['worker-global prototype', Object.getPrototypeOf(self)],
        ['boolean primitive', false],
        ['number primitive', 7],
        ['string primitive', 'target'],
        ['symbol primitive', Symbol('target')],
        ['bigint primitive', 1n],
    ];
    for (const [name, receiver] of invalidReceivers) {
        test('addEventListener rejects ' + name, () =>
            throwsTypeError(() => add.call(receiver, eventName(), () => {})));
        test('addEventListener rejects ' + name + ' even with a null callback', () =>
            throwsTypeError(() => add.call(receiver, eventName(), null)));
        test('removeEventListener rejects ' + name, () =>
            throwsTypeError(() => remove.call(receiver, eventName(), () => {})));
        test('dispatchEvent rejects ' + name, () =>
            throwsTypeError(() => dispatch.call(receiver, new Event(eventName()))));
    }

    // This final asynchronous check also proves that the actual worker pump and
    // cross-thread message serialization, beyond direct dispatch, still work.
    self.addEventListener('message', function receive(event) {
        if (!event.data || event.data.kind !== 'lumen-eventtarget-ping') return;
        self.removeEventListener('message', receive);
        test('An actual parent-to-worker message retains the global event receiver', () => {
            assert(this === self && event.target === self && event.currentTarget === self,
                   'The incoming worker message must target the global');
        });
        self.postMessage({kind: 'lumen-eventtarget-results', expected: 48, rows});
        self.close();
    });
    self.postMessage({kind: 'lumen-eventtarget-ready', completed: rows.length, expected: 48});
})();
