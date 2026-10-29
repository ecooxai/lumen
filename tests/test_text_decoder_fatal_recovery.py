#!/usr/bin/env python3
"""Pinned Chrome interop for fatal UTF-8 invalid-leading-byte chunk tails.

Keep the established170 window/worker/cross-realm checks unchanged. The separate
invalid-continuation disagreement remains an explicit control and in raw paired
observations; this fixture does not claim complete Chrome recovery equivalence.
"""
import hashlib,json
from pathlib import Path
import test_text_codec as fixture

EXTRA=r"""
 for(const bad of [0x80,0xBF,0xC0,0xC1,0xF5,0xFD,0xFE,0xFF])
 check('fatal UTF8 invalid leading byte 0x'+bad.toString(16)+' discards rejected chunk tail',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});
  return throws('TypeError',()=>d.decode(bytes([bad,0xEF]),{stream:true}))&&d.decode()===''&&d.decode(bytes([65]))==='A';
 });
 check('fatal UTF8 discards ASCII following an invalid leading byte',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});
  return throws('TypeError',()=>d.decode(bytes([0xFD,65]),{stream:true}))&&d.decode()===''&&d.decode(bytes([66]))==='B';
 });
 check('discarded UTF8 prefix cannot complete a later BOM',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});
  return throws('TypeError',()=>d.decode(bytes([0xFD,0xEF]),{stream:true}))&&throws('TypeError',()=>d.decode(bytes([0xBB,0xBF,65])))&&d.decode()==='';
 });
 check('fatal UTF8 never consumes a later BOM in the rejected chunk',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});
  return throws('TypeError',()=>d.decode(bytes([0xFD,0xEF,0xBB,0xBF]),{stream:true}))&&d.decode(bytes([0xEF,0xBB,0xBF,65]))==='A';
 });
 check('invalid UTF8 leading byte after valid text still discards its chunk tail',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});
  return throws('TypeError',()=>d.decode(bytes([65,0xFD,0xEF]),{stream:true}))&&d.decode()==='';
 });
 check('fatal UTF8 without a tail preserves the next valid streamed scalar',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});
  return throws('TypeError',()=>d.decode(bytes([0xFD]),{stream:true}))&&d.decode(bytes([0xC2]),{stream:true})===''&&d.decode(bytes([0xA2]))==='¢';
 });
 check('BOM before fatal UTF8 error preserves observed API serialization behavior',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});
  return throws('TypeError',()=>d.decode(bytes([0xEF,0xBB,0xBF,0xFF]),{stream:true}))&&d.decode(bytes([0xEF,0xBB,0xBF,65]))==='A';
 });
 check('replacement UTF8 still consumes bytes after invalid input',()=>{
  const d=new TextDecoder();return d.decode(bytes([0xFD,0xEF]),{stream:true})==='\uFFFD'&&d.decode()==='\uFFFD';
 });
 check('normal UTF8 streaming scalar is unchanged',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});return d.decode(bytes([0xE2,0x82]),{stream:true})===''&&d.decode(bytes([0xAC]))==='€';
 });
 check('fatal ISO2022JP retains its observed Roman state',()=>{
  const d=new TextDecoder('iso-2022-jp',{fatal:true});
  return throws('TypeError',()=>d.decode(bytes([0x1B,0x28,0x4A,0xFF]),{stream:true}))&&d.decode(bytes([0x7E]))==='\u203E';
 });
 check('known invalid-continuation queue behavior remains explicit and unresolved against Chrome',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});
  return throws('TypeError',()=>d.decode(bytes([0xE2,65]),{stream:true}))&&d.decode()==='A';
 });
 for(const [name,calls] of [
  ['partial E2 then bad ASCII',[[[0xE2,65],true],[[0x82,0xAC],false]]],
  ['partial E2 82 then bad ASCII',[[[0xE2,0x82,65],true],[[0xAC],false]]],
  ['restricted E0 then low continuation',[[[0xE0,0x80,65],true],[[0xA0,0x80],false]]],
 ])check('observed followup remains fatal: '+name,()=>{
  const d=new TextDecoder('utf-8',{fatal:true});return calls.every(([input,stream])=>throws('TypeError',()=>d.decode(bytes(input),{stream})));
 });
 check('observed prior-call UTF8 prefix then bad call and continuations remain fatal',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});return d.decode(bytes([0xE2]),{stream:true})===''&&throws('TypeError',()=>d.decode(bytes([65,66]),{stream:true}))&&throws('TypeError',()=>d.decode(bytes([0x82,0xAC])));
 });
 check('fatal UTF8 EOF resets the next nonstream decoding session',()=>{
  const d=new TextDecoder('utf-8',{fatal:true});return d.decode(bytes([0xF0,0x90]),{stream:true})===''&&throws('TypeError',()=>d.decode())&&d.decode(bytes([0xEF,0xBB,0xBF,66]))==='B';
 });
 check('UTF16 fatal queue semantics remain outside the UTF8 interop change',()=>{
  const d=new TextDecoder('utf-16le',{fatal:true});return throws('TypeError',()=>d.decode(bytes([0x00,0xDC,65,0]),{stream:true}))&&d.decode()==='A';
 });
"""
anchor=' return rows;\n}\n'
if fixture.CORE.count(anchor)!=1:raise SystemExit('Established codec fixture changed')
expanded=fixture.CORE.replace(anchor,EXTRA+anchor)
if fixture.HTML.count(fixture.CORE)!=1 or fixture.WORKER.count(fixture.CORE)!=1:raise SystemExit('Established realm fixture identity changed')
fixture.HTML=fixture.HTML.replace(fixture.CORE,expanded)
fixture.WORKER=fixture.WORKER.replace(fixture.CORE,expanded)
fixture.CORE=expanded
fixture.EXPECTED+=48
if fixture.EXPECTED!=218:raise SystemExit('Established codec fixture count changed')

if __name__=='__main__':
    print('TEXT_CODEC_FATAL_EXTENSION '+json.dumps({'sourceSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
       'baseSourceSHA256':hashlib.sha256(Path(fixture.__file__).read_bytes()).hexdigest(),'total':218,
       'scope':'General invalid UTF8 leading-byte recovery interop; invalid-continuation Chrome divergence retained explicitly.'}))
    raise SystemExit(fixture.main())
