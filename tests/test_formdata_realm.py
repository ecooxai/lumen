#!/usr/bin/env python3
"""Thirty-two real-browser FormData realm/brand checks; separate from upstream WPT.

The native/Chromium process transport and1Hz sampler are reused from the frozen
Blob Range fixture. Only this local completion marker changes; no browser API,
realm or document object is mocked.
"""
import argparse
from datetime import datetime,timezone
import hashlib,http.server,importlib.util,json
from pathlib import Path
import threading,time

ROOT=Path(__file__).resolve().parents[1]
MARKER='FORMDATA_REALM_RESULTS '
CASE_MARKER='FORMDATA_REALM_CASE '
SCRIPT=r'''
const frame=document.createElement('iframe');document.body.appendChild(frame);
const child=frame.contentWindow, childDocument=frame.contentDocument;
function form(doc,value){const result=doc.createElement('form'),input=doc.createElement('input');input.name='field';input.value=value;result.appendChild(input);return result;}
const localForm=form(document,'local'), foreignForm=form(childDocument,'foreign');
const localFake=Object.create(HTMLFormElement.prototype),foreignFake=Object.create(child.HTMLFormElement.prototype);
const cases=[];
function valid(name,Ctor,form,value){cases.push([name,()=>{const result=new Ctor(form);return result.get('field')===value&&result.getAll('field').length===1&&Object.getPrototypeOf(result)===Ctor.prototype;}]);}
function rejects(name,Ctor,value,ErrorCtor){cases.push([name,()=>{try{new Ctor(value);return false;}catch(error){return error instanceof ErrorCtor;}}]);}
valid('local constructor / local form',FormData,localForm,'local');
valid('local constructor / foreign form',FormData,foreignForm,'foreign');
valid('foreign constructor / local form',child.FormData,localForm,'local');
valid('foreign constructor / foreign form',child.FormData,foreignForm,'foreign');
cases.push(['local constructor / omitted argument',()=>[...new FormData().entries()].length===0]);
cases.push(['foreign constructor / omitted argument',()=>[...new child.FormData().entries()].length===0]);
rejects('local constructor / local prototype forgery',FormData,localFake,TypeError);
rejects('local constructor / foreign prototype forgery',FormData,foreignFake,TypeError);
rejects('foreign constructor / local prototype forgery',child.FormData,localFake,child.TypeError);
rejects('foreign constructor / foreign prototype forgery',child.FormData,foreignFake,child.TypeError);
rejects('local constructor / plain object',FormData,{},TypeError);
rejects('foreign constructor / plain object',child.FormData,{},child.TypeError);
rejects('local constructor / foreign div',FormData,childDocument.createElement('div'),TypeError);
rejects('foreign constructor / local div',child.FormData,document.createElement('div'),child.TypeError);
rejects('local constructor / null',FormData,null,TypeError);
rejects('foreign constructor / null',child.FormData,null,child.TypeError);

const localData=new FormData(localForm),foreignData=new child.FormData(foreignForm);
const localXhr=new XMLHttpRequest(),foreignXhr=new child.XMLHttpRequest();
const localProgress=new ProgressEvent('progress',{loaded:12}),foreignProgress=new child.ProgressEvent('progress',{loaded:34});
const localReady=Object.getOwnPropertyDescriptor(XMLHttpRequest.prototype,'readyState').get,foreignReady=Object.getOwnPropertyDescriptor(child.XMLHttpRequest.prototype,'readyState').get;
const localLoaded=Object.getOwnPropertyDescriptor(ProgressEvent.prototype,'loaded').get,foreignLoaded=Object.getOwnPropertyDescriptor(child.ProgressEvent.prototype,'loaded').get;
cases.push(['local FormData.get / foreign real object',()=>FormData.prototype.get.call(foreignData,'field')==='foreign']);
cases.push(['foreign FormData.get / local real object',()=>child.FormData.prototype.get.call(localData,'field')==='local']);
cases.push(['local XHR.readyState / foreign real object',()=>localReady.call(foreignXhr)===0]);
cases.push(['foreign XHR.readyState / local real object',()=>foreignReady.call(localXhr)===0]);
cases.push(['local ProgressEvent.loaded / foreign real object',()=>localLoaded.call(foreignProgress)===34]);
cases.push(['foreign ProgressEvent.loaded / local real object',()=>foreignLoaded.call(localProgress)===12]);
function rejectsBorrow(name,call,ErrorCtor){cases.push([name,()=>{try{call();return false}catch(error){return error instanceof ErrorCtor}}]);}
rejectsBorrow('local FormData.get / prototype forgery',()=>FormData.prototype.get.call(Object.create(FormData.prototype),'field'),TypeError);
rejectsBorrow('local FormData.get / proxy of real object',()=>FormData.prototype.get.call(new Proxy(foreignData,{}),'field'),TypeError);
rejectsBorrow('foreign FormData.get / proxy of real object',()=>child.FormData.prototype.get.call(new Proxy(localData,{}),'field'),child.TypeError);
rejectsBorrow('local XHR.readyState / prototype forgery',()=>localReady.call(Object.create(XMLHttpRequest.prototype)),TypeError);
rejectsBorrow('local XHR.readyState / proxy of real object',()=>localReady.call(new Proxy(foreignXhr,{})),TypeError);
rejectsBorrow('foreign XHR.readyState / proxy of real object',()=>foreignReady.call(new Proxy(localXhr,{})),child.TypeError);
rejectsBorrow('local ProgressEvent.loaded / prototype forgery',()=>localLoaded.call(Object.create(ProgressEvent.prototype)),TypeError);
rejectsBorrow('local ProgressEvent.loaded / proxy of real object',()=>localLoaded.call(new Proxy(foreignProgress,{})),TypeError);
rejectsBorrow('foreign ProgressEvent.loaded / proxy of real object',()=>foreignLoaded.call(new Proxy(localProgress,{})),child.TypeError);
rejectsBorrow('foreign FormData.get / prototype forgery',()=>child.FormData.prototype.get.call(Object.create(FormData.prototype),'field'),child.TypeError);

for(const[index,[name,check]]of cases.entries()){let ok=false,error=null;try{ok=check();}catch(value){error=value.name+': '+value.message;}console.log('FORMDATA_REALM_CASE '+JSON.stringify({index,name,ok,error}));}
frame.remove();console.log('FORMDATA_REALM_RESULTS '+JSON.stringify({count:cases.length}));
'''
PAGE=('<!doctype html><meta charset="utf-8"><title>FormData realm regression</title><body><script>'+SCRIPT+'</script>').encode()
NAMES=['local constructor / local form','local constructor / foreign form','foreign constructor / local form','foreign constructor / foreign form','local constructor / omitted argument','foreign constructor / omitted argument','local constructor / local prototype forgery','local constructor / foreign prototype forgery','foreign constructor / local prototype forgery','foreign constructor / foreign prototype forgery','local constructor / plain object','foreign constructor / plain object','local constructor / foreign div','foreign constructor / local div','local constructor / null','foreign constructor / null']
NAMES += ['local FormData.get / foreign real object', 'foreign FormData.get / local real object', 'local XHR.readyState / foreign real object', 'foreign XHR.readyState / local real object', 'local ProgressEvent.loaded / foreign real object', 'foreign ProgressEvent.loaded / local real object', 'local FormData.get / prototype forgery', 'local FormData.get / proxy of real object', 'foreign FormData.get / proxy of real object', 'local XHR.readyState / prototype forgery', 'local XHR.readyState / proxy of real object', 'foreign XHR.readyState / proxy of real object', 'local ProgressEvent.loaded / prototype forgery', 'local ProgressEvent.loaded / proxy of real object', 'foreign ProgressEvent.loaded / proxy of real object', 'foreign FormData.get / prototype forgery']
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
utc=lambda:datetime.now(timezone.utc).isoformat()

class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  if self.path!='/':self.send_error(404);return
  self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(PAGE)));self.end_headers();self.wfile.write(PAGE)
 def log_message(self,*_):pass

def main():
 parser=argparse.ArgumentParser(description=__doc__);choice=parser.add_mutually_exclusive_group(required=True);choice.add_argument('--binary',type=Path);choice.add_argument('--chromium',type=Path);parser.add_argument('--runtime-environment',type=Path);parser.add_argument('--source-manifest',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
 if args.output.exists():raise ValueError('Preserve prior receipt')
 args.output.parent.mkdir(parents=True,exist_ok=True)
 path=ROOT/'tests/test_blob_range_whitespace.py';spec=importlib.util.spec_from_file_location('realm_fixture_transport',path);transport=importlib.util.module_from_spec(spec);spec.loader.exec_module(transport);transport.MARKER=MARKER
 common=transport.common_module();error=common.monitor_module().pid_namespace_error()
 if error:raise RuntimeError(error)
 binary=(args.binary or args.chromium).resolve(strict=True);server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();began=time.monotonic()
 record={'kind':'actual Window/iframe FormData brands and constructor realms','officialWPT':False,'nativeLumenExecuted':bool(args.binary),'chromiumControlExecuted':bool(args.chromium),'binary':str(binary),'binarySHA256':sha(binary),'fixtureSHA256':sha(__file__),'transportSHA256':sha(path),'htmlSHA256':hashlib.sha256(PAGE).hexdigest(),'startedUTC':utc(),'rows':[],'infrastructureErrors':[]}
 if args.source_manifest:record['sourceManifestSHA256']=sha(args.source_manifest)
 try:
  url='http://127.0.0.1:'+str(server.server_port)+'/'
  runtime=json.loads(args.runtime_environment.read_text())if args.runtime_environment else{}
  result=transport.run_native(common,binary,url,args.output.with_suffix(''),runtime)if args.binary else transport.run_chromium(common,binary,url,args.output.with_suffix(''))
  for name in['metrics','cleanup','naturalExitCode','browserVersion']:
   if name in result:record[name]=result[name]
  summaries=[json.loads(line.split(MARKER,1)[1])for line in result['stdout'].splitlines()if MARKER in line]
  rows=[json.loads(line.split(CASE_MARKER,1)[1])for line in result['stdout'].splitlines()if CASE_MARKER in line];record['rows']=rows
  if len(summaries)!=1 or summaries[0].get('count')!=len(NAMES) or len(rows)!=len(NAMES) or [r['index']for r in rows]!=list(range(len(NAMES)))or[r['name']for r in rows]!=NAMES:raise RuntimeError('Fixture assertion inventory differs or runtime did not complete')
  if record.get('metrics',{}).get('errors')or record.get('cleanup',{}).get('error')or record.get('cleanup',{}).get('errors'):raise RuntimeError('Unqualified measurement or cleanup')
  if sha(binary)!=record['binarySHA256']or sha(path)!=record['transportSHA256']:raise RuntimeError('Fixture execution identity changed')
 except Exception as error:record['infrastructureErrors'].append(type(error).__name__+': '+str(error))
 finally:server.shutdown();server.server_close();thread.join(timeout=5)
 record.update(finishedUTC=utc(),durationSeconds=time.monotonic()-began,total=len(NAMES),recorded=len(record['rows']),passed=sum(r['ok']for r in record['rows']),failed=sum(not r['ok']for r in record['rows']),unrun=len(NAMES)-len(record['rows']))
 args.output.write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({k:record[k]for k in['kind','binarySHA256','total','recorded','passed','failed','unrun','infrastructureErrors','durationSeconds']}));raise SystemExit(0 if record['passed']==len(NAMES) and not record['infrastructureErrors']else 1)
if __name__=='__main__':main()
