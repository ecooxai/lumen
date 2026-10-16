#include "../src/js/js_int.h"
extern "C" {
#include "../src/text/font.h"
}
#include <cassert>
#include <unistd.h>

static void check(JsCtx *c, const char *code) {
    JS_ENTER(c);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> value;
    bool ok = v8::Script::Compile(ctx, jstr(iso, code)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&value) && value->BooleanValue(iso);
    if (!ok) fprintf(stderr, "check failed: %s\n%s\n", code,
        caught.HasCaught() ? jcstr(iso, caught.Exception()).c_str() : "false result");
    assert(ok);
}
int main(int argc, char **argv) {
    assert(argc == 2); alarm(30);
    dom_init(); net_init(1); font_init(); js_global_init(argv[0]);
    Document *d = doc_new("https://commands.test/");
    const char *html = "<!doctype html><html><body><button id=b type=button command=show-modal commandfor=d><span id=child>Open</span></button><dialog id=d><input id=inside autofocus></dialog><div id=other></div><div id=host></div></body></html>";
    html_parse(d, html, strlen(html)); JsHost host{}; JsCtx *c = js_new(d, &host);
    check(c, "globalThis.b=document.getElementById('b');globalThis.d=document.getElementById('d');globalThis.other=document.getElementById('other');true");
    const char *mode = argv[1];
    if (!strcmp(mode, "native")) {
        check(c, "globalThis.events=[];d.addEventListener('command',e=>events.push([e.command,e.source===b,e.cancelable,e.bubbles,e.isTrusted]));d.addEventListener('beforetoggle',e=>events.push([e.oldState,e.newState,e.source===b,e.cancelable]));true");
        assert(js_dispatch(c, doc_get_element_by_id(d,"child"), "click", "PointerEvent", true,true,20,20,0,nullptr));
        check(c, "d.open && d.matches(':modal') && document.activeElement.id==='inside' && JSON.stringify(events)==='[[\"show-modal\",true,true,false,true],[\"closed\",\"open\",true,true]]'");
    } else if (!strcmp(mode, "cancel")) {
        check(c, "(()=>{const cancel=e=>e.preventDefault();b.addEventListener('click',cancel);b.click();if(d.open)return false;b.removeEventListener('click',cancel);d.addEventListener('command',cancel);b.click();if(d.open)return false;d.removeEventListener('command',cancel);d.addEventListener('beforetoggle',cancel);b.click();if(d.open)return false;d.removeEventListener('beforetoggle',cancel);b.disabled=true;b.click();if(d.open)return false;b.disabled=false;b.click();return d.open})()");
    } else if (!strcmp(mode, "close")) {
        check(c, "(()=>{b.click();d.returnValue='old';b.command='request-close';let no=e=>e.preventDefault();d.addEventListener('cancel',no);b.click();if(!d.open||d.returnValue!=='old')return false;d.removeEventListener('cancel',no);b.value='yes';b.click();if(d.open||d.matches(':modal')||d.returnValue!=='yes')return false;b.command='show-modal';b.click();b.command='close';b.removeAttribute('value');b.click();return !d.open&&d.returnValue==='yes'})()");
    } else if (!strcmp(mode, "properties")) {
        check(c, "(()=>{if(b.commandForElement!==d||b.command!=='show-modal')return false;b.command='ShOw-MoDaL';if(b.command!=='show-modal'||b.getAttribute('command')!=='ShOw-MoDaL')return false;b.command='unknown';if(b.command!=='')return false;b.command='--Mixed';if(b.command!=='--Mixed')return false;b.commandForElement=other;if(b.commandForElement!==other||b.getAttribute('commandfor')!=='')return false;b.setAttribute('commandfor','d');if(b.commandForElement!==d)return false;b.commandForElement=null;if(b.commandForElement!==null)return false;try{b.commandForElement={};return false}catch(e){return e instanceof TypeError}})()");
    } else if (!strcmp(mode, "custom")) {
        check(c, "(()=>{b.commandForElement=other;b.command='--Mixed';let n=0;other.addEventListener('command',e=>{if(e.command==='--Mixed'&&e.source===b&&!e.bubbles)n++});b.click();b.command='close';b.click();return n===1&&!d.open})()");
    } else if (!strcmp(mode, "form")) {
        check(c, "(()=>{b.type='submit';b.click();if(!d.open)return false;d.close();b.type='reset';b.click();if(!d.open)return false;d.close();const f=document.createElement('form');document.body.append(f);f.append(b);let submits=0;f.addEventListener('submit',e=>{e.preventDefault();submits++});b.type='submit';b.click();if(submits!==1||d.open)return false;b.removeAttribute('type');if(b.type!=='button')return false;b.click();if(d.open)return false;b.type='button';b.click();return d.open&&submits===1})()");
    } else if (!strcmp(mode, "shadow")) {
        check(c, "(()=>{const root=document.getElementById('host').attachShadow({mode:'open'});root.innerHTML='<button command=show-modal commandfor=d></button><dialog id=d></dialog>';const btn=root.querySelector('button'),dlg=root.querySelector('dialog');btn.click();if(!dlg.open||d.open)return false;btn.commandForElement=d;btn.click();return d.open&&dlg.open})()");
    } else if (!strcmp(mode, "lifecycle")) {
        check(c, "(()=>{const x=document.createElement('dialog');try{x.showModal();return false}catch(e){if(e.name!=='InvalidStateError')return false}d.show();try{d.showModal();return false}catch(e){if(e.name!=='InvalidStateError')return false}d.close();d.showModal();d.showModal();try{d.show();return false}catch(e){if(e.name!=='InvalidStateError')return false}d.removeAttribute('open');if(d.matches(':modal'))return false;d.showModal();d.remove();document.body.append(d);return !d.matches(':modal')})()");
    } else if (!strcmp(mode, "modal-order")) {
        check(c, "globalThis.second=document.createElement('dialog');document.body.append(second);second.showModal();d.showModal();second.showModal();true");
        assert(d->modals.n == 2 && d->modals.v[1] == doc_get_element_by_id(d, "d"));
        check(c, "d.close();second.close();d.show();true");
        assert(!d->modals.n);
        check(c, "d.close();d.showModal();second.showModal();document.body.remove();true");
        assert(!d->modals.n);
    } else if (!strcmp(mode, "events")) {
        check(c, "(()=>{const e=new CommandEvent('command',{command:'--x',source:b}),t=new ToggleEvent('toggle',{oldState:'closed',newState:'open',source:b});return e instanceof Event&&e.command==='--x'&&e.source===b&&t.oldState==='closed'&&t.newState==='open'&&t.source===b&&new CommandEvent('x').source===null&&new ToggleEvent('x').oldState===''})()");
    } else if (!strcmp(mode, "reentrant")) {
        check(c, "(()=>{d.addEventListener('command',()=>d.remove(),{once:true});b.click();if(d.open)return false;document.body.append(d);d.addEventListener('beforetoggle',()=>d.remove(),{once:true});b.click();return !d.open})()");
    } else {
        assert(!strcmp(mode, "toggle"));
        check(c, "globalThis.states=[];d.addEventListener('toggle',e=>states.push([e.oldState,e.newState,e.source===b]));globalThis.closed=0;d.addEventListener('close',()=>closed++);b.click();b.command='close';b.click();states.length===0&&closed===0");
        for(int i=0;i<5;i++){usleep(1000);js_tick(c);}
        check(c,"JSON.stringify(states)==='[[\"closed\",\"closed\",true]]'&&closed===1");
    }
    js_release_pins(); js_free(c); doc_free(d); net_shutdown(); printf("%s passed\n",mode);
}
