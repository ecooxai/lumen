import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {once} from 'node:events';
import {createProgressServer} from '../server.mjs';

async function fixture(t, {status, scorecard}={}) {
  const stateDir=await fs.mkdtemp(path.join(os.tmpdir(),'lume-progress-task-'));
  const scorePath=path.join(stateDir,'score.json');
  if(status)await fs.writeFile(path.join(stateDir,'status.json'),JSON.stringify(status));
  if(scorecard)await fs.writeFile(scorePath,JSON.stringify(scorecard));
  const server=createProgressServer({taskId:'102900614',stateDir,scorecard:scorePath});
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  t.after(async()=>{const closed=once(server,'close');server.closeProgress();await closed;await fs.rm(stateDir,{recursive:true,force:true});});
  const base=`http://127.0.0.1:${server.address().port}`;
  return async endpoint=>{const response=await fetch(base+endpoint);assert.equal(response.status,200);return response.json();};
}

test('a new task exposes its own health and browser session identity',async t=>{
  const get=await fixture(t);
  assert.equal((await get('/api/health')).taskId,'102900614');
  assert.deepEqual(await get('/config.json'),{liveBase:'',taskId:'102900614'});
  assert.equal((await get('/api/progress')).status.taskId,'102900614');
});

test('a prior task receipt cannot be presented as current task progress',async t=>{
  const get=await fixture(t,{status:{taskId:'10290244',phase:'Stale completed run',wpt:{passed:999}},scorecard:{task_id:'10290244',score:98}});
  const result=await get('/api/progress');
  assert.equal(result.status.taskId,'102900614');
  assert.equal(result.status.wpt,null);
  assert.equal(result.scorecard,null);
  assert.match(result.status.phase,/identity mismatch/i);
});

test('matching status and score retain their measured values and inherited scope',async t=>{
  const status={taskId:'102900614',phase:'Inherited baseline',wpt:{passed:1850,total:1850},fullOfficialWptPassPercent:null};
  const scorecard={task_id:'102900614',score:50,accepted_artifact_status:'inherited_baseline'};
  const get=await fixture(t,{status,scorecard});
  const result=await get('/api/progress');
  assert.deepEqual(result.status,status);
  assert.deepEqual(result.scorecard,scorecard);
});
