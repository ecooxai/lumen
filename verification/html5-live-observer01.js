/* Passive read-only observer for the unmodified public html5test.co page. */
(function () {
  var started = Date.now(), errors = [], previous = null, stable = 0, announced = false;
  window.addEventListener('error', function (event) {
    if (errors.length < 12) errors.push({message:String(event.message || 'resource-error').slice(0,240),
      source:event.filename ? String(event.filename).split('?')[0].slice(0,240) : null});
  }, true);
  function inspect() {
    var scoreNode = document.getElementById('score'), contents = document.getElementById('contents');
    var results = document.getElementById('results'), loading = document.getElementById('loading');
    var scoreText = scoreNode ? String(scoreNode.innerText || scoreNode.textContent || '').replace(/\s+/g,' ').trim() : '';
    var match = /Your browser scores\s+(\d+)\s+out of\s+(\d+)\s+points/i.exec(scoreText);
    var style = contents ? getComputedStyle(contents) : null;
    var loaderStyle = loading ? getComputedStyle(loading) : null;
    var resultText = results ? String(results.innerText || results.textContent || '') : '';
    var value = {schemaVersion:1,case:'html5test.co-live',elapsedMs:Date.now()-started,
      url:location.href,title:document.title,userAgent:navigator.userAgent,readyState:document.readyState,
      score:match ? Number(match[1]) : null,maximum:match ? Number(match[2]) : null,
      scoreText:scoreText.slice(0,300),visibleResults:!!style && style.visibility!=='hidden' && style.display!=='none',
      loaderHidden:!!loaderStyle && loaderStyle.display==='none',resultTextLength:resultText.length,
      globals:{WhichBrowser:typeof WhichBrowser,Browsers:typeof Browsers,Test:typeof Test},
      viewport:{width:innerWidth,height:innerHeight},documentHidden:document.hidden,errors:errors.slice(),
      detectorScriptPresent:Array.prototype.some.call(document.scripts,function(s){return /^https:\/\/api\.whichbrowser\.net\/rel\/detect\.js(?:\?|$)/.test(s.src);}),
      observerChangedBenchmark:false};
    var complete = location.origin==='https://html5test.co' && location.pathname==='/' &&
      document.readyState==='complete' && value.score!==null && value.maximum>0 && value.score<=value.maximum &&
      value.visibleResults && value.loaderHidden && value.resultTextLength>100 &&
      value.globals.WhichBrowser==='function' && value.globals.Browsers==='object' && value.globals.Test==='function';
    var key=String(value.score)+'/'+String(value.maximum);
    stable=complete && key===previous ? stable+1 : complete ? 1 : 0;previous=key;
    value.complete=complete;value.stableObservations=stable;
    if (stable>=2 && !announced) {
      announced=true;value.stage='completed';value.resultText=resultText.slice(0,30000);
      console.log('LUME_HTML5_LIVE '+JSON.stringify(value));
      console.log('LUME_CAPTURE_REQUEST overview');
    } else if ((Date.now()-started)<2500 || Math.floor((Date.now()-started)/1000)%10===0) {
      value.stage='observing';console.log('LUME_HTML5_LIVE '+JSON.stringify(value));
    }
    if (Date.now()-started<58000) setTimeout(inspect,1000);
    else {value.stage='final';console.log('LUME_HTML5_LIVE '+JSON.stringify(value));}
  }
  setTimeout(inspect,0);
})();
