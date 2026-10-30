'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(process.argv[2], 'utf8');

async function scenario(options = {}) {
  let now = 0, callback = null, callbacksCleared = 0, muted = false;
  const playAt = [], records = [];
  const video = {
    currentTime: 0, readyState: 0, videoWidth: 0, videoHeight: 0,
    paused: true, error: null, currentSrc: '', srcObject: null,
    get muted() { return muted; },
    set muted(value) { if (!options.brokenMute) muted = !!value; },
    total: 0,
    getVideoPlaybackQuality() { return {totalVideoFrames: this.total, droppedVideoFrames: 0}; },
    play() {
      playAt.push({elapsedMs: now, readyState: this.readyState});
      if (this.readyState < 2 || options.readyPlayRejects) {
        this.paused = true;
        return Promise.reject({name: options.readyPlayRejects ? 'NotAllowedError' : 'AbortError'});
      }
      this.paused = false;
      return Promise.resolve();
    },
    pause() { this.paused = true; }
  };
  const window = {addEventListener() {}}; window.top = window;
  const document = {
    hidden: false,
    querySelector(selector) { assert.equal(selector, 'video'); return video; },
    getElementById(id) { assert.equal(id, 'movie_player'); return {classList: {contains(name) {assert.equal(name, 'ad-showing'); return !!options.ad;}}}; }
  };
  const context = {window, document, Date: {now: () => now}, URLSearchParams,
    location: {hostname: 'www.youtube.com', pathname: '/watch', search: '?v=YE7VzlLtp-4'},
    innerWidth: 1024, innerHeight: 724, devicePixelRatio: 1,
    setInterval(fn, interval) {assert.equal(interval, 1000); callback = fn; return 1;},
    clearInterval(id) {assert.equal(id, 1); callback = null; callbacksCleared++;},
    console: {log(line) {assert.ok(line.startsWith('LUME_PUBLIC_VIDEO ')); records.push(JSON.parse(line.slice(18)));}}
  };
  vm.runInNewContext(source, context, {timeout: 1000});
  for (let step=1; step<=46; step++) {
    now = step*1000;
    if (step === 16 && !options.neverReady) {
      video.readyState = 4; video.videoWidth = 640; video.videoHeight = 360;
      video.currentSrc = 'blob:https://www.youtube.com/SYNTHETIC_PRIVATE_SOURCE_NEVER_REPORT';
      // Model the page's observed initialization overwrite, not an engine bug.
      muted = false;
    }
    if (step === 20) muted = false;
    if (!video.paused && video.readyState >= 2) {
      video.currentTime += 1;
      if (!options.zeroFrames) video.total += 24;
    }
    if (callback) callback();
    await Promise.resolve();
  }
  return {final: records.at(-1), records, playAt, callbacksCleared, video};
}

(async () => {
  let passed = 0, failed = 0;
  async function check(name, fn) {
    try {await fn(); passed++; console.log('PASS '+name);}
    catch (error) {failed++; console.log('FAIL '+name+': '+error.message);}
  }
  await check('media readiness prevents the observed premature AbortError', async () => {
    const r = await scenario();
    assert.equal(r.playAt.length, 1);
    assert.ok(r.playAt[0].readyState >= 2 && r.playAt[0].elapsedMs >= 16000);
    assert.equal(r.final.playRejected, false);
    assert.equal(r.final.state, 'video-frame-progress-observed');
  });
  await check('page mute overwrite is reasserted and independently read back', async () => {
    const r = await scenario();
    assert.equal(r.final.muted, true);
    assert.equal(r.final.muteWriteFailureCount, 0);
    assert.ok(r.final.unmutedBeforeEnforcementSamples >= 2);
    assert.ok(r.final.muteWriteCount >= 2);
  });
  await check('a broken native muted setter cannot be treated as success', async () => {
    const r = await scenario({brokenMute: true});
    assert.equal(r.final.muted, false);
    assert.ok(r.final.muteWriteFailureCount > 0);
    assert.equal(r.final.state, 'mute-not-applied');
  });
  await check('moving time cannot replace actual frame-counter progress', async () => {
    const r = await scenario({zeroFrames: true});
    assert.ok(r.final.currentTimeDeltaSeconds >= 3);
    assert.ok(r.final.playbackProgressSamples >= 3);
    assert.equal(r.final.totalFrameCountDelta, 0);
    assert.equal(r.final.nonDroppedFrameCountDelta, 0);
    assert.equal(r.final.state, 'playback-not-verified');
  });
  await check('a genuine ready-state play rejection stays a failure without retry', async () => {
    const r = await scenario({readyPlayRejects: true});
    assert.equal(r.playAt.length, 1);
    assert.equal(r.final.playRejected, true);
    assert.equal(r.final.rejectionName, 'NotAllowedError');
    assert.equal(r.final.state, 'play-rejected');
  });
  await check('never-ready media remains bounded and never gets a premature play', async () => {
    const r = await scenario({neverReady: true});
    assert.equal(r.playAt.length, 0);
    assert.equal(r.final.playAttempted, false);
    assert.equal(r.final.state, 'waiting-for-media-data');
    assert.equal(r.final.resourcesReleased, true);
  });
  await check('cleanup and media source classification disclose no URL', async () => {
    const r = await scenario();
    assert.equal(r.final.resourcesReleased, true);
    assert.equal(r.final.paused, true);
    assert.equal(r.callbacksCleared, 1);
    assert.equal(r.final.videoSourceKind, 'blob');
    assert.equal(r.final.probeRevision, 3);
    assert.equal(JSON.stringify(r.records).includes('SYNTHETIC_PRIVATE_SOURCE_NEVER_REPORT'), false);
  });
  console.log(JSON.stringify({fixture:'synthetic media lifecycle only',checks:passed+failed,passed,failed,
    browserUsed:false,networkUsed:false,realFramesClaimed:false}));
  process.exitCode = failed ? 1 : 0;
})();
