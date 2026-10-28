// Actual worker URL bindings; independent of upstream WPT totals.
(() => {
    'use strict';
    const rows = [];
    function test(name, body) {
        try {
            if (body() !== true) throw new Error('Public URL result did not match the expected value');
            rows.push({name, status: 'PASS'});
        } catch (error) {
            rows.push({name, status: 'FAIL', message: String(error && error.stack || error)});
        }
    }
    function throwsTypeError(body) {
        try { body(); return false; } catch (error) { return error instanceof TypeError; }
    }
    test('worker URL constructor requires its input argument', () => throwsTypeError(() => new URL()));
    test('worker URL constructor does not use the worker location as an implicit base', () => throwsTypeError(() => new URL('./relative')));
    test('an explicit undefined base retains no-base URL construction', () => throwsTypeError(() => new URL('./relative', undefined)));
    test('worker URL construction exposes encoded username and password separately', () => {
        const u = new URL('https://user:pa ss@EXAMPLE.test:443/a?q#f');
        return u.username === 'user' && u.password === 'pa%20ss' && u.host === 'example.test' &&
            u.port === '' && u.origin === 'https://example.test' && u.href === 'https://user:pa%20ss@example.test/a?q#f';
    });
    test('worker URL construction resolves a supplied base with native dot normalization', () =>
        new URL('../next?x', 'https://example.test/a/b').href === 'https://example.test/next?x');
    test('worker URL construction uses native IPv4 canonicalization', () =>
        new URL('http://0x7f000001/').hostname === '127.0.0.1');
    test('worker URL construction uses native IPv6 canonicalization', () =>
        new URL('https://[0:0:0:0:0:0:0:1]/').hostname === '[::1]');
    test('worker URL construction resolves file drive paths', () =>
        new URL('../next', 'file:///C:/dir/file').href === 'file:///C:/next');
    test('worker URL construction rejects a NUL byte inside a host', () =>
        throwsTypeError(() => new URL('https://bad\0tail.test/')));
    test('worker URL construction retains NUL as encoded path data', () =>
        new URL('https://example.test/a\0b').pathname === '/a%00b');
    test('worker URL construction extracts the native blob origin', () =>
        new URL('blob:https://example.test/id').origin === 'https://example.test');
    test('worker URL construction normalizes lone surrogates at the USVString boundary', () =>
        new URL('https://example.test/\ud800').href === 'https://example.test/%EF%BF%BD');
    test('worker URL constructor preserves input and base conversion order', () => {
        const calls = [];
        const u = new URL({toString() { calls.push('input'); return './next'; }},
            {toString() { calls.push('base'); return 'https://example.test/'; }});
        return calls.join(',') === 'input,base' && u.href === 'https://example.test/next';
    });
    test('worker URL getters reject prototype impostors and plain objects', () => {
        const getter = Object.getOwnPropertyDescriptor(URL.prototype, 'href').get;
        return throwsTypeError(() => getter.call({})) &&
            throwsTypeError(() => getter.call(Object.create(URL.prototype)));
    });
    test('worker URL stringifiers reject plain objects even with a forged href', () =>
        throwsTypeError(() => URL.prototype.toString.call({href: 'https://example.test/'})) &&
        throwsTypeError(() => URL.prototype.toJSON.call({href: 'https://example.test/'})));
    test('worker URL canParse distinguishes invalid input and a valid explicit base', () =>
        URL.canParse('https://example.test/') && !URL.canParse('./next') &&
        URL.canParse('./next', 'https://example.test/') && !URL.canParse('http://['));
    test('worker URL parse returns a real URL or null without an implicit base', () => {
        const u = URL.parse('./next', 'https://example.test/');
        return u instanceof URL && u.href === 'https://example.test/next' &&
            URL.parse('./next') === null && URL.parse('http://[') === null;
    });
    test('worker URL static parsers require one argument', () =>
        throwsTypeError(() => URL.canParse()) && throwsTypeError(() => URL.parse()));
    test('worker URL static parsers preserve user conversion exceptions', () => {
        const marker = {marker: true}, input = {toString() { throw marker; }};
        for (const parser of [URL.canParse, URL.parse]) {
            try { parser(input); return false; } catch (error) { if (error !== marker) return false; }
        }
        return true;
    });
    test('worker URL static parsers reject Symbol string arguments', () =>
        throwsTypeError(() => URL.canParse(Symbol('input'))) &&
        throwsTypeError(() => URL.parse('https://example.test/', Symbol('base'))));
    test('worker URL parses a provided invalid base even for an absolute input', () =>
        !URL.canParse('https://example.test/', 'not-a-base') &&
        URL.parse('https://example.test/', 'not-a-base') === null);
    test('worker URL interface lengths and class tag reflect the public API', () =>
        URL.length === 1 && URL.canParse.length === 1 && URL.parse.length === 1 &&
        Object.prototype.toString.call(new URL('https://example.test/')) === '[object URL]');
    test('worker URL searchParams retains identity through non-query component changes', () => {
        const u = new URL('https://example.test/?q=kept'), p = u.searchParams;
        u.pathname = '/next'; u.hash = 'fragment';
        return p === u.searchParams && p.get('q') === 'kept' && u.href === 'https://example.test/next?q=kept#fragment';
    });
    test('worker location fields use the same native URL record', () => {
        const u = new URL(self.location.href);
        return ['href', 'protocol', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'origin']
            .every(key => self.location[key] === u[key]);
    });
    test("worker invalid protocols leave both href and reflected protocol unchanged", () => ['', '0http', 'https\0', 'https ', 'h,tps'].every(input=>{const u=new URL('http://x/');u.protocol=input;return u.href==='http://x/' && u.protocol==='http:';}));
    test("worker protocol setters cannot cross special scheme boundaries", () => (()=>{const a=new URL('http://x/'),b=new URL('custom://x/');a.protocol='custom';b.protocol='https';return a.href==='http://x/' && b.href==='custom://x/';})());
    test("worker protocol mutation preserves an opaque path", () => (()=>{const u=new URL('data:text/plain,hello');u.protocol='custom';return u.href==='custom:text/plain,hello' && u.pathname==='text/plain,hello' && u.host==='';})());
    test("worker protocol parsing normalizes case and ignores text after its first colon", () => (()=>{const u=new URL('http://x/');u.protocol='H\r\nTT\tPS:ignored space';return u.href==='https://x/' && u.protocol==='https:';} )());
    test("worker changing protocol removes a port that becomes the new default", () => (()=>{const u=new URL('http://x:443/');u.protocol='https';return u.href==='https://x/' && u.port==='' && u.origin==='https://x';})());
    test("worker file protocol restrictions preserve credentials and empty file hosts", () => (()=>{const a=new URL('http://u:p@x:81/'),b=new URL('file:///a');a.protocol='file';b.protocol='https';return a.href==='http://u:p@x:81/' && b.href==='file:///a';})());
    test("worker a password can exist without a username", () => (()=>{const u=new URL('https://x/');u.password='secret';return u.href==='https://:secret@x/' && u.username==='' && u.password==='secret';})());
    test("worker clearing a username retains its password", () => (()=>{const u=new URL('https://user:secret@x/');u.username='';return u.href==='https://:secret@x/' && u.password==='secret';})());
    test("worker clearing a password retains its username", () => (()=>{const u=new URL('https://user:secret@x/');u.password='';return u.href==='https://user@x/' && u.username==='user' && u.password==='';})());
    test("worker credential setters encode controls and delimiters without stripping tabs", () => ['username','password'].every(key=>{const u=new URL('https://x/');u[key]='a\0\t\n\r:@/é';return u[key]==='a%00%09%0A%0D%3A%40%2F%C3%A9';}));
    test("worker file opaque and empty-host URLs reject credentials", () => ['file://x/a','data:hello','custom:///a'].every(href=>{const u=new URL(href);u.username='user';u.password='pass';return u.href===href && u.username==='' && u.password==='';}));
    test("worker host and hostname setters preserve an unspecified existing port", () => ['host','hostname'].every(key=>{const u=new URL('http://old.test:8080/a');u[key]='new.test';return u.href==='http://new.test:8080/a' && u.host==='new.test:8080' && u.port==='8080';}));
    test("worker host commits before an invalid later port without discarding the old port", () => (()=>{const u=new URL('http://old.test:8080/a');u.host='new.test:invalid';return u.href==='http://new.test:8080/a' && u.hostname==='new.test' && u.port==='8080';})());
    test("worker host commits before an out-of-range later port", () => (()=>{const u=new URL('http://old.test/a');u.host='new.test:65536';return u.href==='http://new.test/a' && u.host==='new.test' && u.port==='';})());
    test("worker hostname with a port delimiter is rejected before changing host", () => (()=>{const u=new URL('http://old.test:8080/a');u.hostname='new.test:80';return u.href==='http://old.test:8080/a' && u.hostname==='old.test';})());
    test("worker rejected hosts cannot poison any reflected URL field", () => ['host','hostname'].every(key=>['bad\0suffix','bad%00suffix','[broken]','bad@host','bad host'].every(input=>{const u=new URL('https://old.test/a');u[key]=input;return u.href==='https://old.test/a' && u.host==='old.test' && u.hostname==='old.test' && u.origin==='https://old.test';})));
    test("worker empty opaque hosts are accepted only without credentials or ports", () => (()=>{const a=new URL('custom://x/a'),b=new URL('custom://u@x/a'),c=new URL('custom://x:3/a');a.host='';b.host='';c.hostname='';return a.href==='custom:///a' && b.href==='custom://u@x/a' && c.href==='custom://x:3/a';})());
    test("worker file host mutation normalizes localhost and permits an empty host", () => (()=>{const a=new URL('file://x/a'),b=new URL('file://x/b');a.host='loc%41lhost';b.hostname='';return a.href==='file:///a' && b.href==='file:///b';})());
    test("worker host mutation uses the shared IPv4 and IPv6 parsers", () => (()=>{const a=new URL('http://x/'),b=new URL('http://x/');a.host='0x7F000001:8080';b.host='[::0:01]:2';return a.href==='http://127.0.0.1:8080/' && b.href==='http://[::1]:2/' && b.hostname==='[::1]';})());
    test("worker host parsing stops at path query and fragment delimiters", () => ['/suffix','?suffix:99','#suffix','\\suffix'].every(suffix=>{const u=new URL('https://old.test/path?q#f');u.host='new.test'+suffix;return u.href==='https://new.test/path?q#f';}));
    test("worker a backslash remains a forbidden opaque-host byte", () => (()=>{const u=new URL('custom://old.test/a');u.host='new.test\\suffix';return u.href==='custom://old.test/a' && u.host==='old.test';})());
    test("worker port setters use the leading decimal run", () => ['8080suffix','8080/stuff','8080?stuff','8080\0tail'].every(input=>{const u=new URL('http://x/a');u.port=input;return u.href==='http://x:8080/a' && u.port==='8080';}));
    test("worker invalid or overflowing ports leave all old fields unchanged", () => ['65536','999999999999999999999999999999999','invalid',' 81','\0'].every(input=>{const u=new URL('http://x:8080/a');u.port=input;return u.href==='http://x:8080/a' && u.host==='x:8080' && u.port==='8080';}));
    test("worker empty and scheme-default port setters clear an explicit port", () => ['','80'].every(input=>{const u=new URL('http://x:8080/a');u.port=input;return u.href==='http://x/a' && u.port==='';}));
    test("worker port strips ASCII tab and newline but a stripped empty input is not a clear", () => (()=>{const u=new URL('https://x:3000/');u.port='\n\t80\n\t80';if(u.port!=='8080')return false;u.port='\t\n\r';return u.port==='8080';})());
    test("worker pathname setters cannot replace opaque paths", () => ['data:hello','mailto:me@example.test','blob:https://x/id'].every(href=>{const u=new URL(href);u.pathname='/replacement';return u.href===href;}));
    test("worker pathname uses special backslash and dot-segment rules", () => (()=>{const u=new URL('https://x/old?q#f');u.pathname='\\a\\%2E\\b\\%2e.\\c';return u.href==='https://x/a/c?q#f';})());
    test("worker pathname question marks and hashes are data instead of new components", () => (()=>{const u=new URL('https://x/old?q#f');u.pathname='a?b#c';return u.href==='https://x/a%3Fb%23c?q#f' && u.search==='?q' && u.hash==='#f';})());
    test("worker pathname preserves trailing controls and spaces by encoding them", () => (()=>{const u=new URL('https://x/');u.pathname='\0end ';return u.href==='https://x/%00end%20' && u.pathname==='/%00end%20';})());
    test("worker non-special hosted URLs can have an empty path", () => (()=>{const a=new URL('custom://x/old'),b=new URL('custom:///old');a.pathname='';b.pathname='';return a.href==='custom://x' && b.href==='custom://' && a.pathname==='' && b.pathname==='';})());
    test("worker hostless hierarchical URLs retain their initial path slash", () => (()=>{const u=new URL('custom:/old');u.pathname='';if(u.href!=='custom:/')return false;u.pathname='next';return u.href==='custom:/next';})());
    test("worker file pathname setters normalize a drive and cannot shorten above it", () => (()=>{const u=new URL('file:///D:/old');u.pathname='C|/a/../../b';return u.href==='file:///C:/b' && u.pathname==='/C:/b';})());
    test("worker hostless double-slash paths keep their serialization disambiguator", () => (()=>{const u=new URL('custom:/old');u.pathname='//next';return u.href==='custom:/.//next' && u.pathname==='//next' && u.host==='';})());
    test("worker search encodes a hash instead of creating a fragment", () => (()=>{const u=new URL('https://x/path#old');u.search='q=#new';return u.href==='https://x/path?q=%23new#old' && u.hash==='#old';})());
    test("worker search uses the special-scheme apostrophe encoding rule", () => (()=>{const a=new URL('https://x/'),b=new URL('custom://x/');a.search="q='";b.search="q='";return a.search==='?q=%27' && b.search==="?q='";})());
    test("worker search removes its marker before stripping tabs and retains an empty marker", () => (()=>{const u=new URL('https://x/');u.search='\t?x';if(u.href!=='https://x/??x')return false;u.search='?';if(u.href!=='https://x/?' || u.search!=='')return false;u.search='';return u.href==='https://x/';})());
    test("worker fragment mutation preserves an opaque URL instead of adding an authority", () => (()=>{const u=new URL('data:text/plain,hello');u.hash='<next>';return u.href==='data:text/plain,hello#%3Cnext%3E' && u.pathname==='text/plain,hello';})());
    test("worker empty fragment values distinguish removal from an empty present fragment", () => (()=>{const u=new URL('https://x/?q#old');u.hash='#';if(u.href!=='https://x/?q#' || u.hash!=='')return false;u.hash='';return u.href==='https://x/?q';})());
    test("worker fragment setters encode controls and trailing spaces without trimming", () => (()=>{const u=new URL('https://x/');u.hash='\0x ';return u.href==='https://x/#%00x%20' && u.hash==='#%00x%20';})());
    test("worker component mutation does not double-encode existing percent escapes", () => (()=>{const u=new URL('https://x/');u.username='%c3%89té';u.pathname='%2f';u.search='%23';u.hash='%20';return u.href==='https://%c3%89t%C3%A9@x/%2f?%23#%20';})());
    test("worker setting search updates the same linked searchParams object", () => (()=>{const u=new URL('https://x/?old=1');const p=u.searchParams;u.search='?new=2';return p===u.searchParams && p.get('old')===null && p.get('new')==='2' && p.size===1;})());
    test("worker setting href updates the existing linked searchParams object", () => (()=>{const u=new URL('https://x/?old=1');const p=u.searchParams;u.href='https://y/?new=2';return p===u.searchParams && p.get('old')===null && p.get('new')==='2';})());
    test("worker a rejected href leaves both URL and linked searchParams unchanged", () => (()=>{const u=new URL('https://x/?kept=1');const p=u.searchParams;try{u.href='/relative';}catch(error){return error instanceof TypeError && u.href==='https://x/?kept=1' && p===u.searchParams && p.get('kept')==='1';}return false;})());
    test("worker linked searchParams changes still serialize back into the URL", () => (()=>{const u=new URL('https://x/');const p=u.searchParams;p.set('q','two words');return u.href==='https://x/?q=two+words' && p===u.searchParams && p.get('q')==='two words';})());
    test("worker component setters convert even ignored inputs exactly once", () => (()=>{const u=new URL('data:hello');let calls=0;u.hostname={toString(){calls++;return 'ignored.test';}};return calls===1 && u.href==='data:hello';})());
    test("worker component setters preserve user conversion exceptions and old state", () => (()=>{const u=new URL('https://x/'),marker={thrown:true};try{u.host={toString(){throw marker;}};}catch(error){return error===marker && u.href==='https://x/';}return false;})());
    test("worker URL setters reject Symbol values at the Web IDL string boundary", () => ['href','protocol','username','password','host','hostname','port','pathname','search','hash'].every(key=>{const u=new URL('https://x/');try{u[key]=Symbol('url');}catch(error){return error instanceof TypeError && u.href==='https://x/';}return false;}));
    test("worker component setters use the current URL after reentrant input conversion", () => (()=>{const u=new URL('http://old.test/');let calls=0;u.port={toString(){calls++;u.href='https://new.test/new';return '8080';}};return calls===1 && u.href==='https://new.test:8080/new';})());
    test("worker clearing opaque URL query data preserves its parsed encoded trailing space", () => (()=>{const u=new URL('data:space ?q=1');const p=u.searchParams;p.delete('q');return u.href==='data:space%20' && u.pathname==='space%20' && p===u.searchParams && p.size===0;})());

    self.addEventListener('message', function receive(event) {
        if (!event.data || event.data.kind !== 'lumen-url-ping') return;
        self.removeEventListener('message', receive);
        test('actual worker URL objects continue to work after parent message delivery', () =>
            new URL('next', 'https://example.test/').href === 'https://example.test/next');
        self.postMessage({kind: 'lumen-url-results', expected: 74, rows});
        self.close();
    });
    self.postMessage({kind: 'lumen-url-ready', completed: rows.length, expected: 74});
})();
