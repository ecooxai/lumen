import test from 'node:test';
import assert from 'node:assert/strict';
import { ReportStore, ResourceScope, errorState, within, validateEcho } from '../core.mjs';

test('reports retain unsupported and blocked as distinct from failures', () => {
  const report = new ReportStore(['camera', 'gpu', 'wasm']);
  report.set('camera', 'blocked', 'permission denied');
  report.set('gpu', 'unsupported', 'API missing');
  report.set('wasm', 'pass', '42', { actual: 42 });
  assert.deepEqual(report.summary(), { total: 3, pass: 1, fail: 0, blocked: 1, unsupported: 1, idle: 0, running: 0, cancelled: 0, configuration: 0 });
  assert.throws(() => report.set('camera', 'pass-ish', 'invalid'), /state/);
});

test('scope cleanup is idempotent, reverse ordered, and late resources close immediately', () => {
  const scope = new ResourceScope();
  const calls = [];
  scope.add(() => calls.push('first'));
  scope.add(() => calls.push('second'));
  scope.dispose(); scope.dispose();
  scope.add(() => calls.push('late'));
  assert.deepEqual(calls, ['second', 'first', 'late']);
  assert.equal(scope.size, 0);
});

test('one throwing cleanup does not prevent other cleanup', () => {
  const scope = new ResourceScope(); let closed = false;
  scope.add(() => { closed = true; });
  scope.add(() => { throw Error('device already lost'); });
  scope.dispose(); assert.equal(closed, true);
});

test('timeout closes scope and does not leave a live timer', async () => {
  const scope = new ResourceScope(); let stopped = false;
  scope.add(() => { stopped = true; });
  await assert.rejects(within(new Promise(() => {}), 5, scope), /timed out/);
  assert.equal(stopped, true);
  assert.equal(scope.size, 0);
});

test('echo verification requires exact received payload', () => {
  assert.equal(validateEcho('lume-test', 'lume-test'), true);
  assert.throws(() => validateEcho('lume-test', 'unexpected'), /mismatch/);
});

test('manual stop immediately cancels pending operations and their timeout',async()=>{
  const scope=new ResourceScope();const pending=within(new Promise(()=>{}),10000,scope);
  scope.cancel();await assert.rejects(pending,{name:'AbortError'});assert.equal(scope.size,0);
});

test('permission denial is blocked, unavailable hardware is blocked, actual errors fail', () => {
  assert.equal(errorState({ name: 'NotAllowedError' }), 'blocked');
  assert.equal(errorState({ name: 'NotFoundError' }), 'blocked');
  assert.equal(errorState({ name: 'Error' }), 'fail');
});
