import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createNavigationReadiness,waitForNavigationReadiness} from './public_video_gesture_chrome_ready.mjs';

let checks=0;
const note=name=>{checks++;console.log('PASS '+name);};
const selected={frameId:'owned-frame',loaderId:'owned-loader'};
const event=(extra={})=>({method:'Page.lifecycleEvent',params:{...selected,name:'DOMContentLoaded',...extra}});

{
  const gate=createNavigationReadiness();gate.select(selected);
  gate.event(event({frameId:'child-frame'}));gate.event(event({loaderId:'previous-loader'}));gate.event(event({name:'load'}));
  assert.equal(gate.isReady(),false);assert.equal(gate.summary().navigationLoadObserved,true);
  gate.event(event());assert.equal(gate.isReady(),true);
  note('only the matching top-frame loader DOMContentLoaded admits observation');
}
{
  const gate=createNavigationReadiness();gate.event(event());assert.equal(gate.isReady(),false);
  gate.select(selected);assert.equal(gate.isReady(),true);
  note('a lifecycle receipt preceding the navigation response is retained');
}
{
  for(const bad of [{frameId:'owned-frame'},{...selected,errorText:'private URL'},null]) {
    const gate=createNavigationReadiness();assert.throws(()=>gate.select(bad));
  }
  const gate=createNavigationReadiness();gate.select(selected);assert.throws(()=>gate.select(selected));
  note('missing loaders and failed or repeated navigation stay refused');
}
{
  const gate=createNavigationReadiness();gate.select(selected);let clock=0;
  await assert.rejects(waitForNavigationReadiness(gate,0,{now:()=>clock,sleep:async n=>{clock+=n;}}));
  assert.equal(clock,20000);assert.equal(gate.isReady(),false);
  note('the readiness wait expires within the unchanged observation budget');
}
{
  const gate=createNavigationReadiness();gate.select(selected);let clock=0;
  await waitForNavigationReadiness(gate,0,{now:()=>clock,sleep:async n=>{clock+=n;if(clock===300)gate.event(event());}});
  assert.equal(clock,300);assert.equal(gate.isReady(),true);
  const output=JSON.stringify(gate.summary());assert.equal(output.includes('owned-frame'),false);assert.equal(output.includes('owned-loader'),false);
  note('ready events release the wait without emitting frame or loader identifiers');
}
{
  const old=fs.readFileSync(new URL('./public_video_gesture_chrome_diagnostic.mjs',import.meta.url),'utf8');
  const current=fs.readFileSync(new URL('./public_video_gesture_chrome_ready.mjs',import.meta.url),'utf8');
  const begin='export async function dispatchSinglePlayClick';
  assert.equal(old.slice(old.indexOf(begin),old.indexOf('\nasync function main()')).trim(),
    current.slice(current.indexOf(begin),current.indexOf('// The navigation command')).trim());
  assert.equal(current.includes("},5000);pending.set(id"),true);
  assert.equal(current.includes('while(Date.now()-start<55000)'),true);
  assert.equal(current.includes('Date.now()-start<30000'),true);
  assert.equal(current.includes('awaitPromise'),false);
  note('the one-click dispatcher and call/input/observation limits are unchanged');
}
console.log('NAVIGATION_READY_CHECKS checks='+checks+' passed='+checks+' failed=0');
