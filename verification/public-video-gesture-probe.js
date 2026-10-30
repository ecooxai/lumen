(function () {
  'use strict';
  // Fixed public watch page only. No play() call, DOM click(), synthetic event,
  // consent/account/ad action, or URL/title/account data is retained.
  if (window.top !== window) return;
  var started = Date.now(), video = null, timer = null, completed = false;
  var changed = false, adObserved = false, selected = null, armed = false;
  var trustedClicks = 0, untrustedClicks = 0, clickedAt = null, playingAfter = false;
  var muteWrites = 0, muteFailures = 0, unmuted = 0, timeline = [], omitted = 0;
  var baseTime = null, lastTime = null, baseCounts = null, lastCounts = null;
  var progress = 0, firstProgress = null, lastProgress = null, counter = null;
  var state = 'waiting-for-video', cleanupStarted = false;
  var events = ['play','playing','pause','waiting','stalled','ended','error','loadeddata'];
  function elapsed() { return Date.now() - started; }
  function allowed() {
    return /(^|\.)youtube\.com$/.test(location.hostname) && location.pathname === '/watch' &&
      new URLSearchParams(location.search).get('v') === 'YE7VzlLtp-4';
  }
  function counts() {
    if (!video) return null;
    try {
      var q = video.getVideoPlaybackQuality();
      if (Number.isInteger(q.totalVideoFrames) && Number.isInteger(q.droppedVideoFrames) &&
          q.droppedVideoFrames >= 0 && q.totalVideoFrames >= q.droppedVideoFrames) {
        counter = 'getVideoPlaybackQuality'; return {total:q.totalVideoFrames,dropped:q.droppedVideoFrames};
      }
    } catch (_) {}
    return null;
  }
  function sourceKind() {
    if (!video) return 'none';
    try {
      if (video.srcObject) return 'provider-object';
      var value = String(video.currentSrc || '');
      return !value ? 'none' : /^blob:/i.test(value) ? 'blob' : /^https?:/i.test(value) ? 'http' : 'other';
    } catch (_) { return 'unavailable'; }
  }
  function eventRow(type, trusted, measuredTime) {
    var row = {type:type,elapsedMs:measuredTime === undefined ? elapsed() : measuredTime,isTrusted:trusted === true,cleanup:cleanupStarted,
      paused:video ? !!video.paused : null,muted:video ? !!video.muted : null,
      readyState:video ? video.readyState : null,currentTime:video && Number.isFinite(video.currentTime) ? video.currentTime : null};
    if (timeline.length < 24) timeline.push(row); else omitted++;
  }
  function onMedia(event) {
    if (event.type === 'playing' && event.isTrusted === true && clickedAt !== null && !cleanupStarted) playingAfter = true;
    eventRow(event.type,event.isTrusted);
  }
  function enforceMuted() {
    if (video && video.muted !== true) {
      unmuted++; muteWrites++;
      try { video.muted = true; } catch (_) {}
      if (video.muted !== true) muteFailures++;
    }
  }
  function collect() {
    var current = document.querySelector('video');
    if (!allowed()) state = 'unexpected-page';
    else if (!completed && !video && current) {
      video = current; state = 'waiting-for-play-control';
      events.forEach(function (name) { video.addEventListener(name,onMedia); });
      eventRow('bound',false);
    }
    if (video && current !== video) changed = true;
    var player = document.getElementById('movie_player');
    if (player && player.classList && player.classList.contains('ad-showing')) adObserved = true;
    if (!completed) enforceMuted();
    if (video && !completed && clickedAt !== null) {
      var t = Number.isFinite(video.currentTime) ? video.currentTime : null;
      if (t !== null && lastTime !== null && t > lastTime + 0.05) {
        progress++; if (firstProgress === null) firstProgress = elapsed(); lastProgress = elapsed();
      }
      if (t !== null) lastTime = t;
      var c = counts(); if (c) lastCounts = c;
    }
    var total = baseCounts && lastCounts ? lastCounts.total - baseCounts.total : null;
    var dropped = baseCounts && lastCounts ? lastCounts.dropped - baseCounts.dropped : null;
    return {schema:1,probeRevision:1,state:state,elapsedMs:elapsed(),targetPageMatched:allowed(),
      videoPresent:!!video,videoElementChanged:changed,adPlaybackObserved:adObserved,
      clickArmed:armed,trustedControlClickCount:trustedClicks,untrustedControlClickCount:untrustedClicks,
      trustedClickElapsedMs:clickedAt,playingAfterTrustedClick:playingAfter,
      selectedControl:selected ? selected.kind : null,programmaticPlayCalled:false,
      readyState:video ? video.readyState : null,mediaErrorCode:video && video.error ? video.error.code : null,
      paused:video ? !!video.paused : null,muted:video ? !!video.muted : null,
      videoWidth:video ? video.videoWidth : 0,videoHeight:video ? video.videoHeight : 0,
      videoSourceKind:sourceKind(),muteWriteCount:muteWrites,muteWriteFailureCount:muteFailures,
      unmutedBeforeEnforcementSamples:unmuted,currentTimeDeltaSeconds:baseTime !== null && lastTime !== null ? lastTime - baseTime : null,
      playbackProgressSamples:progress,firstProgressElapsedMs:firstProgress,lastProgressElapsedMs:lastProgress,
      totalFrameCountDelta:total,droppedFrameCountDelta:dropped,
      nonDroppedFrameCountDelta:total !== null && dropped !== null ? total - dropped : null,
      videoFrameCounter:counter,viewportWidth:innerWidth,viewportHeight:innerHeight,devicePixelRatio:devicePixelRatio,
      documentHidden:!!document.hidden,resourcesReleased:completed && timer === null && (!video || video.paused),
      eventTimeline:timeline.slice(),eventsOmitted:omitted,actualCompositorFramesVerified:false,
      accountActionsPerformed:false,physicalInputVerified:false};
  }
  function control() {
    collect();
    if (completed || !allowed() || !video || changed || adObserved || document.hidden || muteFailures) return {eligible:false,reason:'unsafe-or-unavailable-page'};
    if (armed) return {eligible:false,reason:'already-armed'};
    if (!video.paused) return {eligible:false,reason:'already-playing'};
    if (video.readyState < 2 || video.videoWidth <= 0 || video.videoHeight <= 0) return {eligible:false,reason:'media-not-ready'};
    var player = document.getElementById('movie_player');
    if (!player) return {eligible:false,reason:'no-player'};
    var kinds = [['large-play','.ytp-large-play-button'],['transport-play','.ytp-play-button']];
    for (var i = 0; i < kinds.length; i++) {
      var button = player.querySelector(kinds[i][1]);
      if (!button || button.tagName !== 'BUTTON' || button.disabled ||
          !/^play(?:\b|\s|\()/i.test(String(button.getAttribute('aria-label') || button.getAttribute('title') || ''))) continue;
      var shown = true;
      for (var node = button, depth = 0; node && depth < 12; node = node.parentElement, depth++) {
        var css = getComputedStyle(node);
        if (css.display === 'none' || css.visibility !== 'visible' || Number(css.opacity) <= 0) shown = false;
        if (node === player) break;
      }
      var r = button.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2;
      if (!shown || r.width < 2 || r.height < 2 || !Number.isFinite(x) || !Number.isFinite(y) ||
          x <= 0 || y <= 0 || x >= innerWidth || y >= innerHeight) continue;
      var top = document.elementFromPoint(x,y);
      if (top !== button && !button.contains(top)) continue;
      return {eligible:true,reason:'visible-unobscured-play-control',kind:kinds[i][0],x:x,y:y,button:button};
    }
    return {eligible:false,reason:'play-control-unavailable'};
  }
  function target(arm) {
    var found = control();
    if (arm && found.eligible) { selected = found; armed = true; state = 'input-armed'; }
    return {eligible:found.eligible,reason:found.reason,control:found.kind || null,x:found.x ?? null,y:found.y ?? null,
      readyState:video ? video.readyState : null,paused:video ? !!video.paused : null,muted:video ? !!video.muted : null};
  }
  function onClick(event) {
    if (completed || !allowed() || !armed || !selected || !video || changed || adObserved) return;
    if (event.target !== selected.button && !selected.button.contains(event.target)) return;
    if (!event.isTrusted) { untrustedClicks++; eventRow('control-click',false); return; }
    var clickTime = elapsed(); trustedClicks++;
    if (trustedClicks === 1) {
      clickedAt = clickTime; baseTime = lastTime = Number.isFinite(video.currentTime) ? video.currentTime : null;
      baseCounts = lastCounts = counts(); state = 'trusted-control-clicked';
    }
    eventRow('control-click',true,clickTime);
  }
  function stop() {
    if (completed) return collect();
    var before = collect();
    state = before.targetPageMatched && trustedClicks === 1 && untrustedClicks === 0 && playingAfter &&
      !changed && !adObserved && muteFailures === 0 && before.mediaErrorCode === null &&
      before.currentTimeDeltaSeconds >= 3 && before.playbackProgressSamples >= 3 && before.nonDroppedFrameCountDelta >= 30 ?
      'video-frame-progress-observed' : 'playback-not-verified';
    cleanupStarted = true;
    if (timer !== null) { clearInterval(timer); timer = null; }
    try { if (video) video.pause(); } catch (_) {}
    eventRow('cleanup',false);
    if (video) events.forEach(function (name) { video.removeEventListener(name,onMedia); });
    document.removeEventListener('click',onClick,true);
    window.removeEventListener('pagehide',stop);
    completed = true;
    return collect();
  }
  window.LumePublicVideoGesture = {snapshot:collect,target:function () { return target(false); },arm:function () { return target(true); },stop:stop};
  document.addEventListener('click',onClick,true);
  window.addEventListener('pagehide',stop,{once:true});
  timer = setInterval(function () { collect(); if (elapsed() >= 45000) stop(); },1000);
}());
