/* Real V8/DOM form default actions with a recording host: no network or UI stubs. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstring>
#include <string>
extern "C" void font_init(void);

static int checks, failures;
#define CHECK(name, condition) do { bool ok_ = (condition); ++checks; printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); if (!ok_) ++failures; } while (0)
static bool value(JsCtx *c, const char *source) {
    JS_ENTER(c);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    bool ok = v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) && script->Run(ctx).ToLocal(&result);
    if (!ok && caught.HasCaught()) fprintf(stderr, "JS exception: %s\n", jcstr(iso, caught.Exception()).c_str());
    iso->PerformMicrotaskCheckpoint();
    const bool passed = ok && result->BooleanValue(iso);
    /* Form navigation is a later task; observe host delivery after this script. */
    js_release_pins(); js_tick(c);
    return passed;
}
struct Fixture {
    Document *document = nullptr;
    JsCtx *context = nullptr;
    int get_calls = 0, post_calls = 0;
    std::string last_url, last_body, last_type;
    static void navigate(void *opaque, const char *url) {
        auto *s = static_cast<Fixture *>(opaque); ++s->get_calls; s->last_url = url;
    }
    static void post(void *opaque, const char *url, const char *body, size_t n, const char *type) {
        auto *s = static_cast<Fixture *>(opaque); ++s->post_calls; s->last_url = url;
        s->last_body.assign(body, n); s->last_type = type;
    }
    Fixture(const char *body = "<dialog id=d><form id=f method=dialog><input name=q value=hello><button id=yes name=choice value=accepted><span id=label>Continue</span></button><button id=no value=cancel>Cancel</button></form></dialog>") {
        document = doc_new("https://forms.test/start");
        std::string html = std::string("<!doctype html><html><body>") + body + "</body></html>";
        html_parse(document, html.c_str(), html.size());
        JsHost host{}; host.ud = this; host.navigate = navigate; host.navigate_post = post;
        context = js_new(document, &host);
        value(context, "globalThis.d=document.getElementById('d');globalThis.f=document.getElementById('f');"
              "globalThis.yes=document.getElementById('yes');globalThis.no=document.getElementById('no');"
              "globalThis.dialogCloseCount=0;globalThis.submits=0;globalThis.lastSubmitter=null;"
              "if(d)d.addEventListener('close',()=>dialogCloseCount++);"
              "if(f)f.addEventListener('submit',e=>{submits++;lastSubmitter=e.submitter;});true");
    }
    bool no_navigation() const { return get_calls == 0 && post_calls == 0; }
    ~Fixture() { js_release_pins(); js_free(context); doc_free(document); }
};

int main(int argc, char **argv) {
    (void)argc; dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    {
        Fixture f;
        CHECK("method reflection recognizes dialog case-insensitively", value(f.context,
            "f.setAttribute('method','DiAlOg');f.method==='dialog'"));
        CHECK("method reflection keeps valid get/post and invalid/missing defaults", value(f.context,
            "f.method='PoSt';let ok=f.method==='post';f.method='GET';ok&&=f.method==='get';"
            "f.method='invalid';ok&&=f.method==='get';f.removeAttribute('method');ok&&=f.method==='get';ok"));
        CHECK("dialog returnValue starts empty and converts assigned values", value(f.context,
            "let initialReturnValueEmpty=d.returnValue==='';d.returnValue=42;initialReturnValueEmpty&&d.returnValue==='42'"));
    }
    for (const char *button : {"yes", "no"}) {
        Fixture f;
        std::string script = std::string("d.showModal();") + button + ".click();"
            "!d.open && dialogCloseCount===1 && submits===1 && lastSubmitter===" + button + " && d.returnValue===" + button + ".value";
        CHECK("consent button closes exactly once with its submitter value", value(f.context, script.c_str()));
        CHECK("consent button never reaches navigation host", f.no_navigation());
    }
    {
        Fixture f;
        CHECK("nested button content uses the same dialog default action", value(f.context,
            "d.showModal();document.getElementById('label').click();!d.open&&dialogCloseCount===1&&d.returnValue==='accepted'&&lastSubmitter===yes"));
        CHECK("nested button content does not navigate", f.no_navigation());
    }
    {
        Fixture f;
        CHECK("requestSubmit uses its actual button and closes without navigating", value(f.context,
            "d.showModal();f.requestSubmit(no);!d.open&&dialogCloseCount===1&&submits===1&&lastSubmitter===no&&d.returnValue==='cancel'") && f.no_navigation());
        CHECK("requestSubmit without submitter preserves existing returnValue", value(f.context,
            "d.returnValue='kept';d.showModal();f.requestSubmit();!d.open&&dialogCloseCount===2&&submits===2&&lastSubmitter===null&&d.returnValue==='kept'") && f.no_navigation());
        CHECK("submit bypasses submit event and preserves existing returnValue", value(f.context,
            "d.showModal();f.submit();!d.open&&dialogCloseCount===3&&submits===2&&d.returnValue==='kept'") && f.no_navigation());
    }
    {
        Fixture f;
        CHECK("cancelled submit keeps dialog open and has no navigation", value(f.context,
            "d.showModal();f.addEventListener('submit',e=>e.preventDefault());yes.click();d.open&&dialogCloseCount===0&&submits===1") && f.no_navigation());
    }
    {
        Fixture f;
        CHECK("cancelled click prevents submit and close", value(f.context,
            "d.showModal();yes.addEventListener('click',e=>e.preventDefault());yes.click();d.open&&dialogCloseCount===0&&submits===0") && f.no_navigation());
    }
    {
        Fixture f;
        CHECK("submitter dialog override runs before invalid action URL parsing", value(f.context,
            "f.method='get';yes.setAttribute('formmethod','DiAlOg');yes.setAttribute('formaction','https://[');"
            "d.showModal();f.requestSubmit(yes);!d.open&&dialogCloseCount===1&&d.returnValue==='accepted'") && f.no_navigation());
    }
    {
        Fixture f;
        CHECK("submitter post override preserves open dialog", value(f.context,
            "f.action='/post';yes.setAttribute('formmethod','POST');d.showModal();yes.click();d.open&&dialogCloseCount===0&&submits===1"));
        CHECK("post override delivers actual URL/body/content type", f.get_calls == 0 && f.post_calls == 1 &&
            f.last_url == "https://forms.test/post" && f.last_body == "q=hello&choice=accepted" &&
            f.last_type == "application/x-www-form-urlencoded");
    }
    for (const char *method : {"", "invalid", "get"}) {
        Fixture f;
        std::string script = std::string("yes.setAttribute('formmethod','") + method + "');d.showModal();yes.click();d.open&&dialogCloseCount===0";
        CHECK("present invalid/empty/get submitter method overrides dialog with get", value(f.context, script.c_str()) &&
            f.get_calls == 1 && f.post_calls == 0 && f.last_url == "https://forms.test/start?q=hello&choice=accepted");
    }
    {
        Fixture f("<form id=f method=dialog action='https://['><button id=yes value=accepted>Continue</button></form>");
        CHECK("dialog method without a dialog ancestor has no navigation", value(f.context,
            "f.requestSubmit(yes);submits===1") && f.no_navigation());
    }
    {
        Fixture f("<dialog id=outer open><dialog id=d><form id=f method=dialog><button id=yes value=inner>Continue</button></form></dialog></dialog>");
        CHECK("nearest dialog closes while outer dialog stays open", value(f.context,
            "d.showModal();yes.click();!d.open&&document.getElementById('outer').open&&dialogCloseCount===1&&d.returnValue==='inner'") && f.no_navigation());
    }
    {
        Fixture f;
        CHECK("default action uses the internal close algorithm", value(f.context,
            "globalThis.overridden=0;d.close=()=>overridden++;d.showModal();yes.click();!d.open&&dialogCloseCount===1&&overridden===0&&d.returnValue==='accepted'") && f.no_navigation());
    }
    {
        Fixture f;
        CHECK("trusted activation fixture opens", value(f.context, "d.showModal();globalThis.trusted=false;yes.addEventListener('click',e=>trusted=e.isTrusted);d.open"));
        Node *button = doc_get_element_by_id(f.document, "yes");
        bool dispatched = button && js_dispatch(f.context, button, "click", "MouseEvent", true, true, 0, 0, 0, nullptr);
        js_release_pins(); js_tick(f.context);
        CHECK("native trusted click uses the same dialog close algorithm", dispatched && value(f.context,
            "trusted&&!d.open&&dialogCloseCount===1&&submits===1&&lastSubmitter===yes&&d.returnValue==='accepted'") && f.no_navigation());
    }
    {
        Fixture f;
        CHECK("ordinary get submission stays available", value(f.context,
            "f.method='get';f.action='/search';yes.click();true") && f.get_calls == 1 && f.post_calls == 0 &&
            f.last_url == "https://forms.test/search?q=hello&choice=accepted");
    }
    {
        Fixture f;
        CHECK("ordinary post submission stays available", value(f.context,
            "f.method='post';f.action='/save';f.submit();true") && f.post_calls == 1 && f.get_calls == 0 &&
            f.last_url == "https://forms.test/save" && f.last_body == "q=hello");
    }
    {
        Fixture f;
        CHECK("the official upload setup can assign reflected form attributes", value(f.context,
            R"JS((()=>{Object.assign(f,{acceptCharset:'UTF-8',action:'/upload',method:'POST',enctype:'multipart/form-data',target:'upload-frame'});return f.acceptCharset==='UTF-8'&&f.getAttribute('accept-charset')==='UTF-8'&&f.enctype==='multipart/form-data'&&f.method==='post'&&f.target==='upload-frame';})())JS"));
        CHECK("missing form enctype and encoding both use the URL-encoded default", value(f.context,
            R"JS((()=>{f.removeAttribute('enctype');return f.enctype==='application/x-www-form-urlencoded'&&f.encoding===f.enctype;})())JS"));
        CHECK("form enctype canonicalizes all known keywords while retaining raw spelling", value(f.context,
            R"JS(['MuLtIpArT/FoRm-DaTa','TEXT/PLAIN','APPLICATION/X-WWW-FORM-URLENCODED'].every(raw=>{f.enctype=raw;return f.enctype===raw.toLowerCase()&&f.encoding===f.enctype&&f.getAttribute('enctype')===raw;}))JS"));
        CHECK("invalid form encodings default without trimming or accepting MIME parameters", value(f.context,
            R"JS(['','invalid',' text/plain','text/plain ','multipart/form-data; boundary=x','text/plain\0tail','t\u00c9xt/plain'].every(raw=>{f.enctype=raw;return f.enctype==='application/x-www-form-urlencoded'&&f.encoding===f.enctype&&f.getAttribute('enctype')===raw;}))JS"));
        CHECK("encoding is a live alias of the enctype content attribute", value(f.context,
            R"JS((()=>{f.encoding='TEXT/PLAIN';if(f.enctype!=='text/plain'||f.getAttribute('enctype')!=='TEXT/PLAIN'||f.hasAttribute('encoding'))return false;f.enctype='multipart/form-data';return f.encoding==='multipart/form-data';})())JS"));
        CHECK("attribute changes and removal update both form encoding aliases", value(f.context,
            R"JS((()=>{f.setAttribute('enctype','MuLtIpArT/FoRm-DaTa');if(f.enctype!=='multipart/form-data'||f.encoding!==f.enctype)return false;f.removeAttribute('enctype');return f.enctype==='application/x-www-form-urlencoded'&&f.encoding===f.enctype;})())JS"));
        CHECK("acceptCharset reflects the hyphenated content attribute without creating acceptcharset", value(f.context,
            R"JS((()=>{f.removeAttribute('accept-charset');f.removeAttribute('acceptcharset');f.acceptCharset='windows-1252 UTF-8';return f.acceptCharset==='windows-1252 UTF-8'&&f.getAttribute('accept-charset')==='windows-1252 UTF-8'&&!f.hasAttribute('acceptcharset');})())JS"));
        CHECK("acceptCharset follows external attribute changes and missing-value reflection", value(f.context,
            R"JS((()=>{f.setAttribute('accept-charset','  UTF-8  ');if(f.acceptCharset!=='  UTF-8  ')return false;f.removeAttribute('accept-charset');return f.acceptCharset==='';})())JS"));
        CHECK("new form string setters use DOMString conversion including null and undefined", value(f.context,
            R"JS((()=>{for(const [key,attr] of [['enctype','enctype'],['encoding','enctype'],['acceptCharset','accept-charset']])for(const item of [null,undefined,42,false,12n]){f[key]=item;if(f.getAttribute(attr)!==String(item))return false;}return true;})())JS"));
        CHECK("new reflected setters perform one string-hint conversion", value(f.context,
            R"JS((()=>{let calls=0;f.enctype={[Symbol.toPrimitive](hint){if(hint!=='string')throw Error('wrong hint');calls++;return 'TEXT/PLAIN';}};return calls===1&&f.enctype==='text/plain'&&f.getAttribute('enctype')==='TEXT/PLAIN';})())JS"));
        CHECK("reflected setter conversion exceptions are retained without an attribute change", value(f.context,
            R"JS((()=>{const marker={};f.enctype='text/plain';try{f.enctype={toString(){throw marker;}};return false;}catch(e){return e===marker&&f.getAttribute('enctype')==='text/plain';}})())JS"));
        CHECK("new form string setters reject Symbols without mutating attributes", value(f.context,
            R"JS((()=>{for(const [key,attr] of [['enctype','enctype'],['encoding','enctype'],['acceptCharset','accept-charset']]){f.setAttribute(attr,'kept');try{f[key]=Symbol();return false;}catch(e){if(!(e instanceof TypeError)||f.getAttribute(attr)!=='kept')return false;}}return true;})())JS"));
        CHECK("form reflection rejects foreign receivers before converting the input", value(f.context,
            R"JS((()=>{let converted=0;const input={toString(){converted++;return 'text/plain';}};for(const key of ['enctype','encoding','acceptCharset']){const d=Object.getOwnPropertyDescriptor(HTMLFormElement.prototype,key);if(!d||!d.set)return false;for(const receiver of [null,{},document.createElement('input'),Object.create(HTMLFormElement.prototype)]){try{d.set.call(receiver,input);return false;}catch(e){if(!(e instanceof TypeError))return false;}try{d.get.call(receiver);return false;}catch(e){if(!(e instanceof TypeError))return false;}}}return converted===0;})())JS"));
        CHECK("form reflection uses intrinsic attribute mutation despite an author override", value(f.context,
            R"JS((()=>{let called=0;f.setAttribute=()=>{called++;throw Error('author override');};try{f.enctype='text/plain';f.encoding='multipart/form-data';f.acceptCharset='UTF-8';return called===0&&f.getAttribute('enctype')==='multipart/form-data'&&f.getAttribute('accept-charset')==='UTF-8';}finally{delete f.setAttribute;}})())JS"));
        CHECK("form reflection retains its intrinsic TypeError after a global replacement", value(f.context,
            R"JS((()=>{const Original=TypeError,d=Object.getOwnPropertyDescriptor(HTMLFormElement.prototype,'enctype');let called=0;globalThis.TypeError=function(){called++;};try{for(const action of [()=>d.set.call({},'text/plain'),()=>d.get.call({}),()=>d.set.call(f,Symbol())]){try{action();return false;}catch(error){if(!(error instanceof Original))return false;}}return called===0;}finally{globalThis.TypeError=Original;}})())JS"));
        CHECK("form encoding accessors are enumerable configurable and nonconstructible", value(f.context,
            R"JS(['enctype','encoding','acceptCharset'].every(key=>{const d=Object.getOwnPropertyDescriptor(HTMLFormElement.prototype,key);if(!d||!d.enumerable||!d.configurable||typeof d.get!=='function'||typeof d.set!=='function'||d.get.length!==0||d.set.length!==1||d.get.name!=='get '+key||d.set.name!=='set '+key)return false;return [d.get,d.set].every(fn=>{if(Object.hasOwn(fn,'prototype'))return false;try{Reflect.construct(Object,[],fn);return false;}catch(e){return e instanceof TypeError;}});}))JS"));
    }
    {
        Fixture f;
        CHECK("input and button formEnctype are empty when the override is absent", value(f.context,
            R"JS(['input','button'].every(tag=>document.createElement(tag).formEnctype===''))JS"));
        CHECK("input and button formEnctype canonicalize while retaining their raw attribute", value(f.context,
            R"JS(['input','button'].every(tag=>['TEXT/PLAIN','MuLtIpArT/FoRm-DaTa','APPLICATION/X-WWW-FORM-URLENCODED'].every(raw=>{const e=document.createElement(tag);e.formEnctype=raw;return e.formEnctype===raw.toLowerCase()&&e.getAttribute('formenctype')===raw;})))JS"));
        CHECK("present empty and invalid formEnctype have a default distinct from absence", value(f.context,
            R"JS(['input','button'].every(tag=>['','invalid','text/plain ','text/plain\0tail'].every(raw=>{const e=document.createElement(tag);e.formEnctype=raw;if(e.formEnctype!=='application/x-www-form-urlencoded'||e.getAttribute('formenctype')!==raw)return false;e.removeAttribute('formenctype');return e.formEnctype==='';})))JS"));
        CHECK("formEnctype converts once and rejects Symbol values without mutation", value(f.context,
            R"JS(['input','button'].every(tag=>{const e=document.createElement(tag);let calls=0;e.formEnctype={toString(){calls++;return 'TEXT/PLAIN';}};try{e.formEnctype=Symbol();return false;}catch(error){return error instanceof TypeError&&calls===1&&e.formEnctype==='text/plain'&&e.getAttribute('formenctype')==='TEXT/PLAIN';}}))JS"));
        CHECK("input and button reflection each reject the other interface before conversion", value(f.context,
            R"JS((()=>{let calls=0;for(const [C,wrong] of [[HTMLInputElement,'button'],[HTMLButtonElement,'input']]){const d=Object.getOwnPropertyDescriptor(C.prototype,'formEnctype');if(!d||!d.set)return false;const value={toString(){calls++;return 'text/plain';}};try{d.set.call(document.createElement(wrong),value);return false;}catch(e){if(!(e instanceof TypeError))return false;}try{d.get.call(document.createElement(wrong));return false;}catch(e){if(!(e instanceof TypeError))return false;}}return calls===0;})())JS"));
        CHECK("formEnctype uses intrinsic attribute mutation despite an author override", value(f.context,
            R"JS(['input','button'].every(tag=>{const e=document.createElement(tag);e.setAttribute=()=>{throw Error('author override');};e.formEnctype='TEXT/PLAIN';return e.getAttribute('formenctype')==='TEXT/PLAIN'&&e.formEnctype==='text/plain';}))JS"));
        CHECK("input and button formEnctype have genuine WebIDL accessor descriptors", value(f.context,
            R"JS([HTMLInputElement,HTMLButtonElement].every(C=>{const d=Object.getOwnPropertyDescriptor(C.prototype,'formEnctype');if(!d||!d.enumerable||!d.configurable||typeof d.get!=='function'||typeof d.set!=='function'||d.get.length!==0||d.set.length!==1||d.get.name!=='get formEnctype'||d.set.name!=='set formEnctype')return false;return [d.get,d.set].every(fn=>{if(Object.hasOwn(fn,'prototype'))return false;try{Reflect.construct(Object,[],fn);return false;}catch(e){return e instanceof TypeError;}});}))JS"));
    }
    {
        Fixture f;
        CHECK("POST uses the form's reflected text/plain state without a submitter override", value(f.context,
            "f.method='post';f.action='/upload';f.enctype='TEXT/PLAIN';f.requestSubmit(yes);true") &&
            f.post_calls == 1 && f.get_calls == 0 && f.last_url == "https://forms.test/upload" &&
            f.last_type == "text/plain" && f.last_body == "q=hello\r\nchoice=accepted\r\n");
    }
    {
        Fixture f;
        CHECK("an empty button enctype override wins over the form multipart state", value(f.context,
            "f.method='post';f.setAttribute('enctype','multipart/form-data');yes.setAttribute('formenctype','');f.requestSubmit(yes);true") &&
            f.post_calls == 1 && f.get_calls == 0 && f.last_type == "application/x-www-form-urlencoded" &&
            f.last_body == "q=hello&choice=accepted");
    }
    {
        Fixture f;
        CHECK("an invalid button enctype override wins over the form text/plain state", value(f.context,
            "f.method='post';f.setAttribute('enctype','text/plain');yes.setAttribute('formenctype','invalid');f.requestSubmit(yes);true") &&
            f.post_calls == 1 && f.get_calls == 0 && f.last_type == "application/x-www-form-urlencoded" &&
            f.last_body == "q=hello&choice=accepted");
    }
    {
        Fixture f;
        CHECK("the button formEnctype setter selects the actual POST serialization", value(f.context,
            "f.method='post';f.enctype='multipart/form-data';yes.formEnctype='TeXt/PlAiN';f.requestSubmit(yes);true") &&
            f.post_calls == 1 && f.get_calls == 0 && f.last_type == "text/plain" &&
            f.last_body == "q=hello\r\nchoice=accepted\r\n");
    }
    {
        Fixture f;
        CHECK("the form enctype setter reaches the existing multipart host boundary", value(f.context,
            "f.method='post';f.enctype='MULTIPART/FORM-DATA';f.submit();true") &&
            f.post_calls == 1 && f.get_calls == 0 && f.last_type.find("multipart/form-data; boundary=") == 0 &&
            f.last_body.find("Content-Disposition: form-data; name=\"q\"\r\n\r\nhello\r\n") != std::string::npos);
    }
    {
        Fixture f;
        CHECK("encoding alias assignments select POST content type", value(f.context,
            "f.method='post';f.encoding='TEXT/PLAIN';f.submit();true") &&
            f.post_calls == 1 && f.get_calls == 0 && f.last_type == "text/plain" && f.last_body == "q=hello\r\n");
    }
    {
        Fixture f;
        CHECK("an invalid form enctype assignment uses the URL-encoded POST default", value(f.context,
            "f.method='post';f.enctype=' text/plain';f.submit();true") &&
            f.post_calls == 1 && f.get_calls == 0 && f.last_type == "application/x-www-form-urlencoded" && f.last_body == "q=hello");
    }
    {
        Fixture f("<form id=f method=post enctype='text/plain'><input name=q value=hello><input id=yes type=submit name=choice value=accepted></form>");
        CHECK("an input formEnctype empty override also wins over the form state", value(f.context,
            "yes.formEnctype='';f.requestSubmit(yes);true") &&
            f.post_calls == 1 && f.get_calls == 0 && f.last_type == "application/x-www-form-urlencoded" &&
            f.last_body == "q=hello&choice=accepted");
    }
    {
        Fixture f;
        CHECK("submission reads internal enctype attributes instead of author IDL overrides", value(f.context,
            R"JS((()=>{f.method='post';f.setAttribute('enctype','text/plain');yes.setAttribute('formenctype','');Object.defineProperty(f,'enctype',{get(){throw Error('author form getter');}});Object.defineProperty(yes,'formEnctype',{get(){throw Error('author submitter getter');}});f.requestSubmit(yes);return true;})())JS") &&
            f.post_calls == 1 && f.get_calls == 0 && f.last_type == "application/x-www-form-urlencoded" &&
            f.last_body == "q=hello&choice=accepted");
    }
    {
        Fixture f;
        CHECK("GET submission ignores encoding overrides and keeps its query serialization", value(f.context,
            "f.method='get';f.action='/search';f.enctype='multipart/form-data';yes.formEnctype='text/plain';f.requestSubmit(yes);true") &&
            f.post_calls == 0 && f.get_calls == 1 && f.last_url == "https://forms.test/search?q=hello&choice=accepted");
    }
    net_shutdown();
    printf("RESULTS form-dialog %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks-failures, checks);
    return failures ? 1 : 0;
}
