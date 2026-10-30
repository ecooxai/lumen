(function () {
  'use strict';
  // Fixed, fresh-profile public watch page only. This never clicks account UI,
  // advertisements, consent choices, or links, and never sends a message.
  if (window.top !== window) return;
  var started = Date.now(), video = null, timer = null, attempted = false;
  var firstTime = null, lastTime = null, firstFrames = null, lastFrames = null;
  var firstDropped = null, lastDropped = null, adObserved = false;
  var frameSource = null, changes = 0, reports = {}, completed = false;
  var playResolved = false, playRejected = false, rejection = null;
  var firstProgress = null, lastProgress = null, videoChanged = false;
  var playRequestElapsed = null, readyStateAtPlayRequest = null;
  var muteWrites = 0, muteWriteFailures = 0, unmutedSamples = 0;
  var result = {schema: 1, probeRevision: 3, state: 'waiting-for-video'};
  function allowedPage() {
    return /(^|\.)youtube\.com$/.test(location.hostname) &&
      location.pathname === '/watch' && new URLSearchParams(location.search).get('v') === 'YE7VzlLtp-4';
  }
  function countFrames(element) {
    try {
      if (typeof element.getVideoPlaybackQuality === 'function') {
        var quality = element.getVideoPlaybackQuality();
        if (Number.isFinite(quality.totalVideoFrames) && Number.isFinite(quality.droppedVideoFrames) &&
            quality.totalVideoFrames >= 0 && quality.droppedVideoFrames >= 0 && quality.droppedVideoFrames <= quality.totalVideoFrames) {
          frameSource = 'getVideoPlaybackQuality'; return {total:quality.totalVideoFrames,dropped:quality.droppedVideoFrames};
        }
      }
      if (Number.isFinite(element.webkitDecodedFrameCount) && Number.isFinite(element.webkitDroppedFrameCount) &&
          element.webkitDecodedFrameCount >= 0 && element.webkitDroppedFrameCount >= 0) {
        frameSource = 'webkitDecodedFrameCount'; return {total:element.webkitDecodedFrameCount,dropped:element.webkitDroppedFrameCount};
      }
    } catch (_) {}
    return null;
  }
  function sourceKind(element) {
    if (!element) return 'none';
    try {
      if (element.srcObject) return 'provider-object';
      var source = String(element.currentSrc || '');
      if (!source) return 'none';
      if (/^blob:/i.test(source)) return 'blob';
      if (/^https?:/i.test(source)) return 'http';
      if (/^data:/i.test(source)) return 'data';
      return 'other';
    } catch (_) { return 'unavailable'; }
  }
  function enforceMuted() {
    if (!video) return;
    // Page initialization may overwrite the initial muted value. Read back
    // every actual write so a broken media binding remains an explicit failure.
    if (video.muted !== true) {
      unmutedSamples++; muteWrites++;
      try { video.muted = true; } catch (_) {}
      if (video.muted !== true) muteWriteFailures++;
    }
  }
  function collect() {
    var elapsed = Date.now() - started;
    var current = document.querySelector('video');
    if (!allowedPage()) result.state = 'unexpected-page';
    else if (!completed && !video && current) {
      video = current; firstTime = Number.isFinite(video.currentTime) ? video.currentTime : null;
      lastTime = firstTime;
      var initialCounts = countFrames(video);
      if (initialCounts) {firstFrames = initialCounts.total; firstDropped = initialCounts.dropped;}
      lastFrames = firstFrames; lastDropped = firstDropped;
      result.state = 'waiting-for-media-data';
    }
    if (video && current !== video) videoChanged = true;
    enforceMuted();
    // The observed Chrome request at HAVE_NOTHING was aborted by the site's
    // subsequent media initialization. Issue the one owned play request only
    // after current media data and intrinsic video dimensions are available.
    // No retry clears an earlier rejection, and no user activation is forged.
    if (allowedPage() && !completed && video && !videoChanged && !attempted &&
        video.readyState >= 2 && video.videoWidth > 0 && video.videoHeight > 0) {
      attempted = true; playRequestElapsed = elapsed; readyStateAtPlayRequest = video.readyState;
      result.state = 'play-requested';
      try {
        var request = video.play();
        if (request && typeof request.then === 'function') request.then(function () { playResolved = true; }, function (error) {
          playRejected = true;
          rejection = error && ['NotAllowedError','NotSupportedError','AbortError','SecurityError'].indexOf(error.name) !== -1 ? error.name : 'OtherError';
        });
      } catch (_) { playRejected = true; rejection = 'OtherError'; }
    }
    var player = document.getElementById('movie_player');
    if (player && player.classList && player.classList.contains('ad-showing')) adObserved = true;
    if (video && !completed) {
      var t = Number.isFinite(video.currentTime) ? video.currentTime : null;
      if (t !== null && lastTime !== null && t > lastTime + 0.05) {
        changes++; if (firstProgress === null) firstProgress = elapsed; lastProgress = elapsed;
      }
      if (t !== null) lastTime = t;
      var f = countFrames(video); if (f !== null) {lastFrames = f.total; lastDropped = f.dropped;}
    }
    var timeDelta = firstTime !== null && lastTime !== null ? lastTime - firstTime : null;
    var frameDelta = firstFrames !== null && lastFrames !== null ? lastFrames - firstFrames : null;
    var droppedDelta = firstDropped !== null && lastDropped !== null ? lastDropped - firstDropped : null;
    return Object.assign({}, result, {
      schema: 1, probeRevision: 3, elapsedMs: elapsed, targetPageMatched: allowedPage(),
      videoPresent: !!video, videoElementChanged: videoChanged, adPlaybackObserved: adObserved, playAttempted: attempted,
      playPromiseResolved: playResolved, playRejected: playRejected, rejectionName: rejection,
      readyState: video ? video.readyState : null, mediaErrorCode: video && video.error ? video.error.code : null,
      playRequestElapsedMs: playRequestElapsed, readyStateAtPlayRequest: readyStateAtPlayRequest,
      muteWriteCount: muteWrites, muteWriteFailureCount: muteWriteFailures, unmutedBeforeEnforcementSamples: unmutedSamples,
      videoSourceKind: sourceKind(video),
      videoWidth: video ? video.videoWidth : 0, videoHeight: video ? video.videoHeight : 0,
      paused: video ? !!video.paused : null, muted: video ? !!video.muted : null,
      currentTimeDeltaSeconds: timeDelta, playbackProgressSamples: changes,
      firstProgressElapsedMs: firstProgress, lastProgressElapsedMs: lastProgress,
      totalFrameCountDelta: frameDelta, droppedFrameCountDelta: droppedDelta,
      nonDroppedFrameCountDelta: frameDelta !== null && droppedDelta !== null ? frameDelta - droppedDelta : null,
      videoFrameCounter: frameSource,
      viewportWidth: innerWidth, viewportHeight: innerHeight, devicePixelRatio: devicePixelRatio,
      documentHidden: !!document.hidden, resourcesReleased: completed && timer === null && (!video || video.paused),
      actualCompositorFramesVerified: false, accountActionsPerformed: false,
      scope: 'fresh public watch page; muted programmatic play; no physical input or audio-output proof'
    });
  }
  function finish() {
    if (completed) return;
    var before = collect();
    result.state = !before.targetPageMatched ? 'unexpected-page' : !video ? 'no-video' :
      playRejected ? 'play-rejected' : videoChanged ? 'video-element-replaced' : adObserved ? 'ad-playback-observed' :
      muteWriteFailures > 0 ? 'mute-not-applied' : !attempted ? 'waiting-for-media-data' :
      before.currentTimeDeltaSeconds >= 3 && before.playbackProgressSamples >= 3 && before.nonDroppedFrameCountDelta >= 30 &&
      before.videoWidth > 0 && before.videoHeight > 0 && before.mediaErrorCode === null ? 'video-frame-progress-observed' : 'playback-not-verified';
    if (timer !== null) { clearInterval(timer); timer = null; }
    try { if (video) video.pause(); } catch (_) {}
    completed = true;
    console.log('LUME_PUBLIC_VIDEO ' + JSON.stringify(collect()));
  }
  window.LumePublicVideo = {snapshot: collect, stop: finish};
  timer = setInterval(function () {
    var snapshot = collect();
    for (var i = 0; i < 4; i++) {
      var delay = [5000,15000,30000,44000][i];
      if (snapshot.elapsedMs >= delay && !reports[delay]) { reports[delay] = true; console.log('LUME_PUBLIC_VIDEO ' + JSON.stringify(snapshot)); }
    }
    if (snapshot.elapsedMs >= 45000) finish();
  }, 1000);
  window.addEventListener('pagehide', finish, {once: true});
}());
