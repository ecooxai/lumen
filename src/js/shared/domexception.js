/* DOMException per WebIDL, shared by the page and worker preludes (which define def()). */
const DOMEX = { __proto__: null, IndexSizeError: 1, HierarchyRequestError: 3, WrongDocumentError: 4, InvalidCharacterError: 5, NoModificationAllowedError: 7, NotFoundError: 8, NotSupportedError: 9, InUseAttributeError: 10, InvalidStateError: 11, SyntaxError: 12, InvalidModificationError: 13, NamespaceError: 14, InvalidAccessError: 15, TypeMismatchError: 17, SecurityError: 18, NetworkError: 19, AbortError: 20, URLMismatchError: 21, QuotaExceededError: 22, TimeoutError: 23, InvalidNodeTypeError: 24, DataCloneError: 25 };
const DOMEX_ERROR = Error, DOMEX_CONSTRUCT = Reflect.construct, DOMEX_STRING = String;
function domExceptionString(value) {
    // Web IDL DOMString conversion rejects a Symbol; String(symbol) does not.
    if (typeof value === 'symbol') throw new TypeError('Cannot convert a Symbol value to a string');
    return DOMEX_STRING(value);
}
function domExceptionState(value) {
    const state = N.domExceptionState(value);
    if (state === undefined) throw new TypeError('Illegal invocation');
    return state;
}
class DOMException extends Error {
    constructor(message = '', name = 'Error') {
        const m = domExceptionString(message), n = domExceptionString(name);
        // Retain a native Error's internal data without inheriting Error's
        // constructor-side properties. Returning the object from this derived
        // constructor also avoids a second newTarget.prototype lookup.
        const self = DOMEX_CONSTRUCT(DOMEX_ERROR, [], new.target);
        N.domExceptionState(self, m, n, DOMEX_ERROR);
        return self;
    }
    get name() { return domExceptionState(this)[1]; }
    get message() { return domExceptionState(this)[0]; }
    get code() { return DOMEX[domExceptionState(this)[1]] || 0; }
}
// Internal deserialization: associated strings and stack were already read
// from private state. Avoid capturing a second snapshot that would be discarded.
function domExceptionClone(state, stack) {
    const self = DOMEX_CONSTRUCT(DOMEX_ERROR, [], DOMException);
    const associatedStack = typeof stack === 'string' ? stack : undefined;
    N.domExceptionState(self, state[0], state[1], DOMEX_ERROR, associatedStack);
    if (typeof associatedStack === 'string') def(self, 'stack', associatedStack); else delete self.stack;
    return self;
}
Object.setPrototypeOf(DOMException, Function.prototype);
Object.defineProperty(DOMException.prototype, Symbol.toStringTag, { value: 'DOMException', configurable: true });
for (const key of ['name', 'message', 'code']) {
    const descriptor = Object.getOwnPropertyDescriptor(DOMException.prototype, key);
    descriptor.enumerable = true;
    Object.defineProperty(DOMException.prototype, key, descriptor);
}
['INDEX_SIZE_ERR', 'DOMSTRING_SIZE_ERR', 'HIERARCHY_REQUEST_ERR', 'WRONG_DOCUMENT_ERR', 'INVALID_CHARACTER_ERR', 'NO_DATA_ALLOWED_ERR', 'NO_MODIFICATION_ALLOWED_ERR', 'NOT_FOUND_ERR', 'NOT_SUPPORTED_ERR', 'INUSE_ATTRIBUTE_ERR', 'INVALID_STATE_ERR', 'SYNTAX_ERR', 'INVALID_MODIFICATION_ERR', 'NAMESPACE_ERR', 'INVALID_ACCESS_ERR', 'VALIDATION_ERR', 'TYPE_MISMATCH_ERR', 'SECURITY_ERR', 'NETWORK_ERR', 'ABORT_ERR', 'URL_MISMATCH_ERR', 'QUOTA_EXCEEDED_ERR', 'TIMEOUT_ERR', 'INVALID_NODE_TYPE_ERR', 'DATA_CLONE_ERR'].forEach((k, i) => { for (const o of [DOMException, DOMException.prototype]) Object.defineProperty(o, k, { value: i + 1, enumerable: true }); });
