import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';

// Execute the actual shipped prelude against the same policy contract as the
// native bridge. These descriptor/status tests do not request hardware access.
// Normative query conversion: https://w3c.github.io/permissions/#dom-permissions-query
const source=readFileSync(process.argv[2]||'src/js/prelude/08-window.js','utf8');
const start=source.indexOf('/* getUserMedia (real camera/mic)');
const end=source.indexOf('    class BlobEvent',start);
assert(start>=0&&end>start,'capture prelude exists');
const code=source.slice(start,end)+'\n})();';

function fixture(){
  const allowed={camera:true,microphone:true,location:true,screen:true};
  const nativeCalls=[];
  const env={EventTarget,Event,DOMException,Promise,navigator:{permissions:{},mediaDevices:{}},
    location:{origin:'https://permission.example'},
    N:{capState(name){nativeCalls.push(name);return allowed[name]?1:-1;},
      capAllowed(name){return allowed[name];}},
  };
  runInNewContext(code,env,{filename:'lumen-permission-prelude.js'});
  return{query:descriptor=>env.navigator.permissions.query(descriptor),allowed,nativeCalls};
}

let checks=0,failures=0;
async function check(name,fn){checks++;try{await fn();console.log('PASS',name);}catch(error){failures++;console.error('FAIL',name,error.message);}}
async function rejected(f,descriptor,name='TypeError',sameError=null){
  let promise;
  assert.doesNotThrow(()=>{promise=f.query(descriptor);},'query errors reject its promise');
  assert.equal(typeof promise.then,'function');
  await assert.rejects(promise,error=>error.name===name&&(!sameError||error===sameError));
}

for(const name of ['lumen-unknown-permission','toString','__proto__','constructor']){
  await check('reject unsupported permission '+name,()=>rejected(fixture(),{name}));
}
await check('reject a Symbol permission name',()=>rejected(fixture(),{name:Symbol('camera')}));
await check('return a rejected promise for descriptor getter errors',async()=>{
  const error=new RangeError('fixture getter');await rejected(fixture(),{get name(){throw error;}},'RangeError',error);
});
await check('return a rejected promise for DOMString conversion errors',async()=>{
  const error=new RangeError('fixture conversion');await rejected(fixture(),{name:{toString(){throw error;}}},'RangeError',error);
});
for(const descriptor of [undefined,null,{},'camera',42]){
  await check('reject missing or invalid descriptor '+String(descriptor),()=>rejected(fixture(),descriptor));
}
await check('convert and retain the requested DOMString',async()=>{
  const f=fixture(),name={toString:()=> 'camera'},status=await f.query({name});
  name.toString=()=> 'microphone';
  assert.equal(status.name,'camera');assert.equal(status.state,'granted');
  assert.deepEqual(f.nativeCalls,['camera']);
});
await check('name is a readonly prototype attribute',async()=>{
  const status=await fixture().query({name:'camera'}),descriptor=Object.getOwnPropertyDescriptor(Object.getPrototypeOf(status),'name');
  assert.equal(typeof descriptor.get,'function');assert.equal(descriptor.set,undefined);assert.equal(descriptor.enumerable,true);
  assert.equal(Reflect.set(status,'name','microphone'),false);assert.equal(status.name,'camera');
});
await check('shadowing the public name cannot retarget its native permission',async()=>{
  const f=fixture(),status=await f.query({name:'camera'});f.allowed.camera=false;
  Object.defineProperty(status,'name',{value:'microphone'});
  assert.equal(status.state,'denied');assert.deepEqual(f.nativeCalls,['camera']);
});
await check('page script cannot construct a forged PermissionStatus',async()=>{
  const status=await fixture().query({name:'camera'});
  assert.throws(()=>new status.constructor('camera'),error=>error.name==='TypeError');
});
await check('name and state getters reject non-status receivers',async()=>{
  const status=await fixture().query({name:'camera'}),proto=Object.getPrototypeOf(status);
  for(const name of ['name','state'])assert.throws(()=>Object.getOwnPropertyDescriptor(proto,name).get.call({}),error=>error.name==='TypeError');
});
await check('geolocation retains the native location mapping',async()=>{
  const f=fixture(),status=await f.query({name:'geolocation'});assert.equal(status.state,'granted');assert.deepEqual(f.nativeCalls,['location']);
});
console.log(`RESULTS permission-descriptors ${failures?'FAIL':'PASS'} (${checks-failures}/${checks}; shipped prelude, controlled policy bridge)`);
process.exitCode=failures?1:0;
