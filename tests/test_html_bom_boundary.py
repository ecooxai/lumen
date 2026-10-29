#!/usr/bin/env python3
"""Only the byte decoder consumes a BOM; Unicode parser inputs preserve U+FEFF."""
import argparse,hashlib,http.server,json,os,subprocess,tempfile,threading
from pathlib import Path

CASES=[]
for label,codec,bom in [('UTF-8','utf-8',b'\xef\xbb\xbf'),('UTF-16LE','utf-16le',b'\xff\xfe'),('UTF-16BE','utf-16be',b'\xfe\xff')]:
    for count in (1,2,3):
        CASES.append({'name':label+' HTML consumes exactly one of '+str(count)+' BOMs',
                      'raw':bom*count+'<b>é</b>'.encode(codec),'mime':'text/html',
                      'encoding':label,'text':'\ufeff'*(count-1)+'é','plain':False})
    CASES.append({'name':label+' plaintext consumes only its signature',
                  'raw':bom*2+'é'.encode(codec),'mime':'text/plain',
                  'encoding':label,'text':'\ufeffé','plain':True})
CASES.append({'name':'JSON preserves the second UTF8 BOM as text','raw':b'\xef\xbb\xbf'*2+b'{}',
              'mime':'application/json','encoding':'UTF-8','text':'\ufeff{}','plain':True})
PUBLIC=[{k:v for k,v in row.items() if k not in ('raw','mime')} for row in CASES]
EXPECTED=len(CASES)+8
HTML=r'''<!doctype html><meta charset=utf-8><title>BOM byte boundary</title><body><script>
'use strict';
const cases=__CASES__,rows=[];
const eq=(actual,expected)=>{if(actual!==expected)throw new Error('expected '+JSON.stringify(expected)+', got '+JSON.stringify(actual));};
const record=async(name,callback)=>{let row={i:rows.length,name,ok:true};try{await callback();}catch(error){row.ok=false;row.error=error.name+': '+error.message;}rows.push(row);console.log('HTML_BOM_CHECK '+JSON.stringify(row));};
async function frameCheck(source,expected,encoding,plain,srcdoc){const frame=document.createElement('iframe');let timer;
 try{await new Promise((resolve,reject)=>{timer=setTimeout(()=>reject(new Error('frame timeout')),1000);frame.onload=resolve;frame.onerror=()=>reject(new Error('frame error'));if(srcdoc)frame.srcdoc=source;else frame.src=source;document.body.appendChild(frame);});
  const doc=frame.contentDocument;eq(doc.body.textContent,expected);eq(doc.characterSet,encoding);
  if(plain)eq(doc.querySelectorAll('pre').length,1);else eq(doc.querySelectorAll('b').length,1);
 }finally{clearTimeout(timer);frame.remove();}
}
(async()=>{
 for(let index=0;index<cases.length;++index){const row=cases[index];await record(row.name,()=>frameCheck('/case/'+index,row.text,row.encoding,row.plain,false));}
 for(const count of [0,1,2])await record('DOMParser preserves '+count+' Unicode FEFF characters',()=>{
  const doc=new DOMParser().parseFromString('\uFEFF'.repeat(count)+'<b>x</b>','text/html');eq(doc.body.textContent,'\uFEFF'.repeat(count)+'x');eq(doc.querySelectorAll('b').length,1);
 });
 for(const count of [0,1,2])await record('srcdoc preserves '+count+' Unicode FEFF characters',()=>frameCheck('\uFEFF'.repeat(count)+'<b>x</b>','\uFEFF'.repeat(count)+'x','UTF-8',false,true));
 for(const count of [1,2])await record('HTML fragments preserve '+count+' Unicode FEFF characters',()=>{
  const div=document.createElement('div');div.innerHTML='\uFEFF'.repeat(count)+'<b>x</b>';eq(div.textContent,'\uFEFF'.repeat(count)+'x');eq(div.querySelectorAll('b').length,1);
 });
 const passed=rows.filter(row=>row.ok).length;console.log('HTML_BOM_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));document.documentElement.setAttribute('data-html-bom-done','1');
})().catch(error=>console.log('HTML_BOM_INFRA '+error.name+': '+error.message));
</script>'''.replace('__CASES__',json.dumps(PUBLIC,ensure_ascii=True).replace('<','\\u003c'))

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path=self.path.split('?',1)[0]
        if path.startswith('/case/'):
            row=CASES[int(path.removeprefix('/case/'))];mime,data=row['mime'],row['raw']
        else:mime,data='text/html;charset=utf-8',HTML.encode()
        self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*_):pass

def main():
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);a=p.parse_args();binary=a.binary.resolve()
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lume-html-bom-') as profile:
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')}
            env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-html-bom-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'25000'],capture_output=True,text=True,env=env,timeout=30)
    except subprocess.TimeoutExpired as error:print('HTML_BOM_INFRA '+json.dumps({'error':'timeout','seconds':error.timeout}));return 2
    finally:server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];summaries=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,target in [('HTML_BOM_CHECK ',rows),('HTML_BOM_RESULTS ',summaries)]:
            at=line.find(marker)
            if at>=0:target.append(json.loads(line[at+len(marker):]))
    if run.returncode or len(rows)!=EXPECTED or len(summaries)!=1 or [r['i'] for r in rows]!=list(range(EXPECTED)):
        print('HTML_BOM_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'summaries':summaries}));return 2
    passed=sum(r['ok'] is True for r in rows)
    if summaries[0]!={'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}:raise RuntimeError('Inconsistent BOM regression receipt')
    print('HTML_BOM_RECEIPT '+json.dumps({'binarySHA256':hashlib.sha256(binary.read_bytes()).hexdigest(),
        'fixtureSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'htmlSHA256':hashlib.sha256(HTML.encode()).hexdigest(),
        'caseSHA256':[hashlib.sha256(c['raw']).hexdigest() for c in CASES],'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}))
    return 0 if passed==EXPECTED else 1

if __name__=='__main__':raise SystemExit(main())
