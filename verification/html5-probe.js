(function () {
  window.addEventListener('error',function(event){console.log('LUME_HTML5_ERROR '+JSON.stringify({message:event.message||null,filename:event.filename||null,resource:event.target&&event.target.src?String(event.target.src).slice(0,500):null,line:event.lineno||null}));},true);
  window.addEventListener('unhandledrejection',function(event){console.log('LUME_HTML5_REJECTION '+JSON.stringify({message:String(event.reason).slice(0,600)}));});
  function report(delay) {
    try {
      var text = document.body ? (document.body.innerText || document.body.textContent || '') : '';
      var found = /scores\s+(\d+)\s+out of\s+(\d+)/i.exec(text);
      var scoreNode = document.querySelector('#score');
      console.log('LUME_EVIDENCE ' + JSON.stringify({case: 'html5test-co', delayMs: delay,
        url: location.href, title: document.title, readyState: document.readyState,
        score: found ? Number(found[1]) : null, maximum: found ? Number(found[2]) : null,
        scoreElementText: scoreNode ? scoreNode.textContent.slice(0, 400) : null,
        userAgent: navigator.userAgent, viewport:{width:innerWidth,height:innerHeight},
        globals:{WhichBrowser:typeof WhichBrowser,Browsers:typeof Browsers,Test:typeof Test},
        scripts:Array.from(document.scripts).map(function(s){return s.src?s.src.split('?')[0]:'(inline)'}).slice(-8),
        pageExcerpt: text.slice(0, 450)}));
    } catch (error) { console.log('LUME_EVIDENCE ' + JSON.stringify({case:'html5test-co',error:String(error)})); }
  }
  [6000, 16000, 29000].forEach(function(delay){setTimeout(function(){report(delay)},delay)});
}());

