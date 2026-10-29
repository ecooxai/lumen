import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {fileURLToPath} from 'node:url';
import {createProgressServer} from '../server.mjs';
import {createMessageStore} from '../model.mjs';

const execute=promisify(execFile);
const here=path.dirname(fileURLToPath(import.meta.url));

test('a new task preserves the same private browser message namespace and receipts',async t=>{
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'lume-message-continuity-'));
  t.after(()=>fs.rm(directory,{recursive:true,force:true}));
  const state=path.join(directory,'state');await fs.mkdir(state);
  await fs.writeFile(path.join(state,'status.json'),JSON.stringify({taskId:'new-task',fullOfficialWptPassPercent:null}));
  const store=createMessageStore(state),previous=await store.submit('Pending before the task changed');
  const token='synthetic-private-capability-for-this-test';
  const server=createProgressServer({taskId:'new-task',messageSessionId:'old-task',stateDir:state,messageToken:token,sampleInterval:100000});
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  t.after(()=>new Promise(resolve=>{server.once('close',resolve);server.closeProgress();}));
  const origin='http://127.0.0.1:'+server.address().port;
  const config=await (await fetch(origin+'/config.json')).json();
  assert.equal(config.taskId,'new-task');
  assert.equal(config.messageSessionId,'old-task','Ordinary reload must keep the old client access and receipt namespace');
  const health=await (await fetch(origin+'/api/health')).json();
  assert.equal(health.taskId,'new-task','Message continuity must not reuse the old task evidence identity');
  const prior=await (await fetch(origin+'/api/message/'+previous.id+'?key='+encodeURIComponent(previous.key))).json();
  assert.equal(prior.text,'Pending before the task changed');
  const posted=await fetch(origin+'/api/message',{method:'POST',headers:{origin,'content-type':'application/json',authorization:'Bearer '+token},body:JSON.stringify({text:'Reply after ordinary reload'})});
  assert.equal(posted.status,201);
  const progress=await (await fetch(origin+'/api/progress')).json();
  assert.equal(progress.status.taskId,'new-task');
  assert.ok(!JSON.stringify(config).includes(token));
  assert.ok(!JSON.stringify(progress).includes(token));
});

test('static delivery uses the same explicit message namespace as the live endpoint',async t=>{
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'lume-static-continuity-'));
  t.after(()=>fs.rm(directory,{recursive:true,force:true}));
  await execute(process.execPath,[path.join(here,'../build.mjs'),'--live-base','https://lume-continuity.example','--output',directory],
    {env:{...process.env,LUME_TASK_ID:'new-task',LUME_MESSAGE_SESSION_ID:'old-task'},timeout:10000});
  const config=JSON.parse(await fs.readFile(path.join(directory,'config.json'),'utf8'));
  assert.equal(config.taskId,'new-task');assert.equal(config.messageSessionId,'old-task');
  assert.equal(config.liveBase,'https://lume-continuity.example');
});
