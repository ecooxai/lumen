/* End-to-end regressions for byte-counted URL input and DOM attributes.
 * The failures were observed in upstream WPT URL tests on 2026-10-22.
 * No network request or test-expectation override is used here.
 */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <unistd.h>

extern "C" void font_init(void);

static int checks, failures;
#define CHECK(name, condition) do { ++checks; if (!(condition)) { \
    ++failures; fprintf(stderr, "FAIL %s\n", name); \
} } while (0)

static bool value(JsCtx *c, const char *source) {
    JS_ENTER(c);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    return v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&result) && result->BooleanValue(iso);
}

/* Use the public DOM setter so these regressions can run before the new native
 * byte-counted storage APIs exist. The V8 string owns the complete UTF-8 value. */
static bool set_attribute(JsCtx *c, Node *node, const char *name, const std::string &text) {
    JS_ENTER(c);
    v8::TryCatch caught(iso);
    v8::Local<v8::Value> wrapped = jwrap(c, node), method, result;
    if (!wrapped->IsObject()) return false;
    auto object = wrapped.As<v8::Object>();
    if (!object->Get(ctx, jstr(iso, "setAttribute")).ToLocal(&method) || !method->IsFunction()) return false;
    v8::Local<v8::Value> args[] = {jstr(iso, name), jstr(iso, text.data(), (int)text.size())};
    return method.As<v8::Function>()->Call(ctx, object, 2, args).ToLocal(&result);
}

struct Fixture {
    Document *document;
    JsCtx *context;
    Fixture() {
        document = doc_new("https://sender.test/start");
        const char *html = "<!doctype html><html><body></body></html>";
        html_parse(document, html, strlen(html));
        JsHost host{};
        context = js_new(document, &host);
    }
    JsCtx *frame(const char *url, const std::string *sandbox = nullptr) {
        Node *element = node_new_element(document, "iframe", NS_HTML);
        node_set_attr(element, "src", url);
        node_append(document->body, element);
        if (sandbox && !set_attribute(context, element, "sandbox", *sandbox)) return nullptr;
        Document *child = doc_new(url);
        const char *html = "<!doctype html><html><body></body></html>";
        html_parse(child, html, strlen(html));
        JsHost host{};
        return js_new_ex(child, &host, context, element);
    }
    ~Fixture() { js_free(context); doc_free(document); }
};

int main(int argc, char **argv) {
    (void)argc; dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    {
        Fixture f;
        CHECK("URL trims leading and trailing C0 bytes without losing the input", value(f.context,
            R"JS(new URL('\0\u001b\u0004\u0012 http://example.com/\u001f \r ').href==='http://example.com/')JS"));
        CHECK("URL preserves embedded NUL in a hierarchical path", value(f.context,
            R"JS(new URL('https://x/\0y').href==='https://x/%00y')JS"));
        CHECK("URL preserves embedded NUL in a query", value(f.context,
            R"JS(new URL('https://x/?\0y').href==='https://x/?%00y')JS"));
        CHECK("URL preserves embedded NUL in a fragment", value(f.context,
            R"JS(new URL('https://x/?#\0y').href==='https://x/?#%00y')JS"));
        CHECK("URL preserves embedded NUL in an opaque path", value(f.context,
            R"JS(new URL('non-special:x/\0y').href==='non-special:x/%00y')JS"));
        CHECK("relative URL resolution preserves embedded NUL", value(f.context,
            R"JS(new URL('a\0b','https://sender.test/base/').href==='https://sender.test/base/a%00b')JS"));
        CHECK("embedded NUL is not a Windows drive delimiter", value(f.context,
            R"JS(new URL('C|\0x','file:///D:/base/file').href==='file:///D:/base/C|%00x')JS"));
        CHECK("special hosts reject raw NUL instead of accepting the prefix", value(f.context,
            R"JS(!URL.canParse('http://a\0b/') && !URL.canParse('https://\0y'))JS"));
        CHECK("opaque hosts reject raw NUL instead of accepting the prefix", value(f.context,
            R"JS(!URL.canParse('sc://a\0b/'))JS"));
        CHECK("special hosts reject percent-decoded NUL", value(f.context,
            R"JS(!URL.canParse('http://hello%00') && !URL.canParse('http://ho%00st/'))JS"));
        CHECK("a base URL containing forbidden NUL is invalid", value(f.context,
            R"JS(!URL.canParse('https://valid.test/','https://a\0b/'))JS"));
        CHECK("postMessage rejects a target origin with an embedded NUL host", value(f.context,
            R"JS((()=>{try{window.postMessage('message','https://sender.test\0.other/');}
                catch(e){return e.name==='SyntaxError';}return false;})())JS"));

        CHECK("setAttribute and getAttribute preserve all raw bytes", value(f.context,
            R"JS(globalThis.nulAnchor=document.createElement('a'); nulAnchor.id='nul-url';
                document.body.appendChild(nulAnchor); nulAnchor.setAttribute('href','https://sender.test/a\0b');
                nulAnchor.getAttribute('href')==='https://sender.test/a\0b')JS"));
        Node *anchor = doc_get_element_by_id(f.document, "nul-url");
        CHECK("native presence is retained for an embedded-NUL value", anchor && node_has_attr(anchor, "href"));
        CHECK("legacy native consumers never receive a truncated URL prefix", anchor && node_attr(anchor, "href") == nullptr);
        CHECK("anchor URL reflection encodes the complete raw path", value(f.context,
            R"JS(nulAnchor.href==='https://sender.test/a%00b')JS"));
        CHECK("attribute node and map expose the complete DOM value", value(f.context,
            R"JS(nulAnchor.getAttributeNode('href').value==='https://sender.test/a\0b' &&
                nulAnchor.attributes.getNamedItem('href').value==='https://sender.test/a\0b')JS"));
        CHECK("cloneNode preserves attribute bytes and reflected URL", value(f.context,
            R"JS(globalThis.nulClone=nulAnchor.cloneNode(false);
                nulClone.getAttribute('href')==='https://sender.test/a\0b' && nulClone.href==='https://sender.test/a%00b')JS"));
        CHECK("an attribute name containing NUL cannot alias an existing name", value(f.context,
            R"JS(nulAnchor.getAttribute('href\0suffix')===null && !nulAnchor.hasAttribute('href\0suffix'))JS"));
        CHECK("changing a suffix after NUL updates the stored value", value(f.context,
            R"JS(nulAnchor.setAttribute('href','https://sender.test/a\0c');
                nulAnchor.getAttribute('href')==='https://sender.test/a\0c' && nulAnchor.href==='https://sender.test/a%00c')JS"));
        CHECK("invalid raw host preserves the original href and protocol fallback", value(f.context,
            R"JS(nulAnchor.setAttribute('href','https://a\0b/');
                nulAnchor.href==='https://a\0b/' && nulAnchor.protocol===':')JS"));
        CHECK("ordinary replacement keeps normal native attribute behavior", value(f.context,
            R"JS(nulAnchor.setAttribute('href','https://sender.test/ok'); nulAnchor.href==='https://sender.test/ok')JS") &&
            anchor && node_attr(anchor, "href") && !strcmp(node_attr(anchor, "href"), "https://sender.test/ok"));
        CHECK("removing an embedded-NUL attribute clears its presence", value(f.context,
            R"JS(nulAnchor.setAttribute('href','https://a\0b/'); nulAnchor.removeAttribute('href');
                !nulAnchor.hasAttribute('href') && nulAnchor.getAttribute('href')===null)JS") &&
            anchor && !node_has_attr(anchor, "href"));
    }
    {
        Fixture f;
        const char first_text[] = "allow-same-origin\0 allow-scripts";
        const std::string first(first_text, sizeof first_text - 1);
        JsCtx *a = f.frame("https://sender.test/first", &first);
        CHECK("a NUL suffix cannot turn an invalid sandbox token into allow-same-origin",
            a && a->sb_opaque && a->opaque_origin);
        CHECK("valid sandbox tokens after NUL are still recognized",
            a && !a->sb_noscript);
        const char second_text[] = "allow-scripts\0 allow-same-origin";
        const std::string second(second_text, sizeof second_text - 1);
        JsCtx *b = f.frame("https://sender.test/second", &second);
        CHECK("a valid same-origin sandbox token after NUL retains tuple identity",
            b && !b->sb_opaque && !b->opaque_origin);
        CHECK("a NUL suffix cannot turn an invalid sandbox token into allow-scripts",
            b && b->sb_noscript);
    }
    {
        Fixture f;
        JsCtx *same = f.frame("https://sender.test/child");
        const char same_allow[] = "microphone *\0; camera 'none'";
        CHECK("an allow directive after NUL is not replaced by a same-origin default grant",
            same && set_attribute(f.context, same->frame_el, "allow", std::string(same_allow, sizeof same_allow - 1)) &&
            !js_capture_allowed(same, "camera"));
        CHECK("a NUL suffix cannot turn an invalid allow token into a wildcard grant",
            same && !js_capture_allowed(same, "microphone"));
        JsCtx *cross = f.frame("https://child.test/child");
        const char cross_allow[] = "camera 'none'\0; microphone *";
        CHECK("valid independent allow directives after NUL retain their grants",
            cross && set_attribute(f.context, cross->frame_el, "allow", std::string(cross_allow, sizeof cross_allow - 1)) &&
            js_capture_allowed(cross, "microphone"));
        CHECK("invalid allow tokens do not grant another feature",
            cross && !js_capture_allowed(cross, "camera"));
        const char origin_allow[] = "camera https://child.test\0.other/";
        CHECK("an allow origin cannot lose an invalid host suffix",
            cross && set_attribute(f.context, cross->frame_el, "allow", std::string(origin_allow, sizeof origin_allow - 1)) &&
            !js_capture_allowed(cross, "camera"));
        const char invalid_src[] = "https://child.test\0.other/";
        CHECK("src delegation cannot lose an invalid host suffix",
            cross && set_attribute(f.context, cross->frame_el, "allow", "camera") &&
            set_attribute(f.context, cross->frame_el, "src", std::string(invalid_src, sizeof invalid_src - 1)) &&
            !js_capture_allowed(cross, "camera"));
        const char path_src[] = "https://child.test/path\0suffix";
        CHECK("src delegation uses the complete valid URL with an encoded NUL path",
            cross && set_attribute(f.context, cross->frame_el, "src", std::string(path_src, sizeof path_src - 1)) &&
            js_capture_allowed(cross, "camera"));
    }
    {
        Fixture f;
        CHECK("an invalid external classic script cannot become an inline script", value(f.context,
            R"JS(globalThis.nulClassicRan=false;globalThis.nulClassic=document.createElement('script');
                nulClassic.src='https://[\0invalid';nulClassic.textContent='nulClassicRan=true';
                document.body.appendChild(nulClassic);!nulClassicRan)JS"));
        CHECK("an invalid external module cannot become an inline module", value(f.context,
            R"JS(globalThis.nulModuleRan=false;globalThis.nulModule=document.createElement('script');
                nulModule.type='module';nulModule.src='https://[\0invalid';nulModule.textContent='nulModuleRan=true';
                document.body.appendChild(nulModule);!nulModuleRan)JS"));
        CHECK("a JavaScript type with a NUL suffix is not a valid executable MIME type", value(f.context,
            R"JS(globalThis.nulTypeRan=false;globalThis.nulType=document.createElement('script');
                nulType.type='text/javascript\0suffix';nulType.textContent='nulTypeRan=true';
                document.body.appendChild(nulType);!nulTypeRan)JS"));
        CHECK("a non-executable data type with NUL cannot become the default script type", value(f.context,
            R"JS(globalThis.nulDataRan=false;globalThis.nulData=document.createElement('script');
                nulData.type='application/ld+json\0suffix';nulData.textContent='nulDataRan=true';
                document.body.appendChild(nulData);!nulDataRan)JS"));
        CHECK("a module type with a NUL suffix cannot select either executable script type", value(f.context,
            R"JS(globalThis.nulModuleTypeRan=false;globalThis.nulModuleType=document.createElement('script');
                nulModuleType.type='module\0suffix';nulModuleType.textContent='nulModuleTypeRan=true';
                document.body.appendChild(nulModuleType);!nulModuleTypeRan)JS"));
        CHECK("nomodule presence is retained even when its value contains NUL", value(f.context,
            R"JS(globalThis.nulNomoduleRan=false;globalThis.nulNomodule=document.createElement('script');
                nulNomodule.setAttribute('nomodule','\0suffix');nulNomodule.textContent='nulNomoduleRan=true';
                document.body.appendChild(nulNomodule);!nulNomoduleRan)JS"));
    }
    {
        Fixture f;
        CHECK("history stores a canonical URL containing the entire NUL path", value(f.context,
            R"JS(history.pushState({kept:true},'','/history\0suffix');
                location.href==='https://sender.test/history%00suffix' && history.state.kept)JS") &&
            !strcmp(f.document->url, "https://sender.test/history%00suffix"));
        CHECK("history rejects a raw-NUL host without changing the native URL", value(f.context,
            R"JS((()=>{try{history.replaceState({changed:true},'','https://sender.test\0.other/');}
                catch(e){return e.name==='SecurityError' && history.state.kept;}return false;})())JS") &&
            !strcmp(f.document->url, "https://sender.test/history%00suffix"));
    }
    {
        Fixture f;
        CHECK("Location href parse failures are SyntaxError DOMExceptions without navigation", value(f.context,
            R"JS(globalThis.checkLocationSyntax = change => {
                    for (const input of ['http://[', 'file://example:1/', 'https://a%00b/', 'https://a\0b/']) {
                        const before = location.href;
                        try { change(input); } catch (error) {
                            if (!(error instanceof DOMException) || error.name !== 'SyntaxError' ||
                                error.code !== 12 || location.href !== before) return false;
                            continue;
                        }
                        return false;
                    }
                    return true;
                };
                checkLocationSyntax(input => { location.href = input; }))JS"));
        CHECK("window.location parse failures use the same SyntaxError boundary", value(f.context,
            R"JS(checkLocationSyntax(input => { window.location = input; }))JS"));
        CHECK("Location assign rejects invalid URLs before navigating", value(f.context,
            R"JS(checkLocationSyntax(input => location.assign(input)))JS"));
        CHECK("Location replace rejects invalid URLs before navigating", value(f.context,
            R"JS(checkLocationSyntax(input => location.replace(input)))JS"));
        CHECK("Location assign requires its URL argument", value(f.context,
            R"JS((()=>{try{location.assign();}catch(error){return error instanceof TypeError;}return false;})())JS"));
        CHECK("Location replace requires its URL argument", value(f.context,
            R"JS((()=>{try{location.replace();}catch(error){return error instanceof TypeError;}return false;})())JS"));
        CHECK("Location rejects Symbol inputs during string conversion", value(f.context,
            R"JS([input => { location.href = input; }, input => location.assign(input), input => location.replace(input)]
                .every(change => { try { change(Symbol('url')); } catch(error) { return error instanceof TypeError; } return false; }))JS"));
        CHECK("Location preserves an exception thrown by the input's conversion", value(f.context,
            R"JS((()=>{const marker={conversion:true};return [input => { location.href=input; }, input => location.assign(input), input => location.replace(input)]
                .every(change => {try {change({toString(){throw marker;}});}catch(error){return error===marker;}return false;});})())JS"));
        CHECK("Location converts each rejected input exactly once", value(f.context,
            R"JS([input => { location.href = input; }, input => location.assign(input), input => location.replace(input)]
                .every(change => { let calls=0; try { change({toString(){calls++;return 'http://[';}}); }
                    catch(error) { return calls===1 && error instanceof DOMException && error.name==='SyntaxError'; } return false; }))JS"));
        CHECK("Location assign retains valid same-document fragment navigation", value(f.context,
            R"JS(location.assign('#location-valid');location.href==='https://sender.test/start#location-valid')JS"));
        CHECK("Location converts a valid navigation input exactly once", value(f.context,
            R"JS(globalThis.locationConversions=0;location.assign({toString(){locationConversions++;return '#converted';}});
                locationConversions===1 && location.href==='https://sender.test/start#converted')JS"));
        CHECK("iframe Location parse failures use the iframe's DOMException realm", value(f.context,
            R"JS((()=>{const frame=document.body.appendChild(document.createElement('iframe'));
                try {frame.contentWindow.location='http://[';}catch(error){const correct=error instanceof frame.contentWindow.DOMException && error.name==='SyntaxError' && error.code===12;frame.remove();return correct;}
                frame.remove();return false;})())JS"));
    }
    {
        Fixture f;
        CHECK("invalid protocols leave both href and reflected protocol unchanged", value(f.context,
            R"JS(['', '0http', 'https\0', 'https ', 'h,tps'].every(input=>{const u=new URL('http://x/');u.protocol=input;return u.href==='http://x/' && u.protocol==='http:';}))JS"));
        CHECK("protocol setters cannot cross special scheme boundaries", value(f.context,
            R"JS((()=>{const a=new URL('http://x/'),b=new URL('custom://x/');a.protocol='custom';b.protocol='https';return a.href==='http://x/' && b.href==='custom://x/';})())JS"));
        CHECK("protocol mutation preserves an opaque path", value(f.context,
            R"JS((()=>{const u=new URL('data:text/plain,hello');u.protocol='custom';return u.href==='custom:text/plain,hello' && u.pathname==='text/plain,hello' && u.host==='';})())JS"));
        CHECK("protocol parsing normalizes case and ignores text after its first colon", value(f.context,
            R"JS((()=>{const u=new URL('http://x/');u.protocol='H\r\nTT\tPS:ignored space';return u.href==='https://x/' && u.protocol==='https:';} )())JS"));
        CHECK("changing protocol removes a port that becomes the new default", value(f.context,
            R"JS((()=>{const u=new URL('http://x:443/');u.protocol='https';return u.href==='https://x/' && u.port==='' && u.origin==='https://x';})())JS"));
        CHECK("file protocol restrictions preserve credentials and empty file hosts", value(f.context,
            R"JS((()=>{const a=new URL('http://u:p@x:81/'),b=new URL('file:///a');a.protocol='file';b.protocol='https';return a.href==='http://u:p@x:81/' && b.href==='file:///a';})())JS"));
        CHECK("a password can exist without a username", value(f.context,
            R"JS((()=>{const u=new URL('https://x/');u.password='secret';return u.href==='https://:secret@x/' && u.username==='' && u.password==='secret';})())JS"));
        CHECK("clearing a username retains its password", value(f.context,
            R"JS((()=>{const u=new URL('https://user:secret@x/');u.username='';return u.href==='https://:secret@x/' && u.password==='secret';})())JS"));
        CHECK("clearing a password retains its username", value(f.context,
            R"JS((()=>{const u=new URL('https://user:secret@x/');u.password='';return u.href==='https://user@x/' && u.username==='user' && u.password==='';})())JS"));
        CHECK("credential setters encode controls and delimiters without stripping tabs", value(f.context,
            R"JS(['username','password'].every(key=>{const u=new URL('https://x/');u[key]='a\0\t\n\r:@/é';return u[key]==='a%00%09%0A%0D%3A%40%2F%C3%A9';}))JS"));
        CHECK("file opaque and empty-host URLs reject credentials", value(f.context,
            R"JS(['file://x/a','data:hello','custom:///a'].every(href=>{const u=new URL(href);u.username='user';u.password='pass';return u.href===href && u.username==='' && u.password==='';}))JS"));
        CHECK("host and hostname setters preserve an unspecified existing port", value(f.context,
            R"JS(['host','hostname'].every(key=>{const u=new URL('http://old.test:8080/a');u[key]='new.test';return u.href==='http://new.test:8080/a' && u.host==='new.test:8080' && u.port==='8080';}))JS"));
        CHECK("host commits before an invalid later port without discarding the old port", value(f.context,
            R"JS((()=>{const u=new URL('http://old.test:8080/a');u.host='new.test:invalid';return u.href==='http://new.test:8080/a' && u.hostname==='new.test' && u.port==='8080';})())JS"));
        CHECK("host commits before an out-of-range later port", value(f.context,
            R"JS((()=>{const u=new URL('http://old.test/a');u.host='new.test:65536';return u.href==='http://new.test/a' && u.host==='new.test' && u.port==='';})())JS"));
        CHECK("hostname with a port delimiter is rejected before changing host", value(f.context,
            R"JS((()=>{const u=new URL('http://old.test:8080/a');u.hostname='new.test:80';return u.href==='http://old.test:8080/a' && u.hostname==='old.test';})())JS"));
        CHECK("rejected hosts cannot poison any reflected URL field", value(f.context,
            R"JS(['host','hostname'].every(key=>['bad\0suffix','bad%00suffix','[broken]','bad@host','bad host'].every(input=>{const u=new URL('https://old.test/a');u[key]=input;return u.href==='https://old.test/a' && u.host==='old.test' && u.hostname==='old.test' && u.origin==='https://old.test';})))JS"));
        CHECK("empty opaque hosts are accepted only without credentials or ports", value(f.context,
            R"JS((()=>{const a=new URL('custom://x/a'),b=new URL('custom://u@x/a'),c=new URL('custom://x:3/a');a.host='';b.host='';c.hostname='';return a.href==='custom:///a' && b.href==='custom://u@x/a' && c.href==='custom://x:3/a';})())JS"));
        CHECK("file host mutation normalizes localhost and permits an empty host", value(f.context,
            R"JS((()=>{const a=new URL('file://x/a'),b=new URL('file://x/b');a.host='loc%41lhost';b.hostname='';return a.href==='file:///a' && b.href==='file:///b';})())JS"));
        CHECK("host mutation uses the shared IPv4 and IPv6 parsers", value(f.context,
            R"JS((()=>{const a=new URL('http://x/'),b=new URL('http://x/');a.host='0x7F000001:8080';b.host='[::0:01]:2';return a.href==='http://127.0.0.1:8080/' && b.href==='http://[::1]:2/' && b.hostname==='[::1]';})())JS"));
        CHECK("host parsing stops at path query and fragment delimiters", value(f.context,
            R"JS(['/suffix','?suffix:99','#suffix','\\suffix'].every(suffix=>{const u=new URL('https://old.test/path?q#f');u.host='new.test'+suffix;return u.href==='https://new.test/path?q#f';}))JS"));
        CHECK("a backslash remains a forbidden opaque-host byte", value(f.context,
            R"JS((()=>{const u=new URL('custom://old.test/a');u.host='new.test\\suffix';return u.href==='custom://old.test/a' && u.host==='old.test';})())JS"));
        CHECK("port setters use the leading decimal run", value(f.context,
            R"JS(['8080suffix','8080/stuff','8080?stuff','8080\0tail'].every(input=>{const u=new URL('http://x/a');u.port=input;return u.href==='http://x:8080/a' && u.port==='8080';}))JS"));
        CHECK("invalid or overflowing ports leave all old fields unchanged", value(f.context,
            R"JS(['65536','999999999999999999999999999999999','invalid',' 81','\0'].every(input=>{const u=new URL('http://x:8080/a');u.port=input;return u.href==='http://x:8080/a' && u.host==='x:8080' && u.port==='8080';}))JS"));
        CHECK("empty and scheme-default port setters clear an explicit port", value(f.context,
            R"JS(['','80'].every(input=>{const u=new URL('http://x:8080/a');u.port=input;return u.href==='http://x/a' && u.port==='';}))JS"));
        CHECK("port strips ASCII tab and newline but a stripped empty input is not a clear", value(f.context,
            R"JS((()=>{const u=new URL('https://x:3000/');u.port='\n\t80\n\t80';if(u.port!=='8080')return false;u.port='\t\n\r';return u.port==='8080';})())JS"));
        CHECK("pathname setters cannot replace opaque paths", value(f.context,
            R"JS(['data:hello','mailto:me@example.test','blob:https://x/id'].every(href=>{const u=new URL(href);u.pathname='/replacement';return u.href===href;}))JS"));
        CHECK("pathname uses special backslash and dot-segment rules", value(f.context,
            R"JS((()=>{const u=new URL('https://x/old?q#f');u.pathname='\\a\\%2E\\b\\%2e.\\c';return u.href==='https://x/a/c?q#f';})())JS"));
        CHECK("pathname question marks and hashes are data instead of new components", value(f.context,
            R"JS((()=>{const u=new URL('https://x/old?q#f');u.pathname='a?b#c';return u.href==='https://x/a%3Fb%23c?q#f' && u.search==='?q' && u.hash==='#f';})())JS"));
        CHECK("pathname preserves trailing controls and spaces by encoding them", value(f.context,
            R"JS((()=>{const u=new URL('https://x/');u.pathname='\0end ';return u.href==='https://x/%00end%20' && u.pathname==='/%00end%20';})())JS"));
        CHECK("non-special hosted URLs can have an empty path", value(f.context,
            R"JS((()=>{const a=new URL('custom://x/old'),b=new URL('custom:///old');a.pathname='';b.pathname='';return a.href==='custom://x' && b.href==='custom://' && a.pathname==='' && b.pathname==='';})())JS"));
        CHECK("hostless hierarchical URLs retain their initial path slash", value(f.context,
            R"JS((()=>{const u=new URL('custom:/old');u.pathname='';if(u.href!=='custom:/')return false;u.pathname='next';return u.href==='custom:/next';})())JS"));
        CHECK("file pathname setters normalize a drive and cannot shorten above it", value(f.context,
            R"JS((()=>{const u=new URL('file:///D:/old');u.pathname='C|/a/../../b';return u.href==='file:///C:/b' && u.pathname==='/C:/b';})())JS"));
        CHECK("hostless double-slash paths keep their serialization disambiguator", value(f.context,
            R"JS((()=>{const u=new URL('custom:/old');u.pathname='//next';return u.href==='custom:/.//next' && u.pathname==='//next' && u.host==='';})())JS"));
        CHECK("search encodes a hash instead of creating a fragment", value(f.context,
            R"JS((()=>{const u=new URL('https://x/path#old');u.search='q=#new';return u.href==='https://x/path?q=%23new#old' && u.hash==='#old';})())JS"));
        CHECK("search uses the special-scheme apostrophe encoding rule", value(f.context,
            R"JS((()=>{const a=new URL('https://x/'),b=new URL('custom://x/');a.search="q='";b.search="q='";return a.search==='?q=%27' && b.search==="?q='";})())JS"));
        CHECK("search removes its marker before stripping tabs and retains an empty marker", value(f.context,
            R"JS((()=>{const u=new URL('https://x/');u.search='\t?x';if(u.href!=='https://x/??x')return false;u.search='?';if(u.href!=='https://x/?' || u.search!=='')return false;u.search='';return u.href==='https://x/';})())JS"));
        CHECK("fragment mutation preserves an opaque URL instead of adding an authority", value(f.context,
            R"JS((()=>{const u=new URL('data:text/plain,hello');u.hash='<next>';return u.href==='data:text/plain,hello#%3Cnext%3E' && u.pathname==='text/plain,hello';})())JS"));
        CHECK("empty fragment values distinguish removal from an empty present fragment", value(f.context,
            R"JS((()=>{const u=new URL('https://x/?q#old');u.hash='#';if(u.href!=='https://x/?q#' || u.hash!=='')return false;u.hash='';return u.href==='https://x/?q';})())JS"));
        CHECK("fragment setters encode controls and trailing spaces without trimming", value(f.context,
            R"JS((()=>{const u=new URL('https://x/');u.hash='\0x ';return u.href==='https://x/#%00x%20' && u.hash==='#%00x%20';})())JS"));
        CHECK("component mutation does not double-encode existing percent escapes", value(f.context,
            R"JS((()=>{const u=new URL('https://x/');u.username='%c3%89té';u.pathname='%2f';u.search='%23';u.hash='%20';return u.href==='https://%c3%89t%C3%A9@x/%2f?%23#%20';})())JS"));
        CHECK("setting search updates the same linked searchParams object", value(f.context,
            R"JS((()=>{const u=new URL('https://x/?old=1');const p=u.searchParams;u.search='?new=2';return p===u.searchParams && p.get('old')===null && p.get('new')==='2' && p.size===1;})())JS"));
        CHECK("setting href updates the existing linked searchParams object", value(f.context,
            R"JS((()=>{const u=new URL('https://x/?old=1');const p=u.searchParams;u.href='https://y/?new=2';return p===u.searchParams && p.get('old')===null && p.get('new')==='2';})())JS"));
        CHECK("a rejected href leaves both URL and linked searchParams unchanged", value(f.context,
            R"JS((()=>{const u=new URL('https://x/?kept=1');const p=u.searchParams;try{u.href='/relative';}catch(error){return error instanceof TypeError && u.href==='https://x/?kept=1' && p===u.searchParams && p.get('kept')==='1';}return false;})())JS"));
        CHECK("linked searchParams changes still serialize back into the URL", value(f.context,
            R"JS((()=>{const u=new URL('https://x/');const p=u.searchParams;p.set('q','two words');return u.href==='https://x/?q=two+words' && p===u.searchParams && p.get('q')==='two words';})())JS"));
        CHECK("component setters convert even ignored inputs exactly once", value(f.context,
            R"JS((()=>{const u=new URL('data:hello');let calls=0;u.hostname={toString(){calls++;return 'ignored.test';}};return calls===1 && u.href==='data:hello';})())JS"));
        CHECK("component setters preserve user conversion exceptions and old state", value(f.context,
            R"JS((()=>{const u=new URL('https://x/'),marker={thrown:true};try{u.host={toString(){throw marker;}};}catch(error){return error===marker && u.href==='https://x/';}return false;})())JS"));
        CHECK("URL setters reject Symbol values at the Web IDL string boundary", value(f.context,
            R"JS(['href','protocol','username','password','host','hostname','port','pathname','search','hash'].every(key=>{const u=new URL('https://x/');try{u[key]=Symbol('url');}catch(error){return error instanceof TypeError && u.href==='https://x/';}return false;}))JS"));
        CHECK("component setters use the current URL after reentrant input conversion", value(f.context,
            R"JS((()=>{const u=new URL('http://old.test/');let calls=0;u.port={toString(){calls++;u.href='https://new.test/new';return '8080';}};return calls===1 && u.href==='https://new.test:8080/new';})())JS"));
        CHECK("anchor and area protocol rejection keeps their reflected URL coherent", value(f.context,
            R"JS(['a','area'].every(tag=>{const a=document.createElement(tag);a.href='https://x/a';a.protocol='bad scheme';return a.href==='https://x/a' && a.protocol==='https:' && a.getAttribute('href')==='https://x/a';}))JS"));
        CHECK("anchor and area support password-only credentials", value(f.context,
            R"JS(['a','area'].every(tag=>{const a=document.createElement(tag);a.href='https://x/a';a.password='secret';return a.href==='https://:secret@x/a' && a.username==='' && a.password==='secret';}))JS"));
        CHECK("anchor and area pathname setters encode question marks and NUL", value(f.context,
            R"JS(['a','area'].every(tag=>{const a=document.createElement(tag);a.href='https://x/a?q#f';a.pathname='b?\0';return a.href==='https://x/b%3F%00?q#f' && a.pathname==='/b%3F%00';}))JS"));
        CHECK("anchor and area host setters retain a valid host before an invalid port", value(f.context,
            R"JS(['a','area'].every(tag=>{const a=document.createElement(tag);a.href='https://x:8080/a';a.host='new.test:invalid';return a.href==='https://new.test:8080/a' && a.hostname==='new.test' && a.port==='8080';}))JS"));
        CHECK("anchor and area opaque URLs retain their path when components change", value(f.context,
            R"JS(['a','area'].every(tag=>{const a=document.createElement(tag);a.href='data:hello';a.pathname='/ignored';a.hash='next';return a.href==='data:hello#next' && a.pathname==='hello';}))JS"));
        CHECK("anchor and area convert component inputs before reparsing current href", value(f.context,
            R"JS(['a','area'].every(tag=>{const a=document.createElement(tag);a.href='https://old.test/a';let calls=0;a.pathname={toString(){calls++;a.href='https://new.test/b';return 'next';}};return calls===1 && a.href==='https://new.test/next';}))JS"));
        CHECK("anchor and area reject Symbol components even with an invalid href", value(f.context,
            R"JS(['a','area'].every(tag=>{const a=document.createElement(tag);a.href='http://[';try{a.pathname=Symbol('path');}catch(error){return error instanceof TypeError && a.getAttribute('href')==='http://[';}return false;}))JS"));
        CHECK("clearing opaque URL query data preserves its parsed encoded trailing space", value(f.context,
            R"JS((()=>{const u=new URL('data:space ?q=1');const p=u.searchParams;p.delete('q');return u.href==='data:space%20' && u.pathname==='space%20' && p===u.searchParams && p.size===0;})())JS"));
    }
    {
        Fixture f;
        CHECK("javascript anchor activation returns before executing its script", value(f.context,
            R"JS(globalThis.jsurlAnchor=document.body.appendChild(document.createElement('a'));
                jsurlAnchor.href='javascript:globalThis.jsurlDeferred=1;undefined';
                jsurlAnchor.click(); typeof jsurlDeferred==='undefined')JS"));
        js_tick(f.context);
        CHECK("javascript anchor script runs in a later native task", value(f.context, "jsurlDeferred===1"));
        CHECK("javascript navigation does not store its executable URL in document history", value(f.context,
            "location.href==='https://sender.test/start'"));

        CHECK("invalid javascript hosts are ignored without synchronous execution", value(f.context,
            R"JS(jsurlAnchor.href='javascript://test:test/%0aglobalThis.jsurlInvalidA=1';
                jsurlAnchor.click(); typeof jsurlInvalidA==='undefined')JS"));
        CHECK("invalid hosts with tab in their scheme do not throw from hyperlink activation", value(f.context,
            R"JS(jsurlAnchor.href='java\x09script://test:test/%0aglobalThis.jsurlInvalidB=1';
                jsurlAnchor.click(); typeof jsurlInvalidB==='undefined')JS"));
        js_tick(f.context);
        CHECK("invalid javascript hosts never execute later either", value(f.context,
            "typeof jsurlInvalidA==='undefined' && typeof jsurlInvalidB==='undefined'"));

        CHECK("hierarchical javascript URL execution is deferred", value(f.context,
            R"JS(jsurlAnchor.href='javascript://host/1%0a//../0/;globalThis.jsurlPath=1;/%0aglobalThis.jsurlPath=2;/..///';
                jsurlAnchor.click(); typeof jsurlPath==='undefined')JS"));
        js_tick(f.context);
        CHECK("javascript navigation executes the normalized URL path", value(f.context, "jsurlPath===1"));
        CHECK("javascript query text remains part of the deferred script", value(f.context,
            R"JS(jsurlAnchor.href='javascript:globalThis.jsurlQuery="https://whatsoever.com/?a=b&c=5&x=y";undefined';
                jsurlAnchor.click(); typeof jsurlQuery==='undefined')JS"));
        CHECK("newline-containing schemes are parsed before queueing", value(f.context,
            R"JS(jsurlAnchor.href='java\x0Ascript:globalThis.jsurlNewline="https://whatsoever.com/?a=b&c=5&x=y";undefined';
                jsurlAnchor.click(); typeof jsurlNewline==='undefined')JS"));
        js_tick(f.context);
        CHECK("serialized path and query execute as one script", value(f.context,
            "jsurlQuery==='https://whatsoever.com/?a=b&c=5&x=y' && jsurlNewline===jsurlQuery"));
        CHECK("mixed-case schemes are parsed before activation", value(f.context,
            R"JS(jsurlAnchor.href='JaVaScRiPt:globalThis.jsurlUpper=1;undefined';jsurlAnchor.click();typeof jsurlUpper==='undefined')JS"));
        js_tick(f.context);
        CHECK("canonical mixed-case javascript executes in a later task", value(f.context, "jsurlUpper===1"));

        CHECK("URL source is snapshotted before the anchor changes", value(f.context,
            R"JS(jsurlAnchor.href='javascript:globalThis.jsurlSnapshot=1;undefined';jsurlAnchor.click();
                jsurlAnchor.href='javascript:globalThis.jsurlSnapshot=2;undefined';typeof jsurlSnapshot==='undefined')JS"));
        js_tick(f.context);
        CHECK("later href mutation cannot replace a queued javascript source", value(f.context, "jsurlSnapshot===1"));
        CHECK("malformed percent escapes are accepted as literal script text", value(f.context,
            R"JS(jsurlAnchor.href='javascript:globalThis.jsurlPercent="%zz%";undefined';jsurlAnchor.click();true)JS"));
        CHECK("invalid UTF-8 is queued through the explicit-length native boundary", value(f.context,
            R"JS(jsurlAnchor.href='javascript:globalThis.jsurlUtf8="%FF";undefined';jsurlAnchor.click();true)JS"));
        CHECK("percent-decoded NUL cannot truncate a queued script", value(f.context,
            R"JS(jsurlAnchor.href='javascript:globalThis.jsurlNul="a%00b";globalThis.jsurlNulTail=1;undefined';jsurlAnchor.click();true)JS"));
        js_tick(f.context);
        CHECK("percent decoder preserves malformed escapes", value(f.context, "jsurlPercent==='%zz%'"));
        CHECK("script UTF-8 conversion replaces invalid bytes", value(f.context, "jsurlUtf8==='\xef\xbf\xbd'"));
        CHECK("complete script bytes survive an embedded NUL", value(f.context,
            R"JS(jsurlNul==='a\0b' && jsurlNulTail===1)JS"));

        CHECK("Location javascript assignment is also asynchronous", value(f.context,
            R"JS(location.assign('javascript:globalThis.jsurlLocation=1;undefined');typeof jsurlLocation==='undefined')JS"));
        js_tick(f.context);
        CHECK("Location javascript assignment reaches the native execution queue", value(f.context, "jsurlLocation===1"));
        CHECK("two queued javascript URLs preserve their execution order", value(f.context,
            R"JS(globalThis.jsurlOrder=[];jsurlAnchor.href='javascript:jsurlOrder.push(1);undefined';jsurlAnchor.click();
                jsurlAnchor.href='javascript:jsurlOrder.push(2);undefined';jsurlAnchor.click();jsurlOrder.length===0)JS"));
        js_tick(f.context);
        CHECK("javascript tasks drain in order", value(f.context, "jsurlOrder.join(',')==='1,2'"));
        CHECK("a javascript task may queue another task without executing it recursively", value(f.context,
            R"JS(jsurlAnchor.href='javascript:jsurlOrder.push(3);location.assign("javascript:jsurlOrder.push(4);undefined");undefined';
                jsurlAnchor.click();true)JS"));
        js_tick(f.context);
        CHECK("new javascript tasks wait for the next native delivery pass", value(f.context, "jsurlOrder.join(',')==='1,2,3'"));
        js_tick(f.context);
        CHECK("the next delivery pass executes the newly queued task", value(f.context, "jsurlOrder.join(',')==='1,2,3,4'"));
    }
    {
        const struct { const char *policy; bool allowed; } cases[] = {
            {"script-src 'none'", false},
            {"default-src 'none'", false},
            {"script-src 'unsafe-eval'", false},
            {"script-src 'unsafe-inline'", true},
            {"script-src 'unsafe-inline' 'strict-dynamic'", false},
            {"script-src 'nonce-known' 'unsafe-inline'", false},
            {"script-src-elem 'none'; script-src-attr 'unsafe-inline'; script-src 'unsafe-eval'", false},
            {"script-src-elem 'unsafe-inline'; script-src-attr 'none'; script-src 'none'", true},
            {"script-src 'unsafe-inline'\nscript-src 'none'", false},
            {"script-src 'sha256-KYYCpZGA0SH7jRsNZt1D6Kc+HobGhSNSOcHIeC+Fe3Y='", false},
            {"script-src 'unsafe-hashes' 'sha256-KYYCpZGA0SH7jRsNZt1D6Kc+HobGhSNSOcHIeC+Fe3Y='", true},
            {"script-src 'unsafe-hashes' 'sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA='", false},
        };
        for (const auto &entry : cases) {
            Fixture f;
            f.document->csp = xstrdup(entry.policy);
            CHECK("CSP fixture queues javascript without synchronously running it", value(f.context,
                R"JS(globalThis.jsurlPolicyAnchor=document.body.appendChild(document.createElement('a'));
                    jsurlPolicyAnchor.setAttribute('nonce','known');
                    jsurlPolicyAnchor.href='javascript:globalThis.jsurlPolicyRan=1;undefined';jsurlPolicyAnchor.click();
                    typeof jsurlPolicyRan==='undefined')JS"));
            js_tick(f.context);
            CHECK(entry.policy, value(f.context, entry.allowed ? "jsurlPolicyRan===1" : "typeof jsurlPolicyRan==='undefined'"));
        }
    }
    {
        Fixture f;
        CHECK("javascript source policy is captured when the request is queued", value(f.context,
            "globalThis.jsurlPolicySnapshot=document.body.appendChild(document.createElement('a'));true"));
        f.document->csp = xstrdup("script-src 'none'");
        CHECK("blocked source policy does not execute synchronously", value(f.context,
            R"JS(jsurlPolicySnapshot.href='javascript:globalThis.jsurlOldPolicy=1;undefined';jsurlPolicySnapshot.click();
                typeof jsurlOldPolicy==='undefined')JS"));
        free(f.document->csp); f.document->csp = nullptr;
        js_tick(f.context);
        CHECK("removing the original source policy cannot authorize an already queued script", value(f.context,
            "typeof jsurlOldPolicy==='undefined'"));
        CHECK("a later policy does not change the captured navigation request", value(f.context,
            R"JS(jsurlPolicySnapshot.href='javascript:globalThis.jsurlNewPolicy=1;undefined';jsurlPolicySnapshot.click();true)JS"));
        f.document->csp = xstrdup("script-src 'none'");
        js_tick(f.context);
        CHECK("the cloned source policy remains authoritative after queueing", value(f.context,
            "jsurlNewPolicy===1"));
    }
    {
        Fixture f;
        JsCtx *same = f.frame("https://sender.test/child");
        CHECK("same-origin destination has a real native child context", same != nullptr);
        if (same) {
            { JS_ENTER(f.context);
              CHECK("same-origin javascript navigation is consumed by the native queue",
                js_windows_navigate(f.context, same, "javascript:globalThis.jsurlChild=1;undefined", false)); }
            CHECK("child javascript does not execute in the caller's synchronous task", value(same, "typeof jsurlChild==='undefined'"));
            js_tick(same);
            CHECK("queued child navigation executes in the destination realm", value(same, "jsurlChild===1") &&
                value(f.context, "typeof jsurlChild==='undefined'"));
            same->doc->csp = xstrdup("script-src 'none'");
            { JS_ENTER(f.context);
              CHECK("same-origin javascript retains the initiating document's policy",
                js_windows_navigate(f.context, same, "javascript:globalThis.jsurlSourcePolicy=1;undefined", false)); }
            js_tick(same);
            CHECK("a different destination policy cannot replace the captured source policy", value(same,
                "jsurlSourcePolicy===1"));
        }
        JsCtx *cross = f.frame("https://different.test/child");
        CHECK("cross-origin destination has a real native child context", cross != nullptr);
        if (cross) {
            { JS_ENTER(f.context); v8::TryCatch caught(iso);
              (void)js_windows_navigate(f.context, cross, "javascript:globalThis.jsurlCross=1;undefined", false); }
            js_tick(cross);
            CHECK("cross-origin javascript navigation cannot execute in the target", value(cross, "typeof jsurlCross==='undefined'"));
        }
        const std::string sandbox = "allow-scripts";
        JsCtx *opaque = f.frame("https://sender.test/opaque", &sandbox);
        CHECK("sandbox fixture has its own opaque principal", opaque && opaque->opaque_origin);
        if (opaque) {
            { JS_ENTER(f.context); v8::TryCatch caught(iso);
              (void)js_windows_navigate(f.context, opaque, "javascript:globalThis.jsurlOpaqueParent=1;undefined", false); }
            CHECK("an opaque document may queue its own javascript navigation", value(opaque,
                R"JS(location.assign('javascript:globalThis.jsurlOpaqueSelf=1;undefined');typeof jsurlOpaqueSelf==='undefined')JS"));
            js_tick(opaque);
            CHECK("opaque self identity is retained without accepting another principal", value(opaque,
                "jsurlOpaqueSelf===1 && typeof jsurlOpaqueParent==='undefined'"));
        }
        const std::string no_scripts = "allow-same-origin";
        JsCtx *blocked = f.frame("https://sender.test/no-scripts", &no_scripts);
        CHECK("script-disabled sandbox retains its native restriction", blocked && blocked->sb_noscript);
        if (blocked) {
            { JS_ENTER(f.context);
              (void)js_windows_navigate(f.context, blocked, "javascript:globalThis.jsurlSandbox=1;undefined", false); }
            js_tick(blocked);
            CHECK("javascript URL cannot bypass a script-disabled sandbox", value(blocked, "typeof jsurlSandbox==='undefined'"));
        }
    }
    {
        Fixture f;
        CHECK("a queued javascript URL can be cancelled by actual frame removal", value(f.context,
            R"JS(globalThis.jsurlRemoved=false;globalThis.jsurlFrame=document.body.appendChild(document.createElement('iframe'));
                jsurlFrame.contentWindow.location.assign('javascript:parent.jsurlRemoved=true;undefined');
                jsurlFrame.remove();!jsurlRemoved)JS"));
        js_tick(f.context);
        CHECK("removed frames never execute their queued javascript task", value(f.context, "!jsurlRemoved"));
    }
    {
        Fixture f;
        CHECK("a javascript request waits while its document epoch is current", value(f.context,
            R"JS(location.assign('javascript:globalThis.jsurlOldEpoch=1;undefined');typeof jsurlOldEpoch==='undefined')JS"));
        const JsWindowId window = f.context->window_id;
        const uint64_t old_epoch = f.context->window_epoch;
        /* The native commit hook is the same lifecycle boundary used by the
         * host when it replaces a document. Reattaching this fixture isolates
         * queue invalidation without a network/navigation test helper. */
        (void)js_window_commit(window, f.document, JS_COOP_UNSAFE_NONE);
        js_windows_attach(f.context);
        CHECK("document commit advances the native destination epoch", f.context->window_id == window &&
            f.context->window_epoch != old_epoch);
        js_tick(f.context);
        CHECK("a previous epoch's executable URL is purged on document commit", value(f.context,
            "typeof jsurlOldEpoch==='undefined'"));
        CHECK("a replacement epoch can admit new javascript work", value(f.context,
            R"JS(location.assign('javascript:globalThis.jsurlNewEpoch=1;undefined');typeof jsurlNewEpoch==='undefined')JS"));
        js_tick(f.context);
        CHECK("the replacement epoch executes only its newly queued URL", value(f.context,
            "jsurlNewEpoch===1 && typeof jsurlOldEpoch==='undefined'"));
    }
    {
        Fixture f;
        CHECK("the native navigation queue has bounded admission", value(f.context,
            R"JS((()=>{let quota=false;for(let i=0;i<300;i++){try{location.assign('javascript:void 0');}
                catch(error){quota=error instanceof DOMException && error.name==='QuotaExceededError';break;}}return quota;})())JS"));
        js_tick(f.context);
        CHECK("delivered javascript requests release their queue reservation", value(f.context,
            R"JS(location.assign('javascript:globalThis.jsurlAfterQuota=1;undefined');true)JS"));
        js_tick(f.context);
        CHECK("new javascript work runs after the bounded queue drains", value(f.context, "jsurlAfterQuota===1"));
    }
    {
        /* The upstream detached-Blob test begins with a parsed iframe and
         * obtains it through its Window named property. Dynamic frame tests
         * did not exercise wrappers created during the parent's bootstrap. */
        Document *parsed_document = doc_new("https://sender.test/start");
        const char *html = "<!doctype html><html><body>"
            "<iframe id='parsedFrame'></iframe>"
            "<iframe id='srcdocFrame' srcdoc='<!doctype html><p id=inside>ready</p>'></iframe>"
            "<div id='namedElement'></div></body></html>";
        html_parse(parsed_document, html, strlen(html));
        JsHost host{};
        JsCtx *parsed_context = js_new(parsed_document, &host);
        CHECK("parsed-iframe document initializes its JavaScript context",
            parsed_context && !parsed_context->ctx.IsEmpty());
        if (parsed_context && !parsed_context->ctx.IsEmpty()) {
            CHECK("parsed iframe IDs resolve to their original DOM elements", value(parsed_context,
                R"JS(globalThis.parsedNode=document.getElementById('parsedFrame');
                    globalThis.srcdocNode=document.getElementById('srcdocFrame');
                    parsedNode===globalThis.parsedFrame && srcdocNode===globalThis.srcdocFrame)JS"));
            CHECK("parsed blank iframe has the HTMLIFrameElement interface", value(parsed_context,
                "parsedNode instanceof HTMLIFrameElement"));
            CHECK("parsed srcdoc iframe has the HTMLIFrameElement interface", value(parsed_context,
                "srcdocNode instanceof HTMLIFrameElement"));
            CHECK("parsed iframe retains its specialized prototype", value(parsed_context,
                "Object.getPrototypeOf(parsedNode)===HTMLIFrameElement.prototype"));
            CHECK("parsed iframe inherits DOM methods and attribute reflection", value(parsed_context,
                "parsedNode.getAttribute('id')==='parsedFrame' && parsedNode.tagName==='IFRAME'"));
            CHECK("parsed iframe retains its document and connected state", value(parsed_context,
                "parsedNode.ownerDocument===document && parsedNode.isConnected"));
            CHECK("parsed iframe contentWindow exposes a real child global", value(parsed_context,
                R"JS(globalThis.parsedChild=parsedNode.contentWindow;
                    parsedChild!==null && parsedChild.window===parsedChild)JS"));
            CHECK("parsed iframe contentDocument matches its child realm", value(parsed_context,
                "parsedNode.contentDocument===parsedChild.document"));
            CHECK("parsed iframe keeps independent realm constructors", value(parsed_context,
                "parsedChild.Array!==Array && parsedChild.document instanceof parsedChild.HTMLDocument"));
            CHECK("child frameElement returns the same parsed DOM wrapper", value(parsed_context,
                "parsedChild.frameElement===parsedNode"));
            CHECK("parsed child preserves its same-origin parent relationship", value(parsed_context,
                "parsedChild.parent===window"));
            CHECK("parsed child exposes its own Blob constructor", value(parsed_context,
                "typeof parsedChild.Blob==='function' && parsedChild.Blob!==Blob"));
            CHECK("parsed srcdoc content remains accessible through its DOM interface", value(parsed_context,
                "srcdocNode.contentDocument.getElementById('inside').textContent==='ready'"));
            CHECK("ordinary named elements keep their DOM interface", value(parsed_context,
                "namedElement instanceof HTMLDivElement && namedElement.ownerDocument===document"));
            CHECK("child lookups do not replace the parsed iframe wrapper", value(parsed_context,
                "document.getElementById('parsedFrame')===parsedNode && globalThis.parsedFrame===parsedNode"));
            CHECK("dynamic iframe creation still exposes the same public interfaces", value(parsed_context,
                R"JS((()=>{const frame=document.body.appendChild(document.createElement('iframe'));
                    const okay=frame instanceof HTMLIFrameElement && frame.contentWindow.frameElement===frame;
                    frame.remove();return okay;})())JS"));
            CHECK("window messaging still rejects genuine noncloneable DOM interfaces", value(parsed_context,
                R"JS((()=>{try{window.postMessage(new Event('probe'),'https://sender.test');}
                    catch(error){return error instanceof DOMException && error.name==='DataCloneError';}
                    return false;})())JS"));
        }
        if (parsed_context) js_free(parsed_context);
        doc_free(parsed_document);
    }
    {
        Fixture f;
        CHECK("detached Blob fixture captures both real allocation realms", value(f.context,
            R"JS(globalThis.blobFrame=document.body.appendChild(document.createElement('iframe'));
                globalThis.ChildBlob=blobFrame.contentWindow.Blob;
                globalThis.ChildBuffer=blobFrame.contentWindow.ArrayBuffer;
                globalThis.childSlice=ChildBlob.prototype.slice;
                globalThis.childText=ChildBlob.prototype.text;
                globalThis.parentBlob=new Blob(['foobar']);
                globalThis.childBlob=new ChildBlob(['abcdef']);
                globalThis.retainedChildDocument=blobFrame.contentDocument;
                globalThis.retainedChildElement=retainedChildDocument.body.appendChild(retainedChildDocument.createElement('p'));
                retainedChildElement.setAttribute('data-retained','owner-live');
                globalThis.retainedChildGetAttribute=retainedChildElement.getAttribute;
                globalThis.detachedChildTimeoutRan=false;globalThis.detachedChildRafRan=false;
                blobFrame.contentWindow.setTimeout(()=>{detachedChildTimeoutRan=true;},0);
                blobFrame.contentWindow.requestAnimationFrame(()=>{detachedChildRafRan=true;});
                typeof childSlice==='function' && ChildBlob!==Blob)JS"));
        JsCtx *retained_child = f.context->kids.size()==1 ? f.context->kids.front() : nullptr;
        CHECK("detached Blob fixture queues native timeout and animation work before removal",
              retained_child && retained_child->timers.size()==1 && retained_child->rafs.size()==1);
        CHECK("Blob fixture removes its iframe through the real DOM", value(f.context,
              "blobFrame.remove();blobFrame.parentNode===null && !blobFrame.isConnected"));
        js_tick(f.context);
        bool owner_still_retained = false;
        for (JsCtx *child : f.context->kids) if (child==retained_child) owner_still_retained=true;
        const bool detached_owner_safe = owner_still_retained && retained_child->dead &&
            retained_child->doc && retained_child->frame_el &&
            !(retained_child->frame_el->flags & NF_CONNECTED) &&
            js_frame_ctx(retained_child->frame_el)==nullptr;
        CHECK("removed iframe is inactive while its reachable realm retains a safe native owner",
              detached_owner_safe && value(f.context, "window.length===0 && blobFrame.contentWindow===null"));
        CHECK("removed iframe cancels its pending native timeout and animation work",
              detached_owner_safe && retained_child->timers.empty() && retained_child->rafs.empty() &&
              retained_child->anims.empty());
        CHECK("a retained child native-bound DOM method remains safe after frame deactivation",
              detached_owner_safe && value(f.context,
              "retainedChildGetAttribute.call(retainedChildElement,'data-retained')==='owner-live' && retainedChildElement.ownerDocument===retainedChildDocument"));
        // The detached realm is intentionally reachable through the values
        // above. Native-owner reclamation is a separate weak-lifetime concern;
        // deleting it here would leave arbitrary retained native callbacks with
        // dangling JsCtx/Document pointers. Guard all borrowed-method probes so
        // a future ownership regression fails instead of dereferencing one.
        const auto retained_value = [&](const char *source) {
            return detached_owner_safe && value(f.context, source);
        };
        CHECK("a detached borrowed slice allocates in its receiver's Blob realm", retained_value(
            R"JS(globalThis.parentSlice=childSlice.call(parentBlob,1,3);
                Object.getPrototypeOf(parentSlice)===Blob.prototype && parentSlice instanceof Blob)JS"));
        CHECK("the reverse borrowed slice retains the child receiver's Blob realm", retained_value(
            R"JS(globalThis.childResult=Blob.prototype.slice.call(childBlob,1,3);
                Object.getPrototypeOf(childResult)===ChildBlob.prototype && childResult instanceof ChildBlob)JS"));
        CHECK("a retained detached Blob constructor can encode new string bytes", retained_value(
            R"JS(globalThis.afterDetached=new ChildBlob(['after']);afterDetached.size===5)JS"));
        CHECK("detached Blob read methods settle through live realm resources", retained_value(
            R"JS(globalThis.blobReadResults={};
                parentSlice.text().then(v=>blobReadResults.text=v,()=>blobReadResults.text='ERROR');
                parentSlice.arrayBuffer().then(v=>blobReadResults.buffer=v,()=>blobReadResults.buffer=null);
                parentSlice.bytes().then(v=>blobReadResults.bytes=v,()=>blobReadResults.bytes=null);
                const reader=parentSlice.stream().getReader();
                reader.read().then(v=>{blobReadResults.stream=v;return reader.read();})
                    .then(v=>blobReadResults.done=v.done,()=>blobReadResults.done=false);
                childResult.arrayBuffer().then(v=>blobReadResults.childBuffer=v,()=>blobReadResults.childBuffer=null);
                childText.call(parentBlob).then(v=>blobReadResults.borrowedText=v,()=>blobReadResults.borrowedText='ERROR');
                afterDetached.text().then(v=>blobReadResults.newText=v,()=>blobReadResults.newText='ERROR');true)JS"));
        for (int i=0;i<10;++i) js_tick(f.context);
        CHECK("removed child timeout and animation callbacks never run during later native ticks",
              value(f.context, "detachedChildTimeoutRan===false && detachedChildRafRan===false"));
        CHECK("detached borrowed slice preserves its selected text", retained_value("blobReadResults.text==='oo'"));
        CHECK("sliced buffers use the receiver's parent realm", retained_value(
            "blobReadResults.buffer instanceof ArrayBuffer && new Uint8Array(blobReadResults.buffer).join(',')==='111,111'"));
        CHECK("sliced bytes preserve the exact byte interval", retained_value(
            "blobReadResults.bytes instanceof Uint8Array && blobReadResults.bytes.join(',')==='111,111'"));
        CHECK("sliced streams deliver one finite byte interval", retained_value(
            "blobReadResults.stream && !blobReadResults.stream.done && blobReadResults.stream.value.join(',')==='111,111' && blobReadResults.done===true"));
        CHECK("reverse sliced buffers use the receiver's retained child realm", retained_value(
            "blobReadResults.childBuffer instanceof ChildBuffer && !(blobReadResults.childBuffer instanceof ArrayBuffer) && new Uint8Array(blobReadResults.childBuffer).join(',')==='98,99'"));
        CHECK("detached borrowed text decoding works with its inactive retained document", retained_value(
            "blobReadResults.borrowedText==='foobar' && blobReadResults.newText==='after'"));
        CHECK("Blob slice does not read a mutable constructor property", retained_value(
            R"JS(Object.defineProperty(parentBlob,'constructor',{get(){throw new Error('constructor read');}});
                Object.getPrototypeOf(childSlice.call(parentBlob,0,1))===Blob.prototype)JS"));
        CHECK("Blob slice uses intrinsic byte length instead of an exposed buffer shadow", retained_value(
            R"JS(Object.defineProperty(parentBlob,'_buf',{value:new ArrayBuffer(0),configurable:true});
                childSlice.call(parentBlob,1,3).size===2)JS"));
        CHECK("changing a genuine Blob prototype does not change its slice realm", retained_value(
            R"JS(Object.setPrototypeOf(parentBlob,null);
                Object.getPrototypeOf(childSlice.call(parentBlob,0,2))===Blob.prototype)JS"));
        CHECK("a Blob prototype impostor has no native slice state", retained_value(
            R"JS((()=>{try{Blob.prototype.slice.call(Object.create(Blob.prototype));}
                catch(e){return e instanceof TypeError;}return false;})())JS"));
        CHECK("a public buffer property cannot forge a Blob slice receiver", retained_value(
            R"JS((()=>{try{Blob.prototype.slice.call({_buf:new ArrayBuffer(3),size:3});}
                catch(e){return e instanceof TypeError;}return false;})())JS"));
        CHECK("a Proxy does not acquire its target's native Blob brand", retained_value(
            R"JS((()=>{try{Blob.prototype.slice.call(new Proxy(new Blob(['abc']),{}));}
                catch(e){return e instanceof TypeError;}return false;})())JS"));
        CHECK("Blob subclasses slice to their realm's base Blob interface", retained_value(
            R"JS((()=>{class Sub extends Blob{};const b=new Sub(['abc']).slice();
                return Object.getPrototypeOf(b)===Blob.prototype && !(b instanceof Sub);})())JS"));
        CHECK("File slicing produces a base Blob in the File's realm", retained_value(
            "Object.getPrototypeOf(new File(['abc'],'name').slice())===Blob.prototype"));
        CHECK("Blob slice clamps half endpoints using ties-to-even", retained_value(
            "new Blob(['abcdef']).slice(0.5,1.5).size===2 && new Blob(['abcdef']).slice(1.5,2.5).size===0"));
        CHECK("negative and infinite Blob slice offsets remain bounded", retained_value(
            "new Blob(['abcdef']).slice(-1.5).size===2 && new Blob(['abcdef']).slice(-Infinity,Infinity).size===6"));
        CHECK("Blob slice lowercases only printable ASCII content types", retained_value(
            "new Blob(['a']).slice(0,1,'TEXT/PLAIN').type==='text/plain' && new Blob(['a']).slice(0,1,'x\\u0000y').type==='' && new Blob(['a']).slice(0,1,'\\u00e9').type===''"));
        CHECK("Blob slice converts nullable content type as a DOMString", retained_value(
            "new Blob(['a']).slice(0,1,null).type==='null'"));
        CHECK("Blob slice converts start, end and content type in order", retained_value(
            R"JS((()=>{const order=[];new Blob(['abc']).slice({valueOf(){order.push('start');return 0;}},
                {valueOf(){order.push('end');return 1;}},{toString(){order.push('type');return 'X/Y';}});
                return order.join(',')==='start,end,type';})())JS"));
        CHECK("Blob slice rejects a BigInt numeric endpoint", retained_value(
            R"JS((()=>{try{new Blob(['abc']).slice(1n);}catch(e){return e instanceof TypeError;}return false;})())JS"));
        CHECK("Blob slice rejects a Symbol content type", retained_value(
            R"JS((()=>{try{new Blob(['abc']).slice(0,1,Symbol());}catch(e){return e instanceof TypeError;}return false;})())JS"));
    }
    {
        struct DataCase { const char *name, *url; std::string bytes; bool success; const char *type; };
        const DataCase cases[] = {
            {"data URL excludes the literal fragment from plain response bytes", "data:,payload#fragment", "payload", true, "text/plain;charset=US-ASCII"},
            {"data URL preserves a percent-encoded hash in response bytes", "data:,before%23after#fragment", "before#after", true, nullptr},
            {"data URL retains query text as part of its body", "data:,body?query#fragment", "body?query", true, nullptr},
            {"data URL preserves bytes after percent-decoded NUL", "data:,a%00b#fragment", std::string("a\0b",3), true, nullptr},
            {"base64 data URL preserves binary NUL and excludes its fragment", "data:;base64,YQBi#fragment", std::string("a\0b",3), true, nullptr},
            {"base64 padding is not followed by fragment payload", "data:;base64,eA==#AAAA", "x", true, nullptr},
            {"base64 data marker is ASCII case insensitive", "data:;BASE64,eA==", "x", true, nullptr},
            {"base64 marker permits its specified ASCII spaces", "data:text/plain; base64 ,eA==", "x", true, "text/plain"},
            {"base64 data URL percent-decodes final padding", "data:;base64,eA%3D%3D", "x", true, nullptr},
            {"base64 decoder removes all ASCII whitespace", "data:;base64,%20e%09A%0C%3D%3D%0A", "x", true, nullptr},
            {"base64 decoder accepts omitted final padding", "data:;base64,eA", "x", true, nullptr},
            {"base64 decoder rejects a one-character remainder", "data:;base64,A", "", false, nullptr},
            {"base64 decoder rejects the URL-safe alphabet", "data:;base64,__8=", "", false, nullptr},
            {"base64 decoder rejects percent-decoded NUL", "data:;base64,eA%00==", "", false, nullptr},
            {"base64 decoder rejects misplaced padding", "data:;base64,e=A=", "", false, nullptr},
            {"a comma only inside the fragment cannot start a data body", "data:missing#fragment,body", "", false, nullptr},
            {"data URL supports an empty body before its fragment", "data:,#fragment", "", true, nullptr},
            {"data URL MIME parameters receive the default text type", "data:;charset=utf-8,hello", "hello", true, "text/plain;charset=utf-8"},
            {"a nonfinal base64 token is ordinary MIME metadata", "data:text/plain;base64;charset=utf-8,eA==", "eA==", true, nullptr},
            {"data URL decodes UTF-8 without changing response bytes", "data:,%C3%A9#fragment", "\xc3\xa9", true, nullptr},
            {"data URL preserves malformed percent sequences literally", "data:,%zz%2#fragment", "%zz%2", true, nullptr},
        };
        for (const auto &test : cases) {
            NetRequest *request=net_request_new("GET",test.url);
            NetResponse *response=request?net_fetch_sync(request):nullptr;
            bool okay=response && (test.success ? !response->error && response->status==200 &&
                response->body_len==test.bytes.size() && (!response->body_len ||
                !memcmp(response->body,test.bytes.data(),test.bytes.size())) :
                response->error || response->status!=200);
            if (okay && test.type) {
                const char *type=headers_get(&response->headers,"content-type");
                okay=type && !strcmp(type,test.type);
            }
            if (okay && strchr(test.url,'#')) okay=response->url && strchr(response->url,'#');
            CHECK(test.name,okay);
            if (response) net_response_free(response);
            if (request) net_request_free(request);
        }
    }
    {
        struct TargetCase { const char *name, *fragment, *setup; };
        const TargetCase cases[] = {
            {"initial document fragment selects the matching ID", "foo", "document.body.innerHTML='<p id=foo>ok</p>';globalThis.want=document.body.firstChild;true"},
            {"fragment ID lookup percent-decodes when raw lookup misses", "%66oo", "document.body.innerHTML='<p id=foo>ok</p>';globalThis.want=document.body.firstChild;true"},
            {"raw fragment ID takes precedence over its decoded spelling", "%66oo", "document.body.innerHTML='<p id=foo>decoded</p><p id=\"%66oo\">raw</p>';globalThis.want=document.body.lastChild;true"},
            {"ID targeting takes precedence over an earlier named anchor", "foo", "document.body.innerHTML='<a name=foo>named</a><p id=foo>id</p>';globalThis.want=document.body.lastChild;true"},
            {"fragment targeting selects the first duplicate ID", "foo", "document.body.innerHTML='<p id=foo>first</p><p id=foo>second</p>';globalThis.want=document.body.firstChild;true"},
            {"fragment targeting supports the named HTML anchor fallback", "foo", "document.body.innerHTML='<a name=foo>named</a>';globalThis.want=document.body.firstChild;true"},
            {"fragment targeting decodes UTF-8 Unicode", "%C3%A9", R"JS(globalThis.want=document.body.appendChild(document.createElement('p'));want.id='\u00e9';true)JS"},
            {"fragment UTF-8 decoding retains a leading BOM code point", "%EF%BB%BFfoo", R"JS(globalThis.want=document.body.appendChild(document.createElement('p'));want.id='\ufefffoo';true)JS"},
            {"fragment UTF-8 decoding replaces a truncated sequence once", "%E2%82", R"JS(globalThis.want=document.body.appendChild(document.createElement('p'));want.id='\ufffd';true)JS"},
            {"fragment UTF-8 decoding reprocesses a noncontinuation byte", "%E2%28%A1", R"JS(globalThis.want=document.body.appendChild(document.createElement('p'));want.id='\ufffd(\ufffd';true)JS"},
            {"fragment targeting preserves percent-decoded NUL in IDs", "a%00b", R"JS(globalThis.want=document.body.appendChild(document.createElement('p'));want.id='a\u0000b';true)JS"},
            {"unmatched fragments leave the document target unset", "absent", "document.body.innerHTML='<p id=foo>other</p>';globalThis.want=null;true"},
            {"empty fragments do not target an empty ID", "", "document.body.innerHTML='<p id=\"\">empty</p>';globalThis.want=null;true"},
            {"top fragment with no matching element leaves target unset", "top", "document.body.innerHTML='<p id=foo>other</p>';globalThis.want=null;true"},
            {"document fragment lookup does not enter a shadow tree", "foo", "const host=document.body.appendChild(document.createElement('div'));host.attachShadow({mode:'open'}).innerHTML='<p id=foo>shadow</p>';globalThis.want=null;true"},
        };
        for (const auto &test : cases) {
            Fixture f;
            std::string url=std::string("https://sender.test/start#")+test.fragment;
            free(f.document->url);f.document->url=xstrdup(url.c_str());
            const bool setup=value(f.context,test.setup);
            js_set_ready_state(f.context,1);
            CHECK(test.name,setup && value(f.context,
                "document.querySelector(':target')===want && (want===null || want.matches(':target'))"));
        }
    }
    {
        Fixture f;
        free(f.document->url);f.document->url=xstrdup("https://sender.test/start#foo");
        CHECK("target state is assigned before interactive ready-state listeners", value(f.context,
            R"JS(document.body.innerHTML='<p id=foo>original</p><p id=other>other</p>';
                globalThis.originalTarget=document.body.firstChild;globalThis.readyTarget=false;
                document.addEventListener('readystatechange',()=>{if(document.readyState==='interactive')
                    readyTarget=originalTarget.matches(':target');});true)JS"));
        js_set_ready_state(f.context,1);
        CHECK("interactive listeners observe the selected fragment target", value(f.context,"readyTarget===true"));
        CHECK("an ID mutation does not silently retarget the document", value(f.context,
            "originalTarget.id='changed';document.body.lastChild.id='foo';originalTarget.matches(':target') && !document.body.lastChild.matches(':target')"));
        CHECK("history pushState does not run fragment navigation targeting", value(f.context,
            "history.pushState(null,'','#other');originalTarget.matches(':target') && !document.body.lastChild.matches(':target')"));
        js_set_ready_state(f.context,2);
        CHECK("complete ready state does not reselect an existing target", value(f.context,
            "document.querySelector(':target')===originalTarget"));
    }
    {
        Fixture f;
        CHECK("actual data iframe navigation starts with a retained fragment URL", value(f.context,
            R"JS(globalThis.dataTargetMessages=[];
                addEventListener('message',event=>{if(event.data && event.data.kind==='fragment-probe')
                    dataTargetMessages.push({data:event.data,origin:event.origin});});
                const html='<!doctype html><p id="foo">fragment-ready</p><script>'+ 
                    'addEventListener("load",()=>parent.postMessage({kind:"fragment-probe",target:document.getElementById("foo").matches(":target"),hash:location.hash,tail:document.body.lastChild.tagName},"*"));'+'<\/script>';
                globalThis.dataTargetFrame=document.createElement('iframe');
                dataTargetFrame.src='data:text/html;charset=utf-8,'+encodeURIComponent(html)+'#foo';
                document.body.appendChild(dataTargetFrame);true)JS"));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while (std::chrono::steady_clock::now()<deadline && !value(f.context,"dataTargetMessages.length>0")) {
            net_poll();js_tick(f.context);usleep(1000);
        }
        CHECK("actual data iframe finishes loading and posts one result", value(f.context,"dataTargetMessages.length===1"));
        CHECK("data iframe load handlers observe the initial target selector", value(f.context,
            "dataTargetMessages.length===1 && dataTargetMessages[0].data.target===true"));
        CHECK("data iframe navigation retains its URL fragment", value(f.context,
            "dataTargetMessages.length===1 && dataTargetMessages[0].data.hash==='#foo'"));
        CHECK("data iframe response bytes exclude fragment suffix text", value(f.context,
            "dataTargetMessages.length===1 && dataTargetMessages[0].data.tail==='SCRIPT'"));
        CHECK("fragment navigation does not give a data iframe a tuple origin", value(f.context,
            "dataTargetMessages.length===1 && dataTargetMessages[0].origin==='null'"));
    }
    net_shutdown();
    printf("RESULTS url-dom %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
