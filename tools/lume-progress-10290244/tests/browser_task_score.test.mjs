import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';

// Execute the shipped browser module with inert DOM/network surfaces. Checking
// the rendered text catches stale score attribution without duplicating its
// selection expression or exporting test-only browser code.
async function browser() {
  const nodes = new Map();
  const node = () => ({textContent: '', className: '', disabled: false, value: '',
    append() {}, replaceChildren() {}, addEventListener() {}});
  const document = {title: '', createElement: node,
    getElementById(id) {if (!nodes.has(id)) nodes.set(id, node()); return nodes.get(id);}};
  const context = vm.createContext({document,
    location: {origin: 'https://progress.example', hash: '', pathname: '/', search: ''},
    sessionStorage: {getItem() {return null;}, setItem() {}}, history: {replaceState() {}},
    URL, URLSearchParams, performance, crypto,
    fetch: async url => ({json: async () => url === '/config.json'
      ? {taskId: 'transport-id', liveBase: ''} : {status: {}}}),
    WebSocket: class {close() {}}, setInterval() {return 0;}, clearInterval() {},
    setTimeout() {return 0;}, clearTimeout() {}});
  const source = await fs.readFile(new URL('../public/app.js', import.meta.url), 'utf8');
  await vm.runInContext(`(async()=>{${source}\n globalThis.__render=render;})()`, context);
  await Promise.resolve();
  return {render: context.__render, score: () => nodes.get('score').textContent};
}

test('new active task displays its own inherited score on the reused route', async () => {
  const view = await browser();
  view.render({status: {taskId: 'transport-id', activeTaskId: '10300110', iteration: 2,
    currentTaskScorecard: {task_id: '10300110', score: 66.426948, accepted_artifact_status: 'inherited_baseline'}},
    scorecard: {task_id: 'transport-id', score: 55}});
  assert.equal(view.score(), 'Iteration 2 · 66.43/100 inherited task gates');
});

test('explicit task identity rejects a stale nested score and transport score', async () => {
  const view = await browser();
  view.render({status: {taskId: 'transport-id', activeTaskId: '10300110', iteration: 3,
    currentTaskScorecard: {task_id: '10292019r', score: 98}},
    scorecard: {task_id: 'transport-id', score: 98}});
  assert.equal(view.score(), 'Iteration 3 · —/100 task gates');
});

test('matching active task score may arrive from the snapshot scorecard', async () => {
  const view = await browser();
  view.render({status: {taskId: 'transport-id', activeTaskId: '10300110', iteration: 4},
    scorecard: {task_id: '10300110', score: 67}});
  assert.equal(view.score(), 'Iteration 4 · 67.00/100 task gates');
});

test('previous published task remains readable before explicit handoff', async () => {
  const view = await browser();
  view.render({status: {taskId: 'transport-id', iteration: 1,
    currentTaskScorecard: {task_id: '10292019r', score: 66.426948,
      accepted_artifact_status: 'inherited_baseline'}},
    scorecard: {task_id: 'transport-id', score: 55}});
  assert.equal(view.score(), 'Iteration 1 · 66.43/100 inherited task gates');
});

test('unmatched snapshot cannot create a score when no nested task exists', async () => {
  const view = await browser();
  view.render({status: {taskId: 'transport-id', iteration: 5},
    scorecard: {task_id: 'unrelated-task', score: 98}});
  assert.equal(view.score(), 'Iteration 5 · —/100 task gates');
});
