from pathlib import Path
import ast,hashlib,json
O=Path('output/session-10290244/frame-microtask-candidate')
src=(O/'test_frame_microtask_realm.py').read_text()
html=r'''<!doctype html><meta charset="utf-8"><title>Storage key unsigned long regression</title><script>
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
prefix,rest=src.split("HTML = r'''",1);_,suffix=rest.split("'''",1)
out=prefix+"HTML = r'''"+html+"'''"+suffix
out=out.replace('EXPECTED=35','EXPECTED=66').replace('FRAME_MICROTASK_','STORAGE_KEY_').replace('data-frame-microtask-done','data-storage-key-done').replace('lume-frame-microtask-','lume-storage-key-')
ast.parse(out)
p=O/'test_storage_key_unsigned_long.py';p.write_text(out)
(O/'storage-key-fixture.html').write_text(html)
print(json.dumps({'test':str(p),'sha256':hashlib.sha256(out.encode()).hexdigest(),'expected_checks':66}))
