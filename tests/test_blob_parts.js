const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const {test}=require('node:test');
function harness(){
 const source=fs.readFileSync('src/js/prelude/07-web.js','utf8');let encodes=0;
 const context={ArrayBuffer,Uint8Array,DataView,ReadableStream,def:(o,k,v)=>Object.defineProperty(o,k,{value:v,writable:true,configurable:true}),N:{encode(s){encodes++;return new TextEncoder().encode(s).buffer},decode:b=>new TextDecoder().decode(b)}};
 vm.runInNewContext(source.slice(source.indexOf('const toBytes'),source.indexOf('class File extends'))+'\nthis.TestBlob=Blob;',context);
 return {Blob:context.TestBlob,encodes:()=>encodes};
}
test('adjacent character parts use one encoding allocation',async()=>{
 const h=harness(),s='abcé界'.repeat(8192),b=new h.Blob(s.split(''));
 assert.equal(await b.text(),s);assert.equal(h.encodes(),1);
});
test('Unicode conversion occurs per part before joining',async()=>{
 const {Blob}=harness();
 assert.deepEqual([...await new Blob(['\ud83d','\ude00']).bytes()],[239,191,189,239,191,189]);
 assert.deepEqual([...await new Blob(['\ud83d\ude00']).bytes()],[240,159,152,128]);
 assert.equal(await new Blob(['\ud83d','','x','\ude00']).text(),'\ufffdx\ufffd');
});
test('text runs preserve binary offsets, ordering and snapshot ownership',async()=>{
 const {Blob}=harness(),a=new Uint8Array([10,20,30,40]);
 const b=new Blob(['A',new DataView(a.buffer,1,2),new Blob(['B']),'C']);a.fill(0);
 assert.deepEqual([...await b.bytes()],[65,20,30,66,67]);
 const copy=await b.arrayBuffer();new Uint8Array(copy).fill(0);assert.equal(b.size,5);assert.equal((await b.bytes())[0],65);
});
test('empty parts and effectful string coercion keep their order',async()=>{
 const {Blob}=harness();let calls=0;const p={toString(){calls++;return 'middle'}};
 assert.equal(await new Blob(['left',p,'right']).text(),'leftmiddleright');assert.equal(calls,1);
 assert.throws(()=>new Blob('abc'),/sequence|map/);
 assert.equal(new Blob([]).size,0);assert.equal(new Blob(['','']).size,0);
});
