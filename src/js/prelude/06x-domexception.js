// Shared Web IDL DOMException interface. Native private state is shared across
// an isolate's realms; inherited prototypes and public fields cannot forge it.
function createDOMExceptionInterface(nativeIDL) {
    'use strict';
    const define = Object.defineProperty, descriptor = Object.getOwnPropertyDescriptor;
    const setPrototype = Object.setPrototypeOf, ProxyIntrinsic = Proxy;
    const StringIntrinsic = String, ErrorIntrinsic = Error, TypeErrorIntrinsic = TypeError;
    const getState = nativeIDL.get, putState = nativeIDL.put;
    const registerPrototype = nativeIDL.register, fallbackPrototype = nativeIDL.fallback;
    const kind = 2;
    const constants = [
        'INDEX_SIZE_ERR', 'DOMSTRING_SIZE_ERR', 'HIERARCHY_REQUEST_ERR', 'WRONG_DOCUMENT_ERR',
        'INVALID_CHARACTER_ERR', 'NO_DATA_ALLOWED_ERR', 'NO_MODIFICATION_ALLOWED_ERR',
        'NOT_FOUND_ERR', 'NOT_SUPPORTED_ERR', 'INUSE_ATTRIBUTE_ERR', 'INVALID_STATE_ERR',
        'SYNTAX_ERR', 'INVALID_MODIFICATION_ERR', 'NAMESPACE_ERR', 'INVALID_ACCESS_ERR',
        'VALIDATION_ERR', 'TYPE_MISMATCH_ERR', 'SECURITY_ERR', 'NETWORK_ERR', 'ABORT_ERR',
        'URL_MISMATCH_ERR', 'QUOTA_EXCEEDED_ERR', 'TIMEOUT_ERR', 'INVALID_NODE_TYPE_ERR', 'DATA_CLONE_ERR',
    ];
    const codes = Object.create(null);
    for (const [name, code] of [
        ['IndexSizeError', 1], ['HierarchyRequestError', 3], ['WrongDocumentError', 4],
        ['InvalidCharacterError', 5], ['NoModificationAllowedError', 7], ['NotFoundError', 8],
        ['NotSupportedError', 9], ['InUseAttributeError', 10], ['InvalidStateError', 11],
        ['SyntaxError', 12], ['InvalidModificationError', 13], ['NamespaceError', 14],
        ['InvalidAccessError', 15], ['TypeMismatchError', 17], ['SecurityError', 18],
        ['NetworkError', 19], ['AbortError', 20], ['URLMismatchError', 21],
        ['QuotaExceededError', 22], ['TimeoutError', 23], ['InvalidNodeTypeError', 24], ['DataCloneError', 25],
    ]) codes[name] = code;
    const domString = value => {
        if (typeof value === 'symbol') throw new TypeErrorIntrinsic('Cannot convert a Symbol value to a string');
        return StringIntrinsic(value);
    };
    const state = object => {
        const value = getState(kind, object);
        if (value === undefined) throw new TypeErrorIntrinsic('Illegal invocation');
        return value;
    };
    const DOMException = new ProxyIntrinsic(function DOMException(message = '', name = 'Error') {
        throw new TypeErrorIntrinsic("DOMException requires 'new'");
    }, {
        construct(target, args, newTarget) {
            const message = domString(args[0] === undefined ? '' : args[0]);
            const name = domString(args[1] === undefined ? 'Error' : args[1]);
            // No ordinary target allocation precedes conversion, and this is
            // the only observable Get of newTarget.prototype. The native
            // fallback follows bound/Proxy targets to the function's realm.
            let prototype = newTarget.prototype;
            if ((typeof prototype !== 'object' || prototype === null) && typeof prototype !== 'function') {
                prototype = fallbackPrototype(kind, newTarget);
            }
            // Allocation stays in the interface constructor's realm. The
            // custom Web IDL binding requires genuine ErrorData/stack slots.
            const object = new ErrorIntrinsic();
            setPrototype(object, prototype);
            putState(kind, object, { message, name });
            return object;
        },
    });
    const prototype = new ErrorIntrinsic();
    define(DOMException, 'prototype', { value: prototype, writable: false });
    registerPrototype(kind, prototype);
    define(prototype, 'constructor', { value: DOMException, writable: true, configurable: true });
    const getters = {
        get name() { return state(this).name; },
        get message() { return state(this).message; },
        get code() { return codes[state(this).name] || 0; },
    };
    for (const name of ['name', 'message', 'code']) {
        define(prototype, name, { ...descriptor(getters, name), enumerable: true, configurable: true });
    }
    for (let index = 0; index < constants.length; index++) {
        const value = { value: index + 1, enumerable: true, writable: false, configurable: false };
        define(DOMException, constants[index], value);
        define(prototype, constants[index], value);
    }
    define(prototype, Symbol.toStringTag, { value: 'DOMException', configurable: true });
    return DOMException;
}
