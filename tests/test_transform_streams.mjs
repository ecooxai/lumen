import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const sourcePath=process.env.LUMEN_STREAMS_SOURCE||new URL('../src/js/prelude/06t-streams.js',import.meta.url);
const source=fs.readFileSync(sourcePath,'utf8');
const sandbox={AbortController,DOMException,TextEncoder,TextDecoder,console};
vm.createContext(sandbox);
if(source.includes('function createWebStreamInterfaces(')){
    vm.runInContext(source+'\nthis.interfaces=createWebStreamInterfaces(null,{TextEncoder,TextDecoder,AbortController,DOMException});',sandbox);
}else{
    const first=source.indexOf('const streamCtl = '),last=source.indexOf('const objectURLs = ',first);
    assert.ok(first>=0&&last>first,'Expected actual pre-extraction stream source');
    sandbox.def=(object,key,value)=>Object.defineProperty(object,key,{value,writable:true,configurable:true});
    vm.runInContext(source.slice(first,last)+'\nthis.interfaces={TransformStream};',sandbox);
}
const rejectedWith=async(promise,reason)=>{
    let rejected=false;
    try{await promise;}catch(error){rejected=true;assert.equal(error,reason);}
    assert.equal(rejected,true,'Operation must reject with the original reason');
};
for(const [name,Transform] of [['node-reference',TransformStream],['lumen-prelude',sandbox.interfaces.TransformStream]]){
    test(name+': rejected start errors pending read and write',{timeout:750},async()=>{
        const reason={startFailure:true};
        const stream=new Transform({start(){return Promise.reject(reason);}});
        const reader=stream.readable.getReader(),writer=stream.writable.getWriter();
        await Promise.all([rejectedWith(reader.read(),reason),rejectedWith(writer.write('x'),reason),rejectedWith(writer.closed,reason)]);
    });
    test(name+': rejected flush errors pending read and close',{timeout:750},async()=>{
        const reason={flushFailure:true};
        const stream=new Transform({flush(){return Promise.reject(reason);}});
        const reader=stream.readable.getReader(),writer=stream.writable.getWriter();
        await Promise.all([rejectedWith(reader.read(),reason),rejectedWith(writer.close(),reason),rejectedWith(writer.closed,reason)]);
    });
    for(const reason of [undefined,null,false]){
        test(name+': abort preserves '+String(reason)+' across a pressure-blocked write',{timeout:750},async()=>{
            const stream=new Transform(),writer=stream.writable.getWriter();
            const write=rejectedWith(writer.write('blocked'),reason);
            for(let i=0;i<8;i++)await Promise.resolve();
            const abort=writer.abort(reason),reader=stream.readable.getReader();
            await Promise.all([write,abort,rejectedWith(reader.read(),reason),rejectedWith(writer.closed,reason)]);
        });
    }
}
// Transformer.cancel is a newer API than the Node reference available on
// some development hosts. Its exact callback contract is asserted on Lumen.
for(const reason of [undefined,null,false]){
    test('lumen-prelude: transformer.cancel receives exact '+String(reason),{timeout:750},async()=>{
        const calls=[],transformer={cancel(value){calls.push({receiver:this,value});}};
        const stream=new sandbox.interfaces.TransformStream(transformer),writer=stream.writable.getWriter();
        const reader=stream.readable.getReader(),read=rejectedWith(reader.read(),reason);
        await writer.abort(reason);await read;
        assert.equal(calls.length,1);assert.equal(calls[0].receiver,transformer);assert.equal(calls[0].value,reason);
    });
}
