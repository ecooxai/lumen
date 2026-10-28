/* HTML BroadcastChannel. The including prelude supplies BC_FIRE and
   BC_LISTENERS; native routing is derived only from the owning context. */
/* Native private state cannot be intercepted by changes to public WeakMap
   methods, prototypes, or globals. DOMString conversion uses the original intrinsic. */
const bcString = String;
const bcLookup = receiver => N.bcState(receiver);
const bcState = receiver => {
    const state = bcLookup(receiver);
    if (!state) throw new TypeError('Illegal invocation');
    return state;
};
const bcRefresh = receiver => {
    const state = bcLookup(receiver);
    if (!state || state.closed) return;
    const listeners = BC_LISTENERS(receiver);
    const active = state.message !== null || state.messageerror !== null ||
        ['message', 'messageerror'].some(type => {
            const list = listeners && listeners.get(type);
            return list && list.some(listener => !listener.removed);
        });
    N.bcRetain(state.id, !!active);
};
/* The base EventTarget operations notify us too, including once/AbortSignal
   removal and borrowed EventTarget.prototype.addEventListener calls. */
const bcPreviousListenerHook = etListenerChanged;
etListenerChanged = receiver => {
    if (bcPreviousListenerHook) bcPreviousListenerHook(receiver);
    bcRefresh(receiver);
};
class BroadcastChannel extends EventTarget {
    constructor(name) {
        super();
        if (arguments.length < 1) throw new TypeError('BroadcastChannel requires a channel name');
        if (typeof name === 'symbol') throw new TypeError('Cannot convert a Symbol value to a string');
        name = bcString(name);
        const state = { name, id: 0, closed: false, message: null, messageerror: null, eligible: N.bcEligible };
        state.id = N.bcOpen(name, this, state);
    }
    get name() { return bcState(this).name; }
    postMessage(message) {
        const state = bcState(this);
        if (arguments.length < 1) throw new TypeError('postMessage requires a message');
        if (!state.eligible()) return;
        if (state.closed) throw new DOMException('BroadcastChannel is closed', 'InvalidStateError');
        N.bcPost(state.id, message);
    }
    close() {
        const state = bcState(this);
        if (!state.closed) { state.closed = true; N.bcClose(state.id); }
    }
    get onmessage() { return bcState(this).message; }
    set onmessage(handler) { const state = bcState(this); state.message = typeof handler === 'function' ? handler : null; bcRefresh(this); }
    get onmessageerror() { return bcState(this).messageerror; }
    set onmessageerror(handler) { const state = bcState(this); state.messageerror = typeof handler === 'function' ? handler : null; bcRefresh(this); }
}
Object.defineProperty(BroadcastChannel.prototype, Symbol.toStringTag, { value: 'BroadcastChannel', configurable: true });
for (const key of ['name', 'postMessage', 'close', 'onmessage', 'onmessageerror']) {
    const descriptor = Object.getOwnPropertyDescriptor(BroadcastChannel.prototype, key);
    descriptor.enumerable = true;
    Object.defineProperty(BroadcastChannel.prototype, key, descriptor);
}
N.bcInit((receiver, data, origin, error) => {
    const state = bcLookup(receiver);
    if (!state || state.closed) return;
    const event = new MessageEvent(error ? 'messageerror' : 'message', { data: error ? null : data, origin });
    BC_FIRE(receiver, event);
    bcRefresh(receiver);
});
