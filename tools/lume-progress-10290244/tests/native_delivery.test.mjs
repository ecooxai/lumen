import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {once} from 'node:events';
import test from 'node:test';
import {createProgressServer} from '../server.mjs';

async function fixture(configure, check) {
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'lumen-native-download-'));
  const archive=path.join(dir,'accepted.zip'),receipt=path.join(dir,'accepted.json');
  const zip=Buffer.from('PK\x03\x04owned native delivery fixture');
  await fs.writeFile(archive,zip);await fs.writeFile(receipt,'{"nativeMacDevelopmentApp":true}\n');
  const options={stateDir:path.join(dir,'state'),sampleInterval:10000,
    publicDir:path.resolve(import.meta.dirname,'../public')};
  await configure({dir,archive,receipt,options});
  const server=createProgressServer(options);
  server.listen(0,'127.0.0.1');await once(server,'listening');
  try {await check(`http://127.0.0.1:${server.address().port}`,zip);}
  finally {server.closeAllConnections();await new Promise(resolve=>server.close(resolve));await fs.rm(dir,{recursive:true,force:true});}
}

test('native downloads remain absent without explicitly configured acceptance files',async()=>{
  await fixture(async()=>{},async base=>{
    for(const name of ['lumen-macos.zip','lumen-macos-receipt.json'])assert.equal((await fetch(base+'/'+name)).status,404);
  });
});

test('only two exact native routes serve the configured bytes and truthful download name',async()=>{
  await fixture(async({archive,receipt,options})=>Object.assign(options,{nativeMacArchive:archive,nativeMacReceipt:receipt}),async(base,zip)=>{
    const head=await fetch(base+'/lumen-macos.zip',{method:'HEAD'});
    assert.equal(head.status,200);assert.equal(head.headers.get('content-type'),'application/zip');
    assert.equal(Number(head.headers.get('content-length')),zip.length);
    assert.match(head.headers.get('content-disposition'),/Mac-development\.zip/);
    assert.equal((await head.arrayBuffer()).byteLength,0);
    assert.deepEqual(Buffer.from(await (await fetch(base+'/lumen-macos.zip')).arrayBuffer()),zip);
    assert.equal((await (await fetch(base+'/lumen-macos-receipt.json')).json()).nativeMacDevelopmentApp,true);
    for(const name of ['accepted.zip','accepted.json','private-fixture/ca.key','api/native-files']){
      assert.equal((await fetch(base+'/'+name)).status,404);
    }
    assert.equal((await fetch(base+'/lumen-macos.zip',{method:'POST'})).status,405);
  });
});

test('native artifact routes reject symlink targets instead of exposing unrelated files',async()=>{
  await fixture(async({dir,archive,receipt,options})=>{
    const link=path.join(dir,'link.zip');await fs.symlink(archive,link);
    Object.assign(options,{nativeMacArchive:link,nativeMacReceipt:receipt});
  },async base=>assert.equal((await fetch(base+'/lumen-macos.zip')).status,404));
});
