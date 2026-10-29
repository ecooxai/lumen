#!/usr/bin/env python3
"""Real HTTP JSON documents: UTF-8 fallback, explicit charset, inert literal markup."""
import argparse,hashlib,http.server,json,os,subprocess,tempfile,threading
from pathlib import Path

CASES=[]
TEXT='{"text":"☃é"}'
def add(name,mime,raw=None,text=TEXT,encoding='UTF-8',plain=True):
    CASES.append({'name':name,'mime':mime,'raw':text.encode() if raw is None else raw,
                  'text':text,'encoding':encoding,'plain':plain})
add('application/json uses UTF8 under a legacy parent','application/json')
add('text/json uses UTF8 under a legacy parent','text/json')
add('case-insensitive JSON essence','APPLICATION/JSON')
add('JSON essence trims HTTP whitespace',' application/json \t; ignored=yes')
add('structured JSON application suffix','application/problem+json')
add('structured JSON text suffix','text/example+json')
add('case-insensitive structured JSON suffix','Application/Example+JSON')
add('explicit UTF8 charset','application/json;charset=utf-8')
add('explicit Windows1251 charset is honored','application/json;charset=windows-1251',
    raw='{"text":"Ж"}'.encode('cp1251'),text='{"text":"Ж"}',encoding='windows-1251')
add('UTF8 BOM precedes explicit transport charset','application/json;charset=windows-1251',raw=b'\xef\xbb\xbf'+TEXT.encode())
add('unknown charset does not inherit legacy parent','application/json;charset=not-an-encoding')
add('HTML metadata and tags are literal JSON text','application/json',text='{"html":"<meta charset=windows-1251><b>é</b>&amp;"}')
add('script-looking JSON data does not execute','application/json',text='{"html":"<script>parent.__jsonDocumentExecuted++</script>"}')
add('leading newline is preserved in JSON text','application/json',text='\n'+TEXT+'\n')
add('plaintext fallback still inherits eligible parent','text/plain',raw='Ж'.encode('cp1251'),text='Ж',encoding='windows-1251')
add('HTML still parses markup and metadata','text/html',raw='<meta charset=utf-8><b>é</b>'.encode(),text='é',plain=False)
PUBLIC=[{k:v for k,v in c.items() if k not in ('raw','mime')} for c in CASES]
HTML=r'''<!doctype html><meta charset=windows-1251><title>JSON document encoding</title><body><script>
'use strict';
const cases=__CASES__,rows=[];
window.__jsonDocumentExecuted=0;
const eq=(actual,expected)=>{if(actual!==expected)throw new Error('expected '+JSON.stringify(expected)+', got '+JSON.stringify(actual));};
(async()=>{
 for(let index=0;index<cases.length;++index){const row=cases[index],frame=document.createElement('iframe');let timer;
  try{
   eq(document.characterSet,'windows-1251');
   await new Promise((resolve,reject)=>{timer=setTimeout(()=>reject(new Error('frame timeout')),1000);frame.onload=resolve;frame.onerror=()=>reject(new Error('frame error'));frame.src='/case/'+index;document.body.appendChild(frame);});
   const doc=frame.contentDocument;
   eq(doc.characterSet,row.encoding);eq(doc.body.textContent,row.text);
   if(row.plain){eq(doc.querySelectorAll('pre').length,1);eq(doc.querySelectorAll('script,b,meta').length,0);eq(doc.compatMode,'CSS1Compat');}
   else eq(doc.querySelectorAll('b').length,1);
   eq(window.__jsonDocumentExecuted,0);
   rows.push({i:index,name:row.name,ok:true});
  }catch(error){rows.push({i:index,name:row.name,ok:false,error:error.name+': '+error.message});}
  finally{clearTimeout(timer);frame.remove();}
  console.log('JSON_DOC_CHECK '+JSON.stringify(rows[rows.length-1]));
 }
 const passed=rows.filter(r=>r.ok).length;
 console.log('JSON_DOC_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));
 document.documentElement.setAttribute('data-json-doc-done','1');
})().catch(error=>console.log('JSON_DOC_INFRA '+error.name+': '+error.message));
</script>'''.replace('__CASES__',json.dumps(PUBLIC,ensure_ascii=True).replace('<','\\u003c'))

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path=self.path.split('?',1)[0]
        if path.startswith('/case/'):
            case=CASES[int(path.removeprefix('/case/'))];mime,data=case['mime'],case['raw']
        else:mime,data='text/html;charset=windows-1251',HTML.encode()
        self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*_):pass

def main():
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);a=p.parse_args();binary=a.binary.resolve()
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lume-json-document-') as profile:
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')}
            env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-json-doc-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'20000'],capture_output=True,text=True,env=env,timeout=25)
    except subprocess.TimeoutExpired as error:print('JSON_DOC_INFRA '+json.dumps({'error':'timeout','seconds':error.timeout}));return 2
    finally:server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];summaries=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,target in [('JSON_DOC_CHECK ',rows),('JSON_DOC_RESULTS ',summaries)]:
            at=line.find(marker)
            if at>=0:target.append(json.loads(line[at+len(marker):]))
    expected=len(CASES)
    if run.returncode or len(rows)!=expected or len(summaries)!=1 or [r['i'] for r in rows]!=list(range(expected)):
        print('JSON_DOC_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'summaries':summaries}));return 2
    passed=sum(r['ok'] is True for r in rows)
    if summaries[0]!={'total':expected,'passed':passed,'failed':expected-passed}:raise RuntimeError('Inconsistent JSON fixture summary')
    print('JSON_DOC_RECEIPT '+json.dumps({'binarySHA256':hashlib.sha256(binary.read_bytes()).hexdigest(),
        'fixtureSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'htmlSHA256':hashlib.sha256(HTML.encode()).hexdigest(),
        'caseSHA256':[hashlib.sha256(c['raw']).hexdigest() for c in CASES],'total':expected,'passed':passed,'failed':expected-passed}))
    return 0 if passed==expected else 1

if __name__=='__main__':raise SystemExit(main())
