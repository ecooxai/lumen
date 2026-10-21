import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {toCookieParam,sameCookies,assertOwnedProfileAvailable} from '../chrome_context_transfer.mjs';
const fixture={name:'fixture',value:'synthetic-only',domain:'accounts.google.com',path:'/scope',secure:true,httpOnly:true,session:true,expires:-1,sameSite:'Strict',sourceScheme:'Secure',sourcePort:443};

test('host-only cookie is imported using URL and session expiry remains absent',()=>{
  const param=toCookieParam(fixture);assert.equal(param.url,'https://accounts.google.com/');assert.equal('domain' in param,false);assert.equal('expires' in param,false);assert.equal(param.path,'/scope');assert.equal(param.httpOnly,true);assert.equal(param.sameSite,'Strict');
});
test('domain scope, persistent expiry and unspecified SameSite are preserved',()=>{
  const row={...fixture,domain:'.google.com',session:false,expires:1700000000.5};delete row.sameSite;const param=toCookieParam(row);
  assert.equal(param.domain,'.google.com');assert.equal('url' in param,false);assert.equal(param.expires,row.expires);assert.equal('sameSite' in param,false);
});
test('roundtrip rejects a changed value, widened scope, relaxed flag, expiry or missing cookie',()=>{
  assert.equal(sameCookies([fixture],[{...fixture}]),true);
  for(const changes of [{value:'other-synthetic'},{domain:'.accounts.google.com'},{httpOnly:false},{secure:false},{session:false},{sameSite:'None'},{sourcePort:80}])assert.equal(sameCookies([fixture],[{...fixture,...changes}]),false);
  assert.equal(sameCookies([fixture],[]),false);
  const persistent={...fixture,session:false,expires:1700000000};assert.equal(sameCookies([persistent],[{...persistent,expires:1700000100}]),false);
});

test('non-Secure cookie uses HTTP setter URL and explicit HTTPS sourceScheme to avoid CDP promotion',()=>{
  const param=toCookieParam({...fixture,secure:false,sourceScheme:'Secure'});assert.equal(param.url,'http://accounts.google.com/');assert.equal(param.secure,false);assert.equal(param.sourceScheme,'Secure');
});


test('profile launcher refuses missing markers and active or stale singleton locks',()=>{
  const root=fs.mkdtempSync(path.join(os.tmpdir(),'lume-bridge-unit-'));
  try{
    assert.throws(()=>assertOwnedProfileAvailable(root),/private-profile-marker-missing/);
    fs.writeFileSync(path.join(root,'LumePrivateProfile.json'),JSON.stringify({kind:'synthetic-cookie-fixture'}));
    assert.doesNotThrow(()=>assertOwnedProfileAvailable(root));
    fs.symlinkSync('nonexistent-fixture-lock',path.join(root,'SingletonLock'));
    assert.throws(()=>assertOwnedProfileAvailable(root),/private-profile-already-in-use-or-stale-lock/);
    fs.unlinkSync(path.join(root,'SingletonLock'));
    fs.writeFileSync(path.join(root,'SingletonSocket'),'synthetic');
    assert.throws(()=>assertOwnedProfileAvailable(root),/private-profile-already-in-use-or-stale-lock/);
  }finally{fs.rmSync(root,{recursive:true,force:true});}
});
