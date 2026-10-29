const illegal = () => { throw new TypeError('Illegal constructor'); };
/* CSS input preprocessing: CR, CRLF and FF become LF, NUL becomes U+FFFD (the native parser sees C strings) */
const cssPre = s => /[\r\f\0]/.test(s) ? s.replace(/\r\n?|\f/g, '\n').replace(/\0/g, '\uFFFD') : s;
class Node extends EventTarget { constructor() { super(); illegal(); } }
{   /* the nodeType and document position constants, read-only on Node and its prototype */
    const k = { ELEMENT_NODE: 1, ATTRIBUTE_NODE: 2, TEXT_NODE: 3, CDATA_SECTION_NODE: 4, ENTITY_REFERENCE_NODE: 5, ENTITY_NODE: 6, PROCESSING_INSTRUCTION_NODE: 7, COMMENT_NODE: 8, DOCUMENT_NODE: 9, DOCUMENT_TYPE_NODE: 10, DOCUMENT_FRAGMENT_NODE: 11, NOTATION_NODE: 12,
        DOCUMENT_POSITION_DISCONNECTED: 1, DOCUMENT_POSITION_PRECEDING: 2, DOCUMENT_POSITION_FOLLOWING: 4, DOCUMENT_POSITION_CONTAINS: 8, DOCUMENT_POSITION_CONTAINED_BY: 16, DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC: 32 };
    for (const o of [Node, Node.prototype]) for (const n in k) Object.defineProperty(o, n, { value: k[n], enumerable: true });
}
const nextIn = (n, root) => { const f = N.first(n); if (f) return f; for (; n && n !== root; n = N.parent(n)) { const x = N.next(n); if (x) return x; } return null; };
const kids = (n) => { const a = []; for (let c = N.first(n); c; c = N.next(c)) a.push(c); return a; };
const elKids = (n) => { const a = []; for (let c = N.first(n); c; c = N.next(c)) if (N.type(c) === 1) a.push(c); return a; };
/* One pair of sets for all same-origin frames: a node's methods come from the realm that made its wrapper, so a mutation
   made through another frame's methods must still update this frame's ranges and iterators. A frame whose top window
   was not reachable yet (its prelude ran before its origin was known) links up when it makes a Range or iterator. */
const LIVE_KEY = Symbol.for('lumen:live-ranges');
/* only once this frame shares objects with another: until then its own sets see every mutation, and looking up top
   would itself mark it shared and keep it from being freed when removed */
const topLiveSets = () => { if (!N.xrefd()) return null; try { const t = N.topWin(); if (t && typeof t === 'object' && t !== G && t[LIVE_KEY]) return t[LIVE_KEY]; } catch (e) {} return null; };
let [liveRanges, liveIters] = (() => {
    const sets = topLiveSets() || [new SSet(), new SSet()];
    Object.defineProperty(G, LIVE_KEY, { value: sets, writable: true, configurable: true });
    return sets;
})();
/* Image loads also run for disconnected elements. decode() observes the selected request,
   waits for native decoding, and rejects if that request changes or its document becomes inactive. */
const imgFailed = new WeakMap(), imgDecodeWaiters = new WeakMap(), imgRequests = new WeakMap();
const imgReferrerPolicies = new SSet(['no-referrer', 'no-referrer-when-downgrade', 'same-origin', 'origin', 'strict-origin', 'origin-when-cross-origin', 'strict-origin-when-cross-origin', 'unsafe-url']);
function imgRequestContext(el) {
    const source = N.imgSrc(el, true);
    const cross = N.attr(el, 'crossorigin'), policy = String(N.attr(el, 'referrerpolicy') || '').toLowerCase();
    return { document: N.ownerDoc(el), source,
        cors: cross === null ? 0 : String(cross).toLowerCase() === 'use-credentials' ? 2 : 1,
        policy: imgReferrerPolicies.has(policy) ? policy : '' };
}
const imgSameRequest = (a, b) => a.document === b.document && a.source === b.source && a.cors === b.cors;
/* A referrer-policy mutation re-runs source selection, but need not replace a
   same-source decode request. Keep it in fetch-context matching, not this guard. */
const imgSameContext = (a, b) => imgSameRequest(a, b) && a.policy === b.policy;
function imgQueueRequest(el) {
    const request = imgRequestContext(el); request.reused = false;
    imgRequests.set(el, request);
    /* Coalesce source mutations before starting native asynchronous work. The
       native cache owns request sharing; this weak record only latches its result. */
    queueMicrotask(() => {
        if (imgRequests.get(el) === request && imgSameContext(request, imgRequestContext(el)))
            request.reused = N.imgRequest(el) === 2;
    });
    return request;
}
function imgSrcChanged(el) {
    if (!el.isConnected) N.imgWatch(el);
    if (!imgFailed.has(el)) EventTarget.prototype.addEventListener.call(el, 'error', () => imgFailed.set(el, N.imgSrc(el)));
    imgFailed.set(el, null);
    imgQueueRequest(el);
    const waiting = imgDecodeWaiters.get(el);
    if (waiting) for (const check of waiting) queueMicrotask(check);
    const s = N.attr(el, 'src');
    if (s && s.trim()) { try { new URL(s, el.baseURI); } catch (e) { setTimeout(() => { if (N.attr(el, 'src') === s) { imgFailed.set(el, s); el.dispatchEvent(new Event('error')); } }, 0); } }
}
function decodeImage(el) {
    return new Promise((resolve, reject) => {
        const src = N.imgSrc(el), available = !!N.imgSize(el), context = imgRequestContext(el);
        let request = imgRequests.get(el);
        if (!request || !imgSameContext(request, context)) request = imgQueueRequest(el);
        const pictureParent = () => { const p = N.parent(el); return p && N.name(p) === 'picture' ? p : null; };
        const picture = pictureParent();
        /* A picture insertion/removal restarts pending source selection even if the selected URL
           is unchanged. An available image or a matching native pending resource keeps its request while that runs. */
        const changed = () => !imgSameRequest(context, imgRequestContext(el)) || (!available && !request.reused && pictureParent() !== picture);
        let settled = false, queued = false;
        const active = () => !!N.ownerDoc(el)?.defaultView && N.fullyActive(el);
        const error = () => new DOMException('The image could not be decoded.', 'EncodingError');
        const cleanup = () => {
            EventTarget.prototype.removeEventListener.call(el, 'load', check);
            EventTarget.prototype.removeEventListener.call(el, 'error', check);
            const waiting = imgDecodeWaiters.get(el);
            if (waiting) { waiting.delete(check); if (!waiting.size) imgDecodeWaiters.delete(el); }
        };
        const finish = ok => {
            if (settled) return;
            settled = true; cleanup();
            if (ok) resolve(undefined); else reject(error());
        };
        const check = () => {
            if (settled || queued) return;
            if (!active() || !src || !src.trim() || changed() || imgFailed.get(el) === src) { finish(false); return; }
            if (N.imgSize(el)) {
                queued = true;
                /* The decode promise resolves in a task, after the update-image microtask. */
                setTimeout(() => { queued = false; finish(active() && !changed() && !!N.imgSize(el)); }, 0);
            } else if (N.imgDone(el)) finish(false);
        };
        let waiting = imgDecodeWaiters.get(el);
        if (!waiting) imgDecodeWaiters.set(el, waiting = new Set());
        waiting.add(check);
        EventTarget.prototype.addEventListener.call(el, 'load', check);
        EventTarget.prototype.addEventListener.call(el, 'error', check);
        if (!el.isConnected) N.imgWatch(el);
        queueMicrotask(check);
    });
}

function linkLiveSets() {
    const sets = topLiveSets();
    if (!sets || sets[0] === liveRanges) return;
    for (const w of liveRanges) sets[0].add(w);
    for (const w of liveIters) sets[1].add(w);
    [liveRanges, liveIters] = sets;
    G[LIVE_KEY] = sets;
}
const rangesEach = (f) => { for (const w of liveRanges) { const r = w.deref(); if (r) f(r); else liveRanges.delete(w); } };
const idxOf = (n) => N.index(n);   /* natively: no wrapper per sibling */
const isCD = (n) => { const t = N.type(n); return t === 3 || t === 4 || t === 7 || t === 8; };
const isTextNode = (n) => { const t = N.type(n); return t === 3 || t === 4; };
const nodeLength = (n) => { const t = N.type(n); if (t === 10) return 0; if (t === 3 || t === 4 || t === 7 || t === 8) return N.text(n).length; let k = 0; for (let c = N.first(n); c; c = N.next(c)) k++; return k; };
const toU32 = (v) => { v = Number(v); if (!Number.isFinite(v)) return 0; v = Math.trunc(v) % 4294967296; return v < 0 ? v + 4294967296 : v; };
const hre = (m = 'The operation would yield an incorrect node tree.') => new DOMException(m, 'HierarchyRequestError');
function rangePreRemove(c) {
    if (liveIters.size) itersPreRemove(c);
    if (!liveRanges.size) return;
    const p = N.parent(c), i = idxOf(c);
    rangesEach(r => {
        if (r._sc instanceof Attr) return;
        if (N.contains(c, r._sc)) { r._sc = p; r._so = i; }
        if (N.contains(c, r._ec)) { r._ec = p; r._eo = i; }
        if (r._sc === p && r._so > i) r._so--;
        if (r._ec === p && r._eo > i) r._eo--;
    });
}
function rangeInserted(p, i, k) { rangesEach(r => { if (r._sc === p && r._so > i) r._so += k; if (r._ec === p && r._eo > i) r._eo += k; }); }
function replaceData(n, o, c, s) {
    const d = N.text(n), len = d.length;
    if (o > len) throw new DOMException(`The offset ${o} is greater than the node's length (${len}).`, 'IndexSizeError');
    if (o + c > len) c = len - o;
    N.setText(n, d.slice(0, o) + s + d.slice(o + c));
    notify('characterData', n, { oldValue: d });
    if (liveRanges.size || liveIters.size) rangesEach(r => {
        if (r._sc === n && r._so > o && r._so <= o + c) r._so = o;
        if (r._ec === n && r._eo > o && r._eo <= o + c) r._eo = o;
        if (r._sc === n && r._so > o + c) r._so += s.length - c;
        if (r._ec === n && r._eo > o + c) r._eo += s.length - c;
    });
}
function preInsertCheck(p, c, ref) {
    const pt = N.type(p), ct = N.type(c);
    if (pt !== 1 && pt !== 9 && pt !== 11) throw hre('Nodes of this type may not contain children.');
    if (N.contains(c, p)) throw hre('The new child element contains the parent.');
    if (ref != null && N.parent(ref) !== p) throw new DOMException('The node before which the new node is to be inserted is not a child of this node.', 'NotFoundError');
    if (ct === 9 || ct === 2) throw hre();
    if ((ct === 3 || ct === 4) && pt === 9) throw hre('Nodes of type \'#text\' may not be inserted inside nodes of type \'#document\'.');
    if (ct === 10 && pt !== 9) throw hre();
    if (pt !== 9) return;
    const els = elKids(p).length, refAfterDoctype = () => { for (let s = ref; s; s = N.next(s)) if (N.type(s) === 10) return true; return false; };
    const one = () => { if (els || (ref && N.type(ref) === 10) || refAfterDoctype()) throw hre(); };
    if (ct === 11) { const k = elKids(c).length; if (k > 1 || kids(c).some(x => isTextNode(x))) throw hre(); if (k === 1) one(); }
    else if (ct === 1) one();
    else if (ct === 10) {
        if (kids(p).some(x => N.type(x) === 10)) throw hre();
        if (ref) { for (let s = N.prev(ref); s; s = N.prev(s)) if (N.type(s) === 1) throw hre(); }
        else if (els) throw hre();
    }
}
/* a NodeList's or HTMLCollection's items: the list (or its proxy target) -> () => current array */
const listItems = new WeakMap();
const listOf = (o, op, iface) => { const f = o !== null && typeof o === 'object' ? listItems.get(o) : undefined; if (!f) throw new TypeError(`Failed to execute '${op}' on '${iface}': Illegal invocation`); return f(); };
const listNeed = (n, op, iface) => { if (n < 1) throw new TypeError(`Failed to execute '${op}' on '${iface}': 1 argument required, but only 0 present.`); };
class NodeList {
    constructor(key = undefined) { illegal(); }
    get length() { return listOf(this, 'length', 'NodeList').length; }
    item(i) { listNeed(arguments.length, 'item', 'NodeList'); const a = listOf(this, 'item', 'NodeList'); i = i >>> 0; return i < a.length ? a[i] : null; }
}
/* WebIDL: a value iterator over an indexed getter uses the Array.prototype functions */
for (const k of ['entries', 'forEach', 'keys', 'values']) Object.defineProperty(NodeList.prototype, k, { value: Array.prototype[k], writable: true, enumerable: true, configurable: true });
Object.defineProperty(NodeList.prototype, Symbol.iterator, { value: Array.prototype.values, writable: true, configurable: true });
class HTMLCollection {
    constructor(key = undefined) { illegal(); }
    get length() { return listOf(this, 'length', 'HTMLCollection').length; }
    item(i) { listNeed(arguments.length, 'item', 'HTMLCollection'); const a = listOf(this, 'item', 'HTMLCollection'); i = i >>> 0; return i < a.length ? a[i] : null; }
    namedItem(k) {
        listNeed(arguments.length, 'namedItem', 'HTMLCollection');
        const a = listOf(this, 'namedItem', 'HTMLCollection'); k = String(k);
        return k ? collFind(a, k) : null;
    }
}
Object.defineProperty(HTMLCollection.prototype, Symbol.iterator, { value: Array.prototype.values, writable: true, configurable: true });
const staticList = (proto, a) => { const o = Object.create(proto); a = Array.from(a); for (let i = 0; i < a.length; i++) Object.defineProperty(o, i, { value: a[i], enumerable: true, configurable: true }); listItems.set(o, () => a); return o; };
const htmlColl = (a) => staticList(HTMLCollection.prototype, a);
const nodeList = (a) => staticList(NodeList.prototype, a);
const liveIdx = (k) => { if (typeof k !== 'string') return -1; const c = k.charCodeAt(0); return c >= 48 && c <= 57 && /^(0|[1-9]\d*)$/.test(k) ? +k : -1; };
const liveHandler = {
    get(t, k, r) { if (k === 'length') return listItems.get(t)().length; const i = liveIdx(k); return i >= 0 ? listItems.get(t)()[i] : Reflect.get(t, k, r); },
    has(t, k) { const i = liveIdx(k); return i >= 0 ? i < listItems.get(t)().length : Reflect.has(t, k); },
    ownKeys(t) { return listItems.get(t)().map((_, i) => String(i)).concat(Reflect.ownKeys(t)); },
    getOwnPropertyDescriptor(t, k) {
        const i = liveIdx(k);
        if (i < 0) return Reflect.getOwnPropertyDescriptor(t, k);
        const a = listItems.get(t)();
        return i < a.length ? { value: a[i], enumerable: true, configurable: true, writable: false } : undefined;
    },
    set(t, k, v, r) { return liveIdx(k) >= 0 || k === 'length' ? false : Reflect.set(t, k, v, r); },
    defineProperty(t, k, d) { return liveIdx(k) >= 0 ? false : Reflect.defineProperty(t, k, d); },
};
/* HTMLCollection named properties: the first element whose id is the name, or an HTML element whose name attribute is */
const collFind = (a, k) => { for (const e of a) if (N.attr(e, 'id') === k || (N.ns(e) === 0 && N.attr(e, 'name') === k)) return e; return null; };
function collNames(a) {
    const out = new SSet();
    for (const e of a) {
        const id = N.attr(e, 'id'); if (id) out.add(id);
        if (N.ns(e) === 0) { const nm = N.attr(e, 'name'); if (nm) out.add(nm); }
    }
    return [...out];
}
/* a named property shows unless an own (expando) property or the prototype chain has that name */
const collNamed = (t, k) => typeof k !== 'string' || !k || liveIdx(k) >= 0 || Object.hasOwn(t, k) || k in Object.getPrototypeOf(t) ? null : collFind(listItems.get(t)(), k);
const collHandler = {
    ...liveHandler,
    get(t, k, r) { if (k === 'length') return listItems.get(t)().length; const i = liveIdx(k); if (i >= 0) return listItems.get(t)()[i]; const e = collNamed(t, k); return e || Reflect.get(t, k, r); },
    has(t, k) { const i = liveIdx(k); return i >= 0 ? i < listItems.get(t)().length : Reflect.has(t, k) || !!collNamed(t, k); },
    ownKeys(t) { const a = listItems.get(t)(), own = Reflect.ownKeys(t); return a.map((_, i) => String(i)).concat(collNames(a).filter(k => liveIdx(k) < 0 && !own.includes(k)), own); },
    getOwnPropertyDescriptor(t, k) {
        const i = liveIdx(k);
        if (i >= 0) { const a = listItems.get(t)(); return i < a.length ? { value: a[i], enumerable: true, configurable: true, writable: false } : undefined; }
        const e = collNamed(t, k);
        return e ? { value: e, enumerable: false, configurable: true, writable: false } : Reflect.getOwnPropertyDescriptor(t, k);
    },
    set(t, k, v, r) { return liveIdx(k) >= 0 || k === 'length' || collNamed(t, k) ? false : Reflect.set(t, k, v, r); },
    defineProperty(t, k, d) { return liveIdx(k) >= 0 || collNamed(t, k) ? false : Reflect.defineProperty(t, k, d); },
    deleteProperty(t, k) { const i = liveIdx(k); if (i >= 0) return i >= listItems.get(t)().length; return collNamed(t, k) ? false : Reflect.deleteProperty(t, k); },
};
/* Live NodeList: recomputed lazily whenever the owner document has mutated. */
function liveList(owner, compute, proto = NodeList.prototype) {
    const t = Object.create(proto);
    let ver = -1, arr = [];
    const cur = () => { const v = N.version(owner); if (v !== ver) { ver = v; arr = compute(); } return arr; };
    const p = new Proxy(t, proto === NodeList.prototype ? liveHandler : collHandler);
    listItems.set(t, cur); listItems.set(p, cur);
    return p;
}
const liveCache = new WeakMap();
function cachedLive(owner, key, compute, proto = NodeList.prototype) {
    let m = liveCache.get(owner);
    if (!m) liveCache.set(owner, m = {});
    return m[key] || (m[key] = liveList(owner, compute, proto));
}
function toNode(x) { return N.isNode(x) ? x : N.textNode(String(x)); }
function insertNode(p, c, ref) {
    if (!N.isNode(c)) throw new TypeError("parameter is not of type 'Node'");
    preInsertCheck(p, c, ref);
    if (ref === c) ref = N.next(c);
    return insertChecked(p, c, ref, false);
}
/* inserts a validated node (a fragment's children) before ref; suppress leaves the record to the caller */
function insertChecked(p, c, ref, suppress) {
    const frag = N.type(c) === 11, added = frag ? kids(c) : [c];
    if (observers.length) {
        if (frag) { if (added.length) notify('childList', c, { removedNodes: added }); }
        else { const op = N.parent(c); if (op) notify('childList', op, { removedNodes: [c], previousSibling: N.prev(c), nextSibling: N.next(c) }); }
    }
    const moved = added.filter(a => N.connected(a));
    if (liveRanges.size || liveIters.size) for (const a of added) if (N.parent(a)) rangePreRemove(a);
    let prev = ref ? N.prev(ref) : N.last(p);
    if (!frag && prev === c) prev = N.prev(c);
    N.insert(p, c, ref == null ? null : ref);
    if (liveRanges.size && added.length) rangeInserted(p, idxOf(added[0]), added.length);
    if (added.length && !suppress) notify('childList', p, { addedNodes: added, previousSibling: prev, nextSibling: ref || null });
    for (const a of moved) ceConnected(a, false, true);
    for (const a of added) ceConnected(a, true);
    return c;
}
/* "convert nodes into a node": one node, or a fragment holding them all (strings become Text) */
function nodesToNode(ns) {
    const list = ns.map(toNode);
    if (list.length === 1) return list[0];
    const f = N.frag();
    for (const n of list) insertNode(f, n, null);
    return f;
}
function replaceCheck(p, node, child) {
    const pt = N.type(p), ct = N.type(node);
    if (pt !== 1 && pt !== 9 && pt !== 11) throw hre('Nodes of this type may not contain children.');
    for (let x = p; x; x = N.parent(x) || (N.type(x) === 11 && N.host(x)) || null) if (x === node) throw hre('The new child element contains the parent.');
    if (N.parent(child) !== p) throw new DOMException('The node to be replaced is not a child of this node.', 'NotFoundError');
    if (ct === 9 || ct === 2) throw hre();
    if ((ct === 3 || ct === 4) && pt === 9) throw hre();
    if (ct === 10 && pt !== 9) throw hre();
    if (pt !== 9) return;
    const others = elKids(p).filter(e => e !== child).length;
    const doctypeAfter = () => { for (let s = N.next(child); s; s = N.next(s)) if (N.type(s) === 10) return true; return false; };
    if (ct === 11) { const k = elKids(node).length; if (k > 1 || kids(node).some(x => isTextNode(x))) throw hre(); if (k === 1 && (others || doctypeAfter())) throw hre(); }
    else if (ct === 1) { if (others || doctypeAfter()) throw hre(); }
    else if (ct === 10) {
        if (kids(p).some(x => N.type(x) === 10 && x !== child)) throw hre();
        for (let s = N.prev(child); s; s = N.prev(s)) if (N.type(s) === 1) throw hre();
    }
}
function movePreCheck(p, node, child) {
    const pt = N.type(p), ct = N.type(node);
    if (p.getRootNode({ composed: true }) !== node.getRootNode({ composed: true })) throw hre('The new parent is not in the same shadow-including tree as the node.');
    for (let n = p; n; n = N.parent(n) || (N.type(n) === 11 && N.host(n)) || null) if (n === node) throw hre('The new child element contains the parent.');
    if (child && N.parent(child) !== p) throw new DOMException('The node before which the new node is to be inserted is not a child of this node.', 'NotFoundError');
    if (ct !== 1 && ct !== 3 && ct !== 4 && ct !== 7 && ct !== 8) throw hre();
    if (ct === 3 && pt === 9) throw hre();
    if (pt === 9 && ct === 1) {
        let after = false;
        for (let s = child; s; s = N.next(s)) if (N.type(s) === 10) after = true;
        if (elKids(p).length || after) throw hre();
    }
}
/* the node keeps its state: custom elements get connectedMoveCallback (else disconnected then connected) */
function moveNode(p, node, child) {
    if (child === node) child = N.next(node);
    const op = N.parent(node);
    if (op) notify('childList', op, { removedNodes: [node], previousSibling: N.prev(node), nextSibling: N.next(node) });
    if (liveRanges.size || liveIters.size) if (op) rangePreRemove(node);
    let prev = child ? N.prev(child) : N.last(p);
    if (prev === node) prev = N.prev(node);
    N.insert(p, node, child);
    if (liveRanges.size) rangeInserted(p, idxOf(node), 1);
    notify('childList', p, { addedNodes: [node], previousSibling: prev, nextSibling: child || null });
    if (N.connected(p)) ceMoved(node);
}
function removeNode(c, suppress) {
    const p = N.parent(c); if (!p) return c;
    const prev = N.prev(c), next = N.next(c), was = N.connected(c);
    if (liveRanges.size || liveIters.size) rangePreRemove(c);
    N.remove(c);
    if (!suppress) notify('childList', p, { removedNodes: [c], previousSibling: prev, nextSibling: next });
    if (was) ceConnected(c, false);
    return c;
}
/* "locate a namespace" and "locate a namespace prefix" (lookupNamespaceURI, lookupPrefix, isDefaultNamespace) */
function locateNS(n, p) {
    const t = n instanceof Attr ? 2 : N.type(n);
    if (t === 1) {
        if (p === 'xml') return XML_NS;
        if (p === 'xmlns') return XMLNS_NS;
        const uri = n.namespaceURI;
        if (uri !== null && n.prefix === p) return uri;
        const l = N.attrsNS(n);
        for (let i = 0; i < l.length; i += 4) {
            if (l[i + 2] !== XMLNS_NS) continue;
            const ap = l[i + 3], ln = ap ? l[i].slice(ap.length + 1) : l[i];
            if ((ap === 'xmlns' && ln === p) || (p === null && ap === null && ln === 'xmlns')) return l[i + 1] === '' ? null : l[i + 1];
        }
        const pe = n.parentElement;
        return pe ? locateNS(pe, p) : null;
    }
    if (t === 9) { const de = n.documentElement; return de ? locateNS(de, p) : null; }
    if (t === 10 || t === 11) return null;
    if (t === 2) return n.ownerElement ? locateNS(n.ownerElement, p) : null;
    const pe = n.parentElement;
    return pe ? locateNS(pe, p) : null;
}
function locatePrefix(el, ns) {
    if (el.namespaceURI === ns && el.prefix !== null) return el.prefix;
    const l = N.attrsNS(el);
    for (let i = 0; i < l.length; i += 4) if (l[i + 3] === 'xmlns' && l[i + 1] === ns) return l[i].slice(6);
    const pe = el.parentElement;
    return pe ? locatePrefix(pe, ns) : null;
}
methods(Node.prototype, {
    get nodeType() { return N.type(this); },
    get nodeName() { const t = N.type(this); return t === 1 ? this.tagName : t === 3 ? '#text' : t === 4 ? '#cdata-section' : t === 7 ? N.name(this) : t === 8 ? '#comment' : t === 9 ? '#document' : t === 11 ? '#document-fragment' : t === 10 ? N.name(this) : ''; },
    get baseURI() { return document.baseURI; },
    get parentNode() { return N.parent(this); },
    get parentElement() { const p = N.parent(this); return p && N.type(p) === 1 ? p : null; },
    get firstChild() { return N.first(this); }, get lastChild() { return N.last(this); },
    get nextSibling() { return N.next(this); }, get previousSibling() { return N.prev(this); },
    get childNodes() { return cachedLive(this, 'childNodes', () => kids(this)); },
    hasChildNodes() { return !!N.first(this); },
    get ownerDocument() { return N.type(this) === 9 ? null : N.ownerDoc(this); },
    get isConnected() { return N.connected(this); },
    get textContent() { const t = N.type(this); return t === 9 || t === 10 ? null : N.text(this); },
    set textContent(v) {
        const t = N.type(this); if (t === 9 || t === 10) return;
        v = v == null ? '' : String(v);
        if (t === 3 || t === 8 || t === 4 || t === 7) { replaceData(this, 0, N.text(this).length, v); return; }
        const removed = kids(this);
        if (liveRanges.size || liveIters.size) for (let i = removed.length - 1; i >= 0; i--) rangePreRemove(removed[i]);
        for (const r of removed) ceConnected(r, false);
        N.setText(this, v);
        if (this.__ass) readopt(this);
        if (removed.length || N.first(this)) notify('childList', this, { removedNodes: removed, addedNodes: kids(this) });
    },
    get nodeValue() { return isCD(this) ? N.text(this) : null; },
    set nodeValue(v) { if (isCD(this)) this.textContent = v; },
    appendChild(c) { return insertNode(this, c, null); },
    insertBefore(c, ref) { return insertNode(this, c, ref === undefined ? null : ref); },
    removeChild(c) {
        const node = N.isNode(c);
        if (!node && !N.attrBranded(c)) throw new TypeError("Failed to execute 'removeChild' on 'Node': parameter 1 is not of type 'Node'.");
        /* Attributes are Nodes with no parent and therefore reach pre-removal's
           NotFoundError, just like any other legitimate non-child node. */
        if (!node || N.parent(c) !== this) throw new DOMException('The node to be removed is not a child of this node.', 'NotFoundError');
        return removeNode(c);
    },
    replaceChild(n, old) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'replaceChild' on 'Node': 2 arguments required, but only ${arguments.length} present.`);
        if (!N.isNode(n)) throw new TypeError("Failed to execute 'replaceChild' on 'Node': parameter 1 is not of type 'Node'.");
        if (!N.isNode(old)) throw new TypeError("Failed to execute 'replaceChild' on 'Node': parameter 2 is not of type 'Node'.");
        replaceCheck(this, n, old);
        let ref = N.next(old), prev = N.prev(old);
        if (ref === n) ref = N.next(n);
        if (prev === n) prev = N.prev(n);
        if (N.type(n) !== 11 && N.parent(n)) removeNode(n);   /* adopting it removes it from its parent */
        const removed = [];
        if (N.parent(old)) { removed.push(old); removeNode(old, true); }
        const added = N.type(n) === 11 ? kids(n) : [n];
        insertChecked(this, n, ref, true);
        notify('childList', this, { addedNodes: added, removedNodes: removed, previousSibling: prev, nextSibling: ref });
        return old;
    },
    cloneNode(deep = false) { const r = N.clone(this, !!deep); cloneControls(this, r, !!deep); if (!N.inert(this)) ceUpgradeTree(r); return r; },
    contains(o) { return o != null && N.isNode(o) && N.contains(this, o); },
});
/* HTML cloning steps: an input's or textarea's dirty value, an input's checkedness and indeterminate state */
const cloneControl = (a, b) => {
    if (a.namespaceURI !== 'http://www.w3.org/1999/xhtml' || (a.localName !== 'input' && a.localName !== 'textarea')) return;
    const v = N.value(a);
    if (v != null) N.setValue(b, v);
    selSet(b, 0, 0, 'none', true);   /* clone value/checkedness, never its selection */
    if (a.localName === 'input') { if (N.checked(a) !== N.checked(b)) N.setChecked(b, N.checked(a)); if (a.__ind) def(b, '__ind', true); }
};
function cloneControls(a, b, deep) {
    if (a.nodeType === 1) cloneControl(a, b);
    if (!deep || !a.firstChild || (a.nodeType !== 1 && a.nodeType !== 9 && a.nodeType !== 11) || !a.querySelector('input,textarea')) return;
    for (let x = a.firstChild, y = b.firstChild; x && y; x = x.nextSibling, y = y.nextSibling) cloneControls(x, y, true);
}
methods(Node.prototype, {
    getRootNode(o = undefined) { let n = this; for (;;) { const p = N.parent(n); if (p) { n = p; continue; } if (o && o.composed && N.type(n) === 11 && N.host(n)) { n = N.host(n); continue; } return n; } },
    isSameNode(o) { return this === o; },
    isEqualNode(o) { if (arguments.length < 1) throw argsErr('isEqualNode', 'Node', 1); return o != null && nodesEqual(this, o); },
    compareDocumentPosition(o) {
        if (o === this) return 0;
        let attr = null;
        if (o instanceof Attr) { attr = o; o = o.ownerElement; }
        else if (!N.isNode(o)) throw new TypeError("Failed to execute 'compareDocumentPosition' on 'Node': parameter 1 is not of type 'Node'.");
        const anc = (n) => { const a = []; for (; n; n = N.parent(n)) a.unshift(n); return a; };
        const a = anc(this), b = o ? anc(o) : [attr];
        /* separate trees: preceding or following by the order their roots were first compared in */
        if (a[0] !== b[0]) return 1 | 32 | (treeSeq(a[0]) < treeSeq(b[0]) ? 4 : 2);
        if (attr && o === this) return 16 | 4;   /* an attribute of this element */
        let i = 0; while (i < a.length && i < b.length && a[i] === b[i]) i++;
        if (i === a.length) return 16 | 4;
        if (i === b.length) return attr ? 2 : 8 | 2;
        for (let c = N.next(a[i]); c; c = N.next(c)) if (c === b[i]) return 4;
        return 2;
    },
    normalize() {
        const texts = [];
        for (let n = N.first(this); n; n = nextIn(n, this)) if (N.type(n) === 3) texts.push(n);
        for (const node of texts) {
            if (!N.contains(this, node)) continue;
            let length = N.text(node).length;
            if (!length) { removeNode(node); continue; }
            let data = ''; for (let x = N.next(node); x && N.type(x) === 3; x = N.next(x)) data += N.text(x);
            if (!data) continue;
            replaceData(node, length, 0, data);
            for (let cur = N.next(node); cur && N.type(cur) === 3; cur = N.next(cur)) {
                if (liveRanges.size || liveIters.size) { const cp = N.parent(cur), ci = idxOf(cur), L = length; rangesEach(r => {
                    if (r._sc === cur) { r._sc = node; r._so += L; } if (r._ec === cur) { r._ec = node; r._eo += L; }
                    if (r._sc === cp && r._so === ci) { r._sc = node; r._so = L; } if (r._ec === cp && r._eo === ci) { r._ec = node; r._eo = L; }
                }); }
                length += N.text(cur).length;
            }
            for (let x = N.next(node); x && N.type(x) === 3;) { const nx = N.next(x); removeNode(x); x = nx; }
        }
    },
    lookupNamespaceURI(p) { return locateNS(this, p == null || p === '' ? null : String(p)); },
    lookupPrefix(ns) { if (ns == null || ns === '') return null; ns = String(ns); const t = N.type(this); const el = t === 1 ? this : t === 9 ? this.documentElement : t === 10 || t === 11 ? null : this.parentElement; return el ? locatePrefix(el, ns) : null; },
    isDefaultNamespace(ns) { return (ns == null || ns === '' ? null : String(ns)) === locateNS(this, null); },
});
/* "equals": same type, the type's own fields, then equal children in order */
function nodesEqual(a, b) {
    const t = N.type(a);
    if (t !== N.type(b)) return false;
    if (t === 10) { if (N.name(a) !== N.name(b) || a.publicId !== b.publicId || a.systemId !== b.systemId) return false; }
    else if (t === 1) {
        if (a.namespaceURI !== b.namespaceURI || a.prefix !== b.prefix || a.localName !== b.localName) return false;
        const x = N.attrsNS(a), y = N.attrsNS(b);
        if (x.length !== y.length) return false;
        for (let i = 0; i < x.length; i += 4) {
            const ln = x[i + 3] ? x[i].slice(x[i + 3].length + 1) : x[i];
            if (N.attrNS(b, x[i + 2], ln) !== x[i + 1]) return false;
        }
    } else if (t === 7) { if (N.name(a) !== N.name(b) || N.text(a) !== N.text(b)) return false; }
    else if (t === 3 || t === 4 || t === 8) { if (N.text(a) !== N.text(b)) return false; }
    let c = N.first(a), d = N.first(b);
    for (; c && d; c = N.next(c), d = N.next(d)) if (!nodesEqual(c, d)) return false;
    return !c && !d;
}
const argsErr = (m, o, n) => new TypeError(`Failed to execute '${m}' on '${o instanceof Document ? 'Document' : o instanceof DocumentFragment ? 'DocumentFragment' : 'Element'}': ${n} argument required, but only 0 present.`);
/* "list of elements with qualified name": a plain lowercase name takes the native selector path (HTML elements match
   case-insensitively, others only exactly); anything else walks the tree comparing qualified names */
const qnameOf = e => { const p = e.prefix, l = e.localName; return p ? p + ':' + l : l; };
function byTagName(root, t) {
    if (t === '*') return N.query(root, '*', true);
    const html = (root.ownerDocument || root) instanceof Document && N.ns(root.ownerDocument || root) !== 3;
    if (html && /^[a-z][a-z0-9-]*$/.test(t)) return N.query(root, t, true).filter(e => N.ns(e) === 0 ? !e.prefix : qnameOf(e) === t);
    const lt = asciiLower(t), r = [];
    for (let e = nextIn(root, root); e; e = nextIn(e, root)) if (N.type(e) === 1 && qnameOf(e) === (html && N.ns(e) === 0 ? lt : t)) r.push(e);
    return r;
}
function byTagNameNS(root, ns, l) {
    const r = [];
    for (let e = nextIn(root, root); e; e = nextIn(e, root))
        if (N.type(e) === 1 && (ns === '*' || e.namespaceURI === ns) && (l === '*' || e.localName === l)) r.push(e);
    return r;
}
const ParentNode = {
    get children() { return cachedLive(this, 'children', () => elKids(this), HTMLCollection.prototype); },
    get childElementCount() { return elKids(this).length; },
    get firstElementChild() { for (let c = N.first(this); c; c = N.next(c)) if (N.type(c) === 1) return c; return null; },
    get lastElementChild() { for (let c = N.last(this); c; c = N.prev(c)) if (N.type(c) === 1) return c; return null; },
    append(...ns) { insertNode(this, nodesToNode(ns), null); },
    prepend(...ns) { insertNode(this, nodesToNode(ns), N.first(this)); },
    replaceChildren(...ns) {
        const node = nodesToNode(ns);
        preInsertCheck(this, node, null);
        const removed = kids(this), added = N.type(node) === 11 ? kids(node) : [node];
        for (const c of removed) removeNode(c, true);
        insertChecked(this, node, null, true);
        if (added.length || removed.length) notify('childList', this, { addedNodes: added, removedNodes: removed, previousSibling: null, nextSibling: null });
        if (this.__ass) readopt(this);
    },
    /* a state-preserving move within one shadow-including tree */
    moveBefore(node, child) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'moveBefore': 2 arguments required, but only ${arguments.length} present.`);
        if (!N.isNode(node)) throw new TypeError("Failed to execute 'moveBefore': parameter 1 is not of type 'Node'.");
        if (child !== null && !N.isNode(child)) throw new TypeError("Failed to execute 'moveBefore': parameter 2 is not of type 'Node'.");
        movePreCheck(this, node, child);
        moveNode(this, node, child);
    },
    querySelector(s) { if (!arguments.length) throw argsErr('querySelector', this, 1); return N.query(this, cssPre(String(s)), false); },
    querySelectorAll(s) { if (!arguments.length) throw argsErr('querySelectorAll', this, 1); return nodeList(N.query(this, cssPre(String(s)), true)); },
    getElementsByTagName(t) { t = String(t); return liveList(this, () => byTagName(this, t), HTMLCollection.prototype); },
    getElementsByTagNameNS(ns, l) { ns = ns == null || ns === '' ? null : String(ns); l = String(l); return liveList(this, () => byTagNameNS(this, ns, l), HTMLCollection.prototype); },
    getElementsByClassName(c) { const p = String(c).split(/[\t\n\f\r ]+/).filter(Boolean); const sel = p.map(x => '.' + CSS.escape(x)).join(''); return liveList(this, () => sel ? N.query(this, sel, true) : [], HTMLCollection.prototype); },
};
const ChildNode = {
    /* the insertion point is the nearest sibling not among the arguments */
    before(...ns) {
        const p = N.parent(this); if (!p) return;
        let prev = N.prev(this);
        while (prev && ns.includes(prev)) prev = N.prev(prev);
        const node = nodesToNode(ns);
        insertNode(p, node, prev ? N.next(prev) : N.first(p));
    },
    after(...ns) {
        const p = N.parent(this); if (!p) return;
        let next = N.next(this);
        while (next && ns.includes(next)) next = N.next(next);
        insertNode(p, nodesToNode(ns), next);
    },
    replaceWith(...ns) {
        const p = N.parent(this); if (!p) return;
        let next = N.next(this);
        while (next && ns.includes(next)) next = N.next(next);
        const node = nodesToNode(ns);
        if (N.parent(this) === p) p.replaceChild(node, this); else insertNode(p, node, next);
    },
    remove() { removeNode(this); },
    get nextElementSibling() { for (let c = N.next(this); c; c = N.next(c)) if (N.type(c) === 1) return c; return null; },
    get previousElementSibling() { for (let c = N.prev(this); c; c = N.prev(c)) if (N.type(c) === 1) return c; return null; },
};
class CharacterData extends Node {}
methods(CharacterData.prototype, ChildNode);
methods(CharacterData.prototype, {
    get data() { return N.text(this); }, set data(v) { replaceData(this, 0, N.text(this).length, v == null ? '' : String(v)); },
    get length() { return N.text(this).length; },
    appendData(s) { replaceData(this, N.text(this).length, 0, String(s)); },
    substringData(o, c) { o = toU32(o); c = toU32(c); const d = N.text(this); if (o > d.length) throw new DOMException(`The offset ${o} is greater than the node's length (${d.length}).`, 'IndexSizeError'); return d.substring(o, o + c); },
    insertData(o, s) { replaceData(this, toU32(o), 0, String(s)); },
    deleteData(o, c) { replaceData(this, toU32(o), toU32(c), ''); },
    replaceData(o, c, s) { replaceData(this, toU32(o), toU32(c), String(s)); },
});
class Text extends CharacterData { constructor(s = '') { return N.textNode(String(s)); } }
methods(Text.prototype, {
    /* the data of this node and its contiguous Text siblings */
    get wholeText() {
        if (!N.isNode(this) || (N.type(this) !== 3 && N.type(this) !== 4)) throw new TypeError("Failed to read the 'wholeText' property from 'Text': Illegal invocation");
        let a = this, t = '';
        for (let p = N.prev(a); p && (N.type(p) === 3 || N.type(p) === 4); p = N.prev(p)) a = p;
        for (let n = a; n && (N.type(n) === 3 || N.type(n) === 4); n = N.next(n)) t += N.text(n);
        return t;
    },
    splitText(o) {
        o = toU32(o); const d = N.text(this), len = d.length;
        if (o > len) throw new DOMException(`The offset ${o} is larger than the Text node's length.`, 'IndexSizeError');
        const doc = N.ownerDoc(this), t = N.type(this) === 4 ? N.cdata(d.slice(o), doc) : N.textNode(d.slice(o), doc), p = N.parent(this);
        if (p) {
            insertNode(p, t, N.next(this));
            if (liveRanges.size || liveIters.size) { const i = idxOf(this) + 1; rangesEach(r => {
                if (r._sc === this && r._so > o) { r._sc = t; r._so -= o; } if (r._ec === this && r._eo > o) { r._ec = t; r._eo -= o; }
                if (r._sc === p && r._so === i) r._so++; if (r._ec === p && r._eo === i) r._eo++;
            }); }
        }
        replaceData(this, o, len - o, '');
        return t;
    },
    get assignedSlot() { return slotOpenFor(this); },
});
class CDATASection extends Text { constructor() { illegal(); } }
class Comment extends CharacterData { constructor(s = '') { return N.comment(String(s)); } }
class DocumentType extends Node {}
methods(DocumentType.prototype, ChildNode);
methods(DocumentType.prototype, { get name() { return N.name(this); }, get publicId() { return N.attr(this, 'publicId') || ''; }, get systemId() { return N.attr(this, 'systemId') || ''; } });
class DocumentFragment extends Node { constructor() { return N.frag(); } }
methods(DocumentFragment.prototype, ParentNode);
methods(DocumentFragment.prototype, { getElementById(id) { return N.query(this, '#' + CSS.escape(String(id)), false); } });
class ShadowRoot extends DocumentFragment { constructor() { illegal(); } }
methods(ShadowRoot.prototype, {
    getAnimations() { return []; },
    get host() { return N.host(this); }, get mode() { return N.srFlags(this) & 1 ? 'closed' : 'open'; },
    get innerHTML() { return N.html(this, false); }, set innerHTML(v) { setInner(this, ttString(TrustedHTML, v == null ? '' : v, 'ShadowRoot innerHTML')); },
    get activeElement() { return null; }, get delegatesFocus() { return !!(N.srFlags(this) & 2); },
    get clonable() { return !!(N.srFlags(this) & 8); }, get serializable() { return !!(N.srFlags(this) & 4); },
    get slotAssignment() { return N.srFlags(this) & 16 ? 'manual' : 'named'; },
    get referenceTarget() { return N.srRef(this); }, set referenceTarget(v) { N.srRef(this, v == null ? null : String(v)); },
    get adoptedStyleSheets() { return adoptedGet(this); }, set adoptedStyleSheets(v) { adoptSheets(this, v); },
    get styleSheets() { return scopeSheets(this); }, get fullscreenElement() { return fsCurrent(this); },
    get onslotchange() { return this.__onslotchange || null; },
    set onslotchange(v) { def(this, '__onslotchange', typeof v === 'function' || (v !== null && typeof v === 'object') ? v : null); },
});

const treeSeqs = new WeakMap(); let treeSeqN = 0;
function treeSeq(root) { let v = treeSeqs.get(root); if (!v) treeSeqs.set(root, v = ++treeSeqN); return v; }
/* Attr nodes: an element's attribute is the same object for as long as it stays on the element (keyed by namespace and
   local name); a detached one (createAttribute, removed or replaced) keeps its own value. */
const attrNodes = new WeakMap();
class Attr extends Node {
    get name() { return this._pfx ? this._pfx + ':' + this._ln : this._ln; }
    get localName() { return this._ln; } get namespaceURI() { return this._ns; } get prefix() { return this._pfx; }
    get specified() { return true; }
    get value() { return this._el ? N.attrNS(this._el, this._ns, this._ln) ?? this._v : this._v; }
    set value(v) {
        v = String(v);
        if (this._el) v = ttAttr(this._el, this._ns, this._ln, v);   /* the default policy may detach it */
        if (this._el) attrChange(this._el, this._ns, this._pfx, this._ln, v); else this._v = v;
    }
    get nodeValue() { return this.value; } set nodeValue(v) { this.value = v == null ? '' : v; }
    get textContent() { return this.value; } set textContent(v) { this.value = v == null ? '' : v; }
    get ownerElement() { return this._el; } get nodeType() { return 2; } get nodeName() { return this.name; }
    get ownerDocument() { return this._el ? this._el.ownerDocument : this._doc; }
    get parentNode() { return null; } get parentElement() { return null; } get isConnected() { return false; }
    get firstChild() { return null; } get lastChild() { return null; } get previousSibling() { return null; } get nextSibling() { return null; }
    get childNodes() { return nodeList([]); } hasChildNodes() { return false; }
    get baseURI() { return (this._el || this.ownerDocument || document).baseURI; }
    getRootNode() { return this; } contains(o) { return o === this; }
    cloneNode() { return mkAttr(null, this._ns, this._pfx, this._ln, this.value, this.ownerDocument); }
    isEqualNode(o) { return o instanceof Attr && o._ns === this._ns && o._ln === this._ln && o.value === this.value; }
    isSameNode(o) { return o === this; }
    lookupPrefix(ns) { return this._el ? this._el.lookupPrefix(ns) : null; }
    lookupNamespaceURI(p) { return this._el ? this._el.lookupNamespaceURI(p) : null; }
    isDefaultNamespace(ns) { return this._el ? this._el.isDefaultNamespace(ns) : ns == null || ns === ''; }
}
function mkAttr(el, ns, pfx, ln, v, doc) {
    const a = Object.create(Attr.prototype); N.attrBrand(a);
    def(a, '_el', el); def(a, '_ns', ns); def(a, '_pfx', pfx); def(a, '_ln', ln); def(a, '_v', v); def(a, '_doc', doc || null);
    return a;
}
const attrKey = (ns, ln) => (ns ?? '') + '\u0000' + ln;
/* the attribute node of el's attribute (ns, ln), made on first use */
function attrNode(el, ns, pfx, ln) {
    let m = attrNodes.get(el); if (!m) attrNodes.set(el, m = new SMap());
    const k = attrKey(ns, ln);
    let a = m.get(k);
    if (!a || a._el !== el) m.set(k, a = mkAttr(el, ns, pfx, ln, '', null));
    a._pfx = pfx;
    return a;
}
/* el's i-th attribute from an N.attrsNS list as an Attr */
const attrAt = (el, l, i) => { const q = l[4 * i], p = l[4 * i + 3]; return attrNode(el, l[4 * i + 2], p, p ? q.slice(p.length + 1) : q); };
const attrByQName = (el, q) => { const l = N.attrsNS(el); for (let i = 0; 4 * i < l.length; i++) if (l[4 * i] === q) return attrAt(el, l, i); return null; };
const attrByNS = (el, ns, ln) => { if (N.attrNS(el, ns, ln) == null) return null; const l = N.attrsNS(el); for (let i = 0; 4 * i < l.length; i++) { const p = l[4 * i + 3]; if (l[4 * i + 2] === ns && (p ? l[4 * i].slice(p.length + 1) : l[4 * i]) === ln) return attrAt(el, l, i); } return null; };
/* an attribute leaving el: its node (if any) keeps the value it had */
function attrDetach(el, ns, ln, v) {
    const m = attrNodes.get(el), k = attrKey(ns, ln), a = m && m.get(k);
    if (a && a._el === el) { a._el = null; a._v = v; m.delete(k); }
}
/* "change"/"append an attribute": sets el's attribute (ns, ln) to v and queues the mutation record and reaction */
function attrChange(el, ns, pfx, ln, v) {
    const old = N.attrNS(el, ns, ln);
    N.setAttrNS(el, ns, pfx, ln, v);   /* by namespace: a namespaced attribute with the same qualified name stays */
    if (!ns && (ln === 'src' || ln === 'srcset' || ln === 'sizes' || ln === 'crossorigin' || ln === 'referrerpolicy') && el instanceof H.HTMLImageElement) imgSrcChanged(el);
    notify('attributes', el, { attributeName: ln, attributeNamespace: ns, oldValue: old });
    ceAttr(el, ln, old, v, ns);
}
function attrRemove(el, ns, ln) {
    const old = N.attrNS(el, ns, ln);
    if (old == null) return null;
    attrDetach(el, ns, ln, old);
    N.rmAttrNS(el, ns, ln);
    if (!ns && (ln === 'src' || ln === 'srcset' || ln === 'sizes' || ln === 'crossorigin' || ln === 'referrerpolicy') && el instanceof H.HTMLImageElement) imgSrcChanged(el);
    notify('attributes', el, { attributeName: ln, attributeNamespace: ns, oldValue: old });
    ceAttr(el, ln, old, null, ns);
    return old;
}
/* "validate and extract" for createElementNS, createAttributeNS and setAttributeNS */
const XML_NS = 'http://www.w3.org/XML/1998/namespace', XMLNS_NS = 'http://www.w3.org/2000/xmlns/';
const validAttrName = n => n.length > 0 && !/[\t\n\f\r \/=>\x00]/.test(n);
function nsExtract(ns, q, attr) {
    ns = ns == null || ns === '' ? null : String(ns); q = String(q);
    const i = q.indexOf(':'), pfx = i >= 0 ? q.slice(0, i) : null, l = i >= 0 ? q.slice(i + 1) : q;
    if ((pfx !== null && !/^[^\t\n\f\r \/>\x00]+$/.test(pfx)) || !(attr ? validAttrName(l) : validLocalName(l)))
        throw new DOMException(`The qualified name provided ('${q}') contains the invalid name-start character.`, 'InvalidCharacterError');
    if ((pfx !== null && ns === null) || (pfx === 'xml' && ns !== XML_NS) || ((q === 'xmlns' || pfx === 'xmlns') !== (ns === XMLNS_NS)))
        throw new DOMException(`The namespace configuration for '${q}' is invalid.`, 'NamespaceError');
    return [ns, pfx, l];
}
const NNM_EL = Symbol('element');   /* not a string key: a map's own property names are its attributes */
class NamedNodeMap {
    constructor() { illegal(); }
    get length() { return N.attrsNS(this[NNM_EL]).length / 4; }
    item(i) { i = i >>> 0; const l = N.attrsNS(this[NNM_EL]); return 4 * i < l.length ? attrAt(this[NNM_EL], l, i) : null; }
    getNamedItem(n) { return attrByQName(this[NNM_EL], lcName(this[NNM_EL], n)); }
    getNamedItemNS(ns, n) { return attrByNS(this[NNM_EL], ns == null || ns === '' ? null : String(ns), String(n)); }
    setNamedItem(a) { return this[NNM_EL].setAttributeNode(a); }
    setNamedItemNS(a) { return this[NNM_EL].setAttributeNode(a); }
    removeNamedItem(n) { const a = this.getNamedItem(n); if (!a) throw new DOMException(`Failed to execute 'removeNamedItem' on 'NamedNodeMap': No item with name '${n}' was found.`, 'NotFoundError'); return this[NNM_EL].removeAttributeNode(a); }
    removeNamedItemNS(ns, n) { const a = this.getNamedItemNS(ns, n); if (!a) throw new DOMException(`Failed to execute 'removeNamedItemNS' on 'NamedNodeMap': No item with that name was found.`, 'NotFoundError'); return this[NNM_EL].removeAttributeNode(a); }
    *[Symbol.iterator]() { for (let i = 0; i < this.length; i++) yield this.item(i); }
}
/* indexed and named access (attributes[0], attributes.id); in HTML documents lowercase names of HTML elements only */
const nnmNames = t => { const l = N.attrsNS(t[NNM_EL]), r = []; for (let i = 0; i < l.length; i += 4) { const q = l[i]; if (!r.includes(q) && !(N.ns(t[NNM_EL]) === 0 && /[A-Z]/.test(q))) r.push(q); } return r; };
const nnmHandler = {
    get(t, k, r) {
        if (typeof k !== 'string') return Reflect.get(t, k, r);
        const i = liveIdx(k); if (i >= 0) return t.item(i) ?? undefined;
        if (k in t) return Reflect.get(t, k, r);
        return attrByQName(t[NNM_EL], k) ?? undefined;
    },
    has(t, k) { if (typeof k !== 'string') return Reflect.has(t, k); const i = liveIdx(k); if (i >= 0) return i < t.length; return Reflect.has(t, k) || nnmNames(t).includes(k); },
    ownKeys(t) { return [...Array(t.length).keys()].map(String).concat(nnmNames(t), Reflect.ownKeys(t)); },
    getOwnPropertyDescriptor(t, k) {
        if (typeof k !== 'string') return Reflect.getOwnPropertyDescriptor(t, k);
        const i = liveIdx(k); if (i >= 0) return i < t.length ? { value: t.item(i), enumerable: true, configurable: true, writable: false } : undefined;
        const own = Reflect.getOwnPropertyDescriptor(t, k); if (own || k in t) return own;
        return nnmNames(t).includes(k) ? { value: attrByQName(t[NNM_EL], k), enumerable: false, configurable: true, writable: false } : undefined;
    },
    set(t, k, v, r) { if (typeof k === 'string' && (liveIdx(k) >= 0 || (!(k in t) && attrByQName(t[NNM_EL], k)))) return false; return Reflect.set(t, k, v, r); },
    defineProperty(t, k, d) { if (typeof k === 'string' && liveIdx(k) >= 0) return false; return Reflect.defineProperty(t, k, d); },
    deleteProperty(t, k) { if (typeof k === 'string' && (liveIdx(k) >= 0 ? liveIdx(k) < t.length : attrByQName(t[NNM_EL], k))) return false; return Reflect.deleteProperty(t, k); },
};
const nnmOf = el => { const o = Object.create(NamedNodeMap.prototype); def(o, NNM_EL, el); return new Proxy(o, nnmHandler); };
const tokHandler = {
    get(t, k, r) { const i = liveIdx(k); return i >= 0 ? t._get()[i] : Reflect.get(t, k, r); },
    has(t, k) { const i = liveIdx(k); return i >= 0 ? i < t._get().length : Reflect.has(t, k); },
    ownKeys(t) { return t._get().map((_, i) => String(i)).concat(Reflect.ownKeys(t)); },
    getOwnPropertyDescriptor(t, k) { const i = liveIdx(k); if (i < 0) return Reflect.getOwnPropertyDescriptor(t, k); const a = t._get(); return i < a.length ? { value: a[i], enumerable: true, configurable: true, writable: false } : undefined; },
    set(t, k, v, r) { return liveIdx(k) >= 0 ? false : Reflect.set(t, k, v, r); },
};
const TOK_KEY = Symbol('DOMTokenList'), tokLists = new WeakSet();
const tokThis = (o, op) => { if (!tokLists.has(o)) throw new TypeError(`Failed to execute '${op}' on 'DOMTokenList': Illegal invocation`); return o; };
const tokNeed = (n, want, op) => { if (n < want) throw new TypeError(`Failed to execute '${op}' on 'DOMTokenList': ${want} argument${want > 1 ? 's' : ''} required, but only ${n} present.`); };
class DOMTokenList {
    constructor(...a) { if (a[0] !== TOK_KEY) illegal(); def(this, '_el', a[1]); def(this, '_attr', a[2]); const p = new Proxy(this, tokHandler); tokLists.add(p); return p; }
    _get() { const v = N.attr(this._el, this._attr); return v ? [...new SSet(v.split(/[\t\n\f\r ]+/).filter(Boolean))] : []; }
    _set(a) { if (!a.length && N.attr(this._el, this._attr) == null) return; this._el.setAttribute(this._attr, a.join(' ')); }
    static _empty(t) { if (!t) throw new DOMException('The token provided must not be empty.', 'SyntaxError'); }
    static _ws(t) { if (/[\t\n\f\r ]/.test(t)) throw new DOMException(`The token provided ('${t}') contains HTML space characters, which are not valid in tokens.`, 'InvalidCharacterError'); }
    static _chk(t) { t = String(t); DOMTokenList._empty(t); DOMTokenList._ws(t); return t; }
    get length() { return this._get().length; }
    get value() { return N.attr(this._el, this._attr) || ''; } set value(v) { this._el.setAttribute(this._attr, v); }
    item(i) { tokNeed(arguments.length, 1, 'item'); i = i >>> 0; return tokThis(this, 'item')._get()[i] ?? null; }
    contains(t) { tokNeed(arguments.length, 1, 'contains'); return tokThis(this, 'contains')._get().includes(String(t)); }
    add(...ts) { ts = ts.map(DOMTokenList._chk); const a = this._get(); for (const t of ts) if (!a.includes(t)) a.push(t); this._set(a); }
    remove(...ts) { ts = ts.map(DOMTokenList._chk); this._set(this._get().filter(x => !ts.includes(x))); }
    toggle(t, force = undefined) {
        tokNeed(arguments.length, 1, 'toggle'); t = DOMTokenList._chk(t); const a = this._get();
        if (a.includes(t)) { if (force === undefined || !force) { this._set(a.filter(x => x !== t)); return false; } return true; }
        if (force === undefined || force) { a.push(t); this._set(a); return true; }
        return false;
    }
    replace(t, n) {
        tokNeed(arguments.length, 2, 'replace'); t = String(t); n = String(n); DOMTokenList._empty(t); DOMTokenList._empty(n); DOMTokenList._ws(t); DOMTokenList._ws(n);
        const a = this._get(); if (!a.includes(t)) return false;
        const out = []; for (const x of a) { if (x === t || x === n) { if (!out.includes(n)) out.push(n); } else out.push(x); }
        this._set(out); return true;
    }
    supports(t) { if (this._attr === 'class') throw new TypeError(`DOMTokenList has no supported tokens.`); return true; }
    toString() { return tokThis(this, 'toString').value; }
}
for (const k of ['entries', 'forEach', 'keys', 'values']) Object.defineProperty(DOMTokenList.prototype, k, { value: Array.prototype[k], writable: true, enumerable: true, configurable: true });
Object.defineProperty(DOMTokenList.prototype, Symbol.iterator, { value: Array.prototype.values, writable: true, configurable: true });
