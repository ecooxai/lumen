import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const sourcePath=process.env.LUMEN_STREAMS_SOURCE||new URL('../src/js/prelude/06t-streams.js',import.meta.url);
const source=fs.readFileSync(sourcePath,'utf8');
const first=source.includes('function writableDeferred(')?source.indexOf('function writableDeferred('):source.indexOf('class WritableStreamDefaultWriter');
const last=source.indexOf('class TransformStream',first);
assert.ok(first>=0&&last>first,'Expected the actual writable prelude source');
const sandbox={AbortController,console,def:(object,key,value)=>Object.defineProperty(object,key,{value,writable:true,configurable:true})};
vm.createContext(sandbox);
vm.runInContext(source.slice(first,last)+'\nthis.testWritable={WritableStream,WritableStreamDefaultWriter};',sandbox);
const engines=[['node-reference',globalThis.WritableStream],['lumen-prelude',sandbox.testWritable.WritableStream]];
const deferred=()=>{let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return{promise,resolve,reject};};
const tick=async()=>{for(let n=0;n<12;n++)await Promise.resolve();};
const typeError=error=>error?.name==='TypeError';
const rangeError=error=>error?.name==='RangeError';
const rejectedWith=async(promise,reason)=>{let caught=false;try{await promise;}catch(error){caught=true;assert.equal(error,reason);}assert.equal(caught,true,'Promise must reject with the original reason');};

for(const [engine,Writable] of engines){
 const check=(name,run)=>test(`${engine}: ${name}`,{timeout:1500},run);
 check('writer ready is stable, pending under weighted backpressure, and resolves only after drain',async()=>{
  const gate=deferred(),entered=deferred();let calls=0;
  const stream=new Writable({write(){calls++;entered.resolve();return gate.promise;}},{highWaterMark:4,size:chunk=>chunk.byteLength});
  const writer=stream.getWriter();const initial=writer.ready;assert.equal(initial,writer.ready);await initial;
  const written=writer.write(new Uint8Array(6));const ready=writer.ready;let readyResolved=false;ready.then(()=>{readyResolved=true;},()=>{});
  try{assert.equal(writer.desiredSize,-2);assert.notEqual(ready,initial);assert.equal(ready,writer.ready);await entered.promise;await tick();assert.equal(readyResolved,false);assert.equal(calls,1);}finally{gate.resolve();}
  await written;await ready;assert.equal(writer.desiredSize,4);await writer.close();await writer.closed;
 });
 check('queued writes are serialized and strategy size includes in-flight data',async()=>{
  const first=deferred(),entered=deferred();const calls=[];
  const stream=new Writable({write(chunk){calls.push(chunk);if(chunk===2){entered.resolve();return first.promise;}}},{highWaterMark:4,size:chunk=>chunk});
  const writer=stream.getWriter();const one=writer.write(2),two=writer.write(3);
  try{assert.equal(writer.desiredSize,-1);await entered.promise;assert.deepEqual(calls,[2]);}finally{first.resolve();}
  await Promise.all([one,two]);assert.deepEqual(calls,[2,3]);assert.equal(writer.desiredSize,4);await writer.close();
 });
 check('zero high water mark starts with pending ready and close resolves it',async()=>{
  const writer=new Writable({}, {highWaterMark:0}).getWriter();let resolved=false;writer.ready.then(()=>{resolved=true;},()=>{});await tick();assert.equal(resolved,false);assert.equal(writer.desiredSize,0);const closing=writer.close();await writer.ready;await closing;assert.equal(writer.desiredSize,0);
 });
 check('release revokes writer promises and methods without cancelling pending sink work',async()=>{
  const gate=deferred(),entered=deferred();const stream=new Writable({write(){entered.resolve();return gate.promise;}});const writer=stream.getWriter();
  const write=writer.write('first'),ready=writer.ready,closed=writer.closed;ready.catch(()=>{});closed.catch(()=>{});await entered.promise;
  writer.releaseLock();assert.equal(stream.locked,false);await assert.rejects(ready,typeError);await assert.rejects(closed,typeError);await assert.rejects(writer.ready,typeError);await assert.rejects(writer.closed,typeError);
  await assert.rejects(writer.write('late'),typeError);await assert.rejects(writer.close(),typeError);await assert.rejects(writer.abort(),typeError);assert.throws(()=>writer.desiredSize,typeError);writer.releaseLock();
  const replacement=stream.getWriter();try{assert.equal(replacement.desiredSize,0);}finally{gate.resolve();}await write;await replacement.ready;await replacement.close();await replacement.closed;
 });
 check('release after close rejects only the released writer, allowing a new closed writer',async()=>{
  const stream=new Writable();const writer=stream.getWriter();await writer.close();const oldReady=writer.ready;const oldClosed=writer.closed;await oldClosed;writer.releaseLock();await oldReady;await oldClosed;await assert.rejects(writer.ready,typeError);await assert.rejects(writer.closed,typeError);const next=stream.getWriter();await next.ready;await next.closed;assert.equal(next.desiredSize,0);await assert.rejects(next.write(1),typeError);
 });
 check('invalid queue size rejects writes and poisons the stream, including undefined thrown reason',async()=>{
  for(const size of [()=>-1,()=>NaN,()=>Infinity]){
   let calls=0;const writer=new Writable({write(){calls++;}},{size}).getWriter();await assert.rejects(writer.write('bad'),rangeError);await assert.rejects(writer.ready,rangeError);await assert.rejects(writer.closed,rangeError);assert.equal(writer.desiredSize,null);assert.equal(calls,0);
  }
  const writer=new Writable({}, {size(){throw undefined;}}).getWriter();await rejectedWith(writer.write(1),undefined);await rejectedWith(writer.ready,undefined);await rejectedWith(writer.closed,undefined);assert.equal(writer.desiredSize,null);
 });
 check('constructor validates strategy and sink callbacks and propagates synchronous start errors',()=>{
  for(const highWaterMark of [-1,NaN])assert.throws(()=>new Writable({}, {highWaterMark}),rangeError);
  assert.doesNotThrow(()=>new Writable({}, {highWaterMark:Infinity}));assert.throws(()=>new Writable({}, {size:1}),typeError);assert.throws(()=>new Writable({write:1}),typeError);
  const error={fixture:true};assert.throws(()=>new Writable({start(){throw error;}}),caught=>caught===error);
 });
 check('controller.error(undefined) rejects queued writes and closed without invoking sink writes',async()=>{
  const started=deferred();let controller,calls=0;const writer=new Writable({start(c){controller=c;return started.promise;},write(){calls++;}}).getWriter();
  const write=writer.write(1);write.catch(()=>{});controller.error(undefined);await rejectedWith(writer.ready,undefined);started.resolve();await rejectedWith(write,undefined);await rejectedWith(writer.closed,undefined);assert.equal(writer.desiredSize,null);assert.equal(calls,0);
 });
 check('abort signals immediately, waits for in-flight write, skips queued writes, and invokes sink once',async()=>{
  const pending=deferred(),entered=deferred(),aborting=deferred();let controller;const calls=[];const reason={stop:true};
  const stream=new Writable({start(c){controller=c;},write(value){calls.push(['write',value]);entered.resolve();return pending.promise;},abort(value){calls.push(['abort',value]);return aborting.promise;}});
  const writer=stream.getWriter();const first=writer.write(1),second=writer.write(2);second.catch(()=>{});await entered.promise;
  const abort=writer.abort(reason),again=writer.abort(reason);assert.equal(abort,again);await tick();assert.equal(controller.signal.aborted,true);assert.equal(controller.signal.reason,reason);assert.deepEqual(calls,[['write',1]]);await rejectedWith(writer.ready,reason);
  pending.resolve();await first;await rejectedWith(second,reason);await tick();assert.deepEqual(calls,[['write',1],['abort',reason]]);let aborted=false;abort.then(()=>{aborted=true;},()=>{});await tick();assert.equal(aborted,false);aborting.resolve();await abort;await rejectedWith(writer.closed,reason);assert.equal(writer.desiredSize,null);
 });
 check('close waits for writes and underlying close while ready clears backpressure',async()=>{
  const pending=deferred(),entered=deferred(),closing=deferred();const calls=[];
  const writer=new Writable({write(){calls.push('write');entered.resolve();return pending.promise;},close(){calls.push('close');return closing.promise;}}).getWriter();
  const write=writer.write(1);await entered.promise;const ready=writer.ready;const close=writer.close();await ready;await assert.rejects(writer.write(2),typeError);await assert.rejects(writer.close(),typeError);assert.deepEqual(calls,['write']);pending.resolve();await write;await tick();assert.deepEqual(calls,['write','close']);let done=false;writer.closed.then(()=>{done=true;});await tick();assert.equal(done,false);closing.resolve();await close;await writer.closed;assert.equal(writer.desiredSize,0);
 });
 check('sink rejection preserves the first error and rejects later queued work',async()=>{
  const gate=deferred(),entered=deferred();const reason={writeFailure:true};let writes=0;const writer=new Writable({write(){writes++;entered.resolve();return gate.promise;}}).getWriter();const a=writer.write(1),b=writer.write(2);a.catch(()=>{});b.catch(()=>{});await entered.promise;gate.reject(reason);await rejectedWith(a,reason);await rejectedWith(b,reason);await rejectedWith(writer.ready,reason);await rejectedWith(writer.closed,reason);assert.equal(writes,1);
 });
 check('stream abort and close reject while locked, while writer operations remain valid',async()=>{
  const stream=new Writable();const writer=stream.getWriter();await assert.rejects(stream.abort('locked'),typeError);await assert.rejects(stream.close(),typeError);await writer.close();await writer.closed;
 });
 check('size callback releasing a writer cannot enqueue on behalf of its replacement',async()=>{
  let writer,calls=0;const stream=new Writable({write(){calls++;}},{size(){writer.releaseLock();return 1;}});writer=stream.getWriter();await assert.rejects(writer.write('reentrant'),typeError);assert.equal(calls,0);const next=stream.getWriter();await next.close();
 });
}
