import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';

// Executes Lumen's shipped getUserMedia implementation against a controlled
// native bridge. These checks cover constraint selection, not camera hardware.
const source = readFileSync(process.argv[2] || 'src/js/prelude/08-window.js', 'utf8');
const start = source.indexOf('/* getUserMedia (real camera/mic)');
const end = source.indexOf('    class BlobEvent', start);
assert(start >= 0 && end > start, 'capture implementation block must exist');
const code = source.slice(start, end) + '\n})();';

function fixture() {
  const calls = {asked: [], camera: [], closed: [], microphones: 0};
  class Track {
    constructor(_token, kind, label) { this._ = {}; this.kind = kind; this.label = label; this.readyState = 'live'; }
    stop() { if (this._.cam) calls.closed.push(this._.cam); this._.cam = 0; this.readyState = 'ended'; }
  }
  class Stream { constructor(tracks) { this.tracks = tracks; } getTracks() { return this.tracks; } }
  const env = {
    EventTarget, Event, DOMException, Promise, Date, setTimeout, clearTimeout,
    MediaStreamTrack: Track, MediaStream: Stream, RTC_INTERNAL: Symbol(), localWindowId: 1,
    location: {origin: 'https://capture.example'}, navigator: {mediaDevices: {}, permissions: {}},
    N: {
      capState: () => 0, capAllowed: () => true,
      capAsk: kind => { calls.asked.push(kind); return true; },
      capCameras: () => ['front camera', 'rear camera', 'USB camera'],
      capMics: () => ['built-in microphone'],
      capCamOpen: (...args) => { calls.camera.push(args); return args[0] + 100; },
      capCamStatus: () => 2, capCamClose: id => calls.closed.push(id),
      capMicOpen: () => ++calls.microphones,
    },
  };
  runInNewContext(code, env, {filename: 'lumen-capture-implementation.js'});
  return {calls, get: constraints => env.navigator.mediaDevices.getUserMedia(constraints)};
}

let checks = 0, failures = 0;
async function check(name, fn) {
  checks++;
  try { await fn(); console.log('PASS', name); }
  catch (error) { failures++; console.error('FAIL', name, error.message); }
}
async function selected(constraint, expected) {
  const f = fixture();
  const stream = await f.get({video: {deviceId: constraint}});
  assert.equal(f.calls.camera.length, 1);
  assert.equal(f.calls.camera[0][0], expected);
  assert.equal(stream.getTracks()[0]._.dev, 'cam' + expected);
  stream.getTracks()[0].stop();
  assert.equal(f.calls.closed.length, 1);
}

await check('plain deviceId selects the requested non-default camera', () => selected('cam1', 1));
await check('exact deviceId selects the requested camera', () => selected({exact: 'cam2'}, 2));
await check('ideal deviceId selects the requested camera', () => selected({ideal: 'cam1'}, 1));
await check('plain deviceId list uses an available preferred camera', () => selected(['missing', 'cam2'], 2));
await check('exact deviceId list chooses an available member', () => selected({exact: ['missing', 'cam1']}, 1));
await check('ideal deviceId list chooses an available member', () => selected({ideal: ['missing', 'cam2']}, 2));
await check('unknown optional deviceId falls back to the available default', () => selected('missing', 0));
await check('unknown ideal deviceId falls back to the available default', () => selected({ideal: 'cam99'}, 0));
await check('mandatory deviceId excludes unavailable hardware before prompting or opening', async () => {
  const f = fixture();
  await assert.rejects(f.get({video: {deviceId: {exact: 'cam99'}}}), error => error.name === 'OverconstrainedError' && error.constraint === 'deviceId');
  assert.equal(f.calls.asked.length, 0);
  assert.equal(f.calls.camera.length, 0);
});
await check('mandatory deviceId list rejects when none of its candidates exist', async () => {
  const f = fixture();
  await assert.rejects(f.get({video: {deviceId: {exact: ['missing', 'cam99']}}}), error => error.name === 'OverconstrainedError' && error.constraint === 'deviceId');
  assert.equal(f.calls.camera.length, 0);
});
await check('exact camera restriction remains mandatory when an ideal camera is also given', async () => {
  await selected({exact: ['cam1', 'cam2'], ideal: 'cam2'}, 2);
});
await check('an ideal camera cannot override an exact restriction', () => selected({exact: 'cam1', ideal: 'cam2'}, 1));
await check('an empty exact list places no restriction on the camera', () => selected({exact: [], ideal: 'cam2'}, 2));
await check('boolean video request preserves the default camera', async () => {
  const f = fixture(); const stream = await f.get({video: true});
  assert.equal(f.calls.camera[0][0], 0); stream.getTracks()[0].stop();
});
console.log(`RESULTS capture-device-selection ${failures ? 'FAIL' : 'PASS'} (${checks - failures}/${checks}; controlled bridge, physical capture untested)`);
process.exitCode = failures ? 1 : 0;
