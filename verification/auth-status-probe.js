(function () {
  // This probe emits only booleans/counts from public UI structure, never names,
  // account identifiers, cookies, query strings, DOM text or token material.
  var start = Date.now(), previousTime = null, progressed = false;
  function exists(selector) { try { return !!document.querySelector(selector); } catch (_) { return false; } }
  function collect() {
    var hostname = location.hostname;
    var site = hostname === 'chatgpt.com' ? 'chatgpt' : /(^|\.)youtube\.com$/.test(hostname) ? 'youtube' : /(^|\.)google\.com$/.test(hostname) ? 'google' : /(^|\.)x\.com$/.test(hostname) ? 'x' : 'other';
    var account = false, signIn = false, loggedInFlag = null;
    if (site === 'google') {
      account = exists('a[href*="SignOutOptions"],a[href*="Logout"],a[aria-label^="Google Account:"]');
      signIn = exists('a[href*="ServiceLogin"],a[href*="signin/v2"]');
    } else if (site === 'youtube') {
      account = exists('#avatar-btn img,button#avatar-btn');
      signIn = exists('ytd-masthead a[href*="ServiceLogin"]');
      try { var flag = window.ytcfg && typeof ytcfg.get === 'function' ? ytcfg.get('LOGGED_IN') : null; if(typeof flag === 'boolean')loggedInFlag=flag; } catch (_) {}
    } else if (site === 'x') {
      account = exists('[data-testid="SideNav_AccountSwitcher_Button"]');
      signIn = exists('a[href="/login"],a[href="/i/flow/login"]');
    } else if (site === 'chatgpt') {
      account = exists('[data-testid="profile-button"],[data-testid="accounts-profile-button"]');
      signIn = exists('[data-testid="login-button"],[data-testid="signup-button"]');
    }
    var composer = document.querySelector('#prompt-textarea,[data-testid="prompt-textarea"]');
    var video = document.querySelector('video');
    if(video && typeof video.currentTime === 'number') { if(previousTime !== null && video.currentTime > previousTime + .05) progressed = true; previousTime = video.currentTime; }
    var result = {schema:1,site:site,elapsedMs:Date.now()-start,readyState:document.readyState,
      uiAccountIndicator:account,uiSignInIndicator:signIn,providerLoggedInFlag:loggedInFlag,
      composerPresent:!!composer,composerEditable:!!composer && (composer.isContentEditable || (!composer.disabled && /^(TEXTAREA|INPUT)$/.test(composer.tagName))),
      videoPresent:!!video,videoReady:!!video && video.readyState >= 2,playbackProgressed:progressed,
      pageHasContent:!!document.body && document.body.children.length > 0,
      documentHidden:!!document.hidden,viewportWidth:innerWidth,viewportHeight:innerHeight,devicePixelRatio:devicePixelRatio};
    return result;
  }
  window.LumePrivateStatus={snapshot:collect};
  function report(){console.log('LUME_AUTH_STATUS '+JSON.stringify(collect()));}
  [5000,15000,30000,45000].forEach(function(delay){setTimeout(report,delay);});
}());

