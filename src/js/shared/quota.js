/* QuotaExceededError (WebIDL): a DOMException carrying the quota and the requested amount */
class QuotaExceededError extends DOMException {
    #quota = null; #requested = null;
    constructor(message = '', options = {}) {
        super(message, 'QuotaExceededError');
        if (options !== undefined && options !== null && typeof options !== 'object' && typeof options !== 'function') throw new TypeError("Failed to construct 'QuotaExceededError': The provided value is not of type 'QuotaExceededErrorOptions'.");
        const member = k => {
            const v = options == null ? undefined : options[k];
            if (v === undefined) return null;
            const x = Number(v);
            if (!Number.isFinite(x)) throw new TypeError(`Failed to construct 'QuotaExceededError': The provided ${k} is non-finite.`);
            if (x < 0) throw new RangeError(`Failed to construct 'QuotaExceededError': The ${k} must not be negative.`);
            return x;
        };
        this.#quota = member('quota'); this.#requested = member('requested');
        if (this.#quota !== null && this.#requested !== null && this.#requested < this.#quota) throw new RangeError("Failed to construct 'QuotaExceededError': requested must not be less than quota.");
    }
    get quota() { return this.#quota; }
    get requested() { return this.#requested; }
}
for (const k of ['quota', 'requested']) Object.defineProperty(QuotaExceededError.prototype, k, { enumerable: true });
