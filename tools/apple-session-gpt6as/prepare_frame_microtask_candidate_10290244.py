from pathlib import Path
import hashlib,json,difflib
R=Path.cwd(); O=R/'output/session-10290244/frame-microtask-candidate'; O.mkdir(parents=True,exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
expected={'src/js/frames.cc':'971785339ea2935a0f4c4d3a6899e2a4134dab6b5ea94cbff232afecf22a30a7','src/js/jsnative.cc':'7fc541e21e554b4b351a21c4a8126c6885d83554b41d7f7bb3b9c060fd57a069','src/js/prelude/08-window.js':'8d01dc87f8640939e227ef71178673f172c20b0cc49750fce98d09f5d5fb9fe9'}
base={p:(R/p).read_bytes() for p in expected}
for p,b in base.items():
    assert sha(b)==expected[p],(p,sha(b))
text={p:b.decode() for p,b in base.items()}
old='FCTX; int i = a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(-1);'
assert text['src/js/frames.cc'].count(old)==1
text['src/js/frames.cc']=text['src/js/frames.cc'].replace(old,'FCTX; frames_scan(c); int i = a[0]->Int32Value(iso->GetCurrentContext()).FromMaybe(-1);')
helper=r'''// Invoke a Web IDL callback in its associated realm and report there.
// Do not use jcall: it settles the microtask queue recursively.
FN(runMicrotask) {
    CTX;
    if (a.Length() < 1 || !a[0]->IsFunction()) {
        iso->ThrowException(v8::Exception::TypeError(jstr(iso, "queueMicrotask callback must be callable")));
        return;
    }
    auto fn = a[0].As<v8::Function>();
    v8::Local<v8::Context> callback_context;
    if (!fn->GetCreationContext(iso).ToLocal(&callback_context)) callback_context = ctx;
    v8::Context::Scope callback_scope(callback_context);
    JsCtx *owner = callback_context->GetNumberOfEmbedderDataFields() >= 2 ? jctx(iso) : c;
    v8::TryCatch caught(iso);
    v8::Local<v8::Value> ignored;
    if (!fn->Call(callback_context, v8::Undefined(iso), 0, nullptr).ToLocal(&ignored)) {
        if (caught.HasCaught() && caught.CanContinue()) {
            auto exception = caught.Exception();
            auto message = caught.Message();
            caught.Reset();
            jreport(owner, exception, message);
        } else if (caught.HasCaught()) {
            (void)caught.ReThrow();
        }
    }
}

'''
anchor='static v8::Local<v8::Value> nstr(v8::Isolate *iso, const char *s)'
p='src/js/jsnative.cc'; assert text[p].count(anchor)==1
text[p]=text[p].replace(anchor,helper+anchor,1)
reg='REG(isNode); REG(domStateGet); REG(domStateSet); REG(version);'
assert text[p].count(reg)==1
text[p]=text[p].replace(reg,reg+' REG(runMicrotask);',1)
p='src/js/prelude/08-window.js'
old="const queueMicrotask = (fn) => { if (typeof fn !== 'function') throw new TypeError('queueMicrotask callback must be callable'); Promise.resolve().then(() => { try { fn(); } catch (e) { report(e); } }); };"
new="const queueMicrotask = (fn) => { if (typeof fn !== 'function') throw new TypeError('queueMicrotask callback must be callable'); Promise.resolve().then(() => { N.runMicrotask(fn); }); };"
assert text[p].count(old)==1
text[p]=text[p].replace(old,new,1)
old="get length() { return this._m.size; } key(i) { return [...this._m.keys()][i] ?? null; }"
new="get length() { return this._m.size; } key(i) { if (!arguments.length) throw new TypeError('Storage.key requires an index'); return [...this._m.keys()][(+i) >>> 0] ?? null; }"
assert text[p].count(old)==1
text[p]=text[p].replace(old,new,1)
manifest={'files':[]}; patches=[]
for p,s in text.items():
    b=s.encode(); dest=O/'candidate'/p; dest.parent.mkdir(parents=True,exist_ok=True); dest.write_bytes(b)
    orig=O/'original'/p; orig.parent.mkdir(parents=True,exist_ok=True); orig.write_bytes(base[p])
    manifest['files'].append({'path':p,'base_sha256':sha(base[p]),'candidate_sha256':sha(b)})
    patches.extend(difflib.unified_diff(base[p].decode().splitlines(True),s.splitlines(True),fromfile='a/'+p,tofile='b/'+p))
patch=''.join(patches).encode();(O/'production.patch').write_bytes(patch);manifest['patch_sha256']=sha(patch)
(O/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
