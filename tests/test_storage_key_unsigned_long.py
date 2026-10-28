#!/usr/bin/env python3
import argparse, hashlib, http.server, json, os, subprocess, tempfile, threading
from pathlib import Path
HTML = r'''<!doctype html><meta charset="utf-8"><title>Storage key unsigned long regression</title><script>
'use strict';
const results=[];
function check(name,ok,detail){const row={i:results.length,name,ok:!!ok,detail:detail===undefined?null:detail};results.push(row);console.log('STORAGE_KEY_CHECK '+JSON.stringify(row));}
try {
 for(const kind of ['localStorage','sessionStorage']){
  const s=window[kind];s.clear();for(let i=0;i<4;i++)s.setItem('lume-key-'+i,'value-'+i);
  const keys=[s.key(0),s.key(1),s.key(2),s.key(3)];
  check(kind+' populated length',s.length===4,s.length);
  check(kind+' distinct valid baseline keys',new Set(keys).size===4&&keys.every(k=>/^lume-key-[0-3]$/.test(k)),keys);
  const cases=[
   ['zero',0,0],['one',1,1],['three',3,3],['out of range',4,null],
   ['2^32',4294967296,0],['2^32+1',4294967297,1],['2^32+2',4294967298,2],['2^32+3',4294967299,3],
   ['negative 2^32',-4294967296,0],['negative 2^32 plus1',-4294967295,1],['negative one',-1,null],
   ['positive fraction',1.9,1],['negative fraction',-1.9,null],['NaN',NaN,0],['positive infinity',Infinity,0],['negative infinity',-Infinity,0],
   ['explicit undefined',undefined,0],['null',null,0],['false',false,0],['true',true,1],
   ['large numeric string','4294967298',2],['fraction string','1.9',1],['nonnumeric string','nonnumeric',0],['2^53',9007199254740992,0],['negative zero',-0,0]
  ];
  for(const [label,value,index] of cases){let got,err=null;try{got=s.key(value);}catch(e){err=String(e);}const want=index===null?null:keys[index];check(kind+' '+label,err===null&&got===want,{got,want,err});}
  let e=null;try{s.key();}catch(x){e=x;}check(kind+' missing index TypeError',e instanceof TypeError);
  e=null;try{s.key(1n);}catch(x){e=x;}check(kind+' BigInt TypeError',e instanceof TypeError);
  e=null;try{s.key(Symbol('index'));}catch(x){e=x;}check(kind+' Symbol TypeError',e instanceof TypeError);
  const hints=[];const numeric={[Symbol.toPrimitive](hint){hints.push(hint);return 4294967298;}};let got,err=null;try{got=s.key(numeric);}catch(x){err=String(x);}check(kind+' number-hint conversion once',err===null&&got===keys[2]&&hints.join(',')==='number',{got,hints,err});
  const expected=new Error('coercion failure');e=null;try{s.key({valueOf(){throw expected;}});}catch(x){e=x;}check(kind+' original coercion exception',e===expected);
  check(kind+' reads preserve storage',s.length===4&&keys.every((k,i)=>s.key(i)===k&&s.getItem(k)==='value-'+k.slice(-1)));
 }
}catch(e){check('fixture completed',false,String(e&&e.stack||e));}
const passed=results.filter(r=>r.ok).length;console.log('STORAGE_KEY_RESULTS '+JSON.stringify({total:results.length,passed,failed:results.length-passed}));document.documentElement.setAttribute('data-storage-key-done','1');
</script>'''
EXPECTED=66

def main():
 p=argparse.ArgumentParser();p.add_argument('--binary',required=True,type=Path);args=p.parse_args();binary=args.binary.resolve()
 if not binary.is_file():p.error('binary does not exist')
 class Handler(http.server.BaseHTTPRequestHandler):
  def do_GET(self):
   data=HTML.encode();self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
  def log_message(self,*_):pass
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 try:
  with tempfile.TemporaryDirectory(prefix='lume-storage-key-') as profile:
   env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-storage-key-done')
   result=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'7000'],capture_output=True,text=True,env=env,timeout=12)
 except subprocess.TimeoutExpired as exc:
  print('STORAGE_KEY_INFRA '+json.dumps({'error':'timeout','seconds':exc.timeout}));return 2
 finally:
  server.shutdown();server.server_close();thread.join(timeout=2)
 print(result.stdout,end='');print(result.stderr,end='');rows=[];summaries=[]
 for line in (result.stdout+'\n'+result.stderr).splitlines():
  for marker,target in [('STORAGE_KEY_CHECK ',rows),('STORAGE_KEY_RESULTS ',summaries)]:
   at=line.find(marker)
   if at>=0:
    try:target.append(json.loads(line[at+len(marker):]))
    except json.JSONDecodeError:print('STORAGE_KEY_INFRA malformed JSON');return 2
 if result.returncode!=0 or len(rows)!=EXPECTED or len(summaries)!=1:
  print('STORAGE_KEY_INFRA '+json.dumps({'exit':result.returncode,'checks':len(rows),'summaries':len(summaries)}));return 2
 if [r.get('i') for r in rows]!=list(range(EXPECTED)):print('STORAGE_KEY_INFRA duplicate or missing check index');return 2
 passed=sum(r.get('ok') is True for r in rows);summary=summaries[0]
 if summary.get('total')!=EXPECTED or summary.get('passed')!=passed or summary.get('failed')!=EXPECTED-passed:print('STORAGE_KEY_INFRA inconsistent summary');return 2
 print('STORAGE_KEY_RECEIPT '+json.dumps({'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256(HTML.encode()).hexdigest(),'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}))
 return 0 if passed==EXPECTED else 1
if __name__=='__main__':raise SystemExit(main())
