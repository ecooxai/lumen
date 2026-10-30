/* Independent async-event diagnostic. This is not the HTML5 benchmark. */
(function () {
  'use strict';
  if (window.top !== window) return;
  var parsed = new URL(location.href), nonce = parsed.searchParams.get('nonce');
  if (parsed.hostname !== '127.0.0.1' || parsed.pathname !== '/event-diagnostic' || !/^[a-f0-9]{24}$/.test(nonce || '')) return;
  var began = Date.now(), records = [], requests = [], retainedImages = [];
  var FIXTURES = [{"id":"png","url":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNQaPjwHwAE5AKQBYzgxgAAAABJRU5ErkJggg==","sha256":"a9e86505a5ed5f5fbaca0521dfc4e5fa2efa7367a7d9445bb74cabd0ba25bcd2","origin":"generated one-pixel control"},{"id":"webp","url":"data:image/webp;base64,UklGRhoAAABXRUJQVlA4TA0AAAAvAAAAEAcQERGIiP4HAA==","sha256":"86be52bdb7547413cafb3ed175a806a798c65de98b40849e0b974c47d187de65","origin":"unchanged upstream engine fixture"},{"id":"jxl","url":"data:image/jxl;base64,/woIELASCAgQAFwASxLFgkWAHL0xqnCBCV0qDp901Te/5QM=","sha256":"1c9c7f9d374c2979fa85c8f6cd9bf74cda07318996ded2bbad78c56e0d2c15b0","origin":"unchanged upstream engine fixture"},{"id":"avif","url":"data:image/avif;base64,AAAAIGZ0eXBhdmlmAAAAAGF2aWZtaWYxbWlhZk1BMUIAAADybWV0YQAAAAAAAAAoaGRscgAAAAAAAAAAcGljdAAAAAAAAAAAAAAAAGxpYmF2aWYAAAAADnBpdG0AAAAAAAEAAAAeaWxvYwAAAABEAAABAAEAAAABAAABGgAAAB0AAAAoaWluZgAAAAAAAQAAABppbmZlAgAAAAABAABhdjAxQ29sb3IAAAAAamlwcnAAAABLaXBjbwAAABRpc3BlAAAAAAAAAAIAAAACAAAAEHBpeGkAAAAAAwgICAAAAAxhdjFDgQ0MAAAAABNjb2xybmNseAACAAIAAYAAAAAXaXBtYQAAAAAAAAABAAEEAQKDBAAAACVtZGF0EgAKCBgANogQEAwgMg8f8D///8WfhwB8+ErK42A=","sha256":"04dfc8e1a6e59e36a3a3a5c15443a97a92a6d2a7da9d276b050d58be3c7952a7","origin":"unchanged upstream engine fixture"},{"id":"heic","url":"data:image/heic;base64,AAAAGGZ0eXBoZWljAAAAAG1pZjFoZWljAAAC/G1ldGEAAAAAAAAAIWhkbHIAAAAAAAAAAHBpY3QAAAAAAAAAAAAAAAAAAAAADnBpdG0AAAAAAAIAAAAYaWRhdAAAAAAAAQABAAAAAAABAAEAAABgaWxvYwEAAABEQAAEAAEAAAAAAAADHAABAAAAAAAAACIAAgABAAAAAAAAAAEAAAAAAAAACAADAAAAAAAAAz4AAQAAAAAAAAAgAAQAAQAAAAAAAAABAAAACAAAAAgAAABiaWluZgAAAAAABAAAABVpbmZlAgAAAQABAABodmMxAAAAABVpbmZlAgAAAAACAABncmlkAAAAABVpbmZlAgAAAQADAABodmMxAAAAABVpbmZlAgAAAAAEAABncmlkAAAAAbFpcHJwAAABhGlwY28AAAB2aHZjQwEDcAAAAAAAAAAAAB7wAPz9+PgAAA8DIAABABhAAQwB//8DcAAAAwCQAAADAAADAB66AkAhAAEAKkIBAQNwAAADAJAAAAMAAAMAHqAggQWW6q6a5uBAQMCAAAADAIAAAAMAhCIAAQAGRAHBc8GJAAAAFGlzcGUAAAAAAAAAQAAAAEAAAAAUaXNwZQAAAAAAAAABAAAAAQAAABBwaXhpAAAAAAMICAgAAABxaHZjQwEECAAAAAAAAAAAAB7wAPz8+PgAAA8DIAABABdAAQwB//8ECAAAAwCf+AAAAwAAHroCQCEAAQAmQgEBBAgAAAMAn/gAAAMAAB7AggQWW6q6a5sCAAADAAIAAAMAAhAiAAEABkQBwXPBiQAAABRpc3BlAAAAAAAAAEAAAABAAAAAFGlzcGUAAAAAAAAAAQAAAAEAAAAOcGl4aQAAAAABCAAAACdhdXhDAAAAAHVybjptcGVnOmhldmM6MjAxNTphdXhpZDoxAAAAACVpcG1hAAAAAAAAAAQAAQKBAgACAgOEAAMChQYABAMHiIkAAAA2aXJlZgAAAAAAAAAOZGltZwACAAEAAQAAAA5kaW1nAAQAAQADAAAADmF1eGwABAABAAIAAABKbWRhdAAAAB4oAa8FEhJM4PoDvef+6D6IWYCvu2VYo5lcYJNTt4AAAAAcKAGuCiQkGcDhnoA/9bdzylCzhNKQp7VBQJa1Aw==","sha256":"f83e11a663c5b03e43a9e1de3e455f207f21e0438c10c57fadf17cb81f45499e","origin":"unchanged upstream engine fixture"},{"id":"invalid","url":"data:image/png;base64,bm90IGEgcG5n","sha256":"2aade9c49b9414c70f452b226271ef5066e2894cdd0557f54857819fb7bcc782","origin":"invalid decoder error control"}];
  function elapsed() { return Date.now() - began; }
  function errorName(error) { return error && typeof error.name === 'string' ? error.name.slice(0,60) : 'Error'; }
  function add(record) {
    record.elapsedMs = elapsed(); records.push(record);
    var row = document.createElement('tr');
    [record.family,record.id,record.terminal,record.upstreamWouldSettle === true ? 'yes' : 'no'].forEach(function (value) {
      var cell = document.createElement('td');cell.textContent=String(value);row.appendChild(cell);
    });
    document.getElementById('rows').appendChild(row);
    return record;
  }
  function imageCase(fixture, connected) {
    return new Promise(function (resolve) {
      var img = new Image(), done = false, timer, events = [], start = elapsed();
      retainedImages.push(img);
      if (connected) { img.width = 24; img.height = 24; document.getElementById('images').appendChild(img); }
      function finish(terminal) {
        if (done) return; done = true; clearTimeout(timer);
        var width = null, height = null, complete = null;
        try { width=img.naturalWidth;height=img.naturalHeight;complete=img.complete; } catch (_) {}
        img.onload = img.onerror = null;
        resolve(add({family:'image',id:fixture.id+(connected?'-attached':'-detached'),format:fixture.id,connected:connected,
          sourceSHA256:fixture.sha256,terminal:terminal,events:events,msUntilTerminal:elapsed()-start,
          upstreamWouldSettle:terminal==='load'||terminal==='error',naturalWidth:width,naturalHeight:height,complete:complete,
          decoderAccepted:terminal==='load' && width>0 && height>0,handlersInstalledAfterSource:true}));
      }
      timer=setTimeout(function(){finish('observer-deadline');},6000);
      try {
        // The upstream suite sets src first. Load/error must be asynchronous.
        img.src=fixture.url;
        img.onload=function(event){events.push({type:'load',trusted:event.isTrusted,elapsedMs:elapsed()});finish('load');};
        img.onerror=function(event){events.push({type:'error',trusted:event.isTrusted,elapsedMs:elapsed()});finish('error');};
      } catch(error) { events.push({type:'exception',name:errorName(error),elapsedMs:elapsed()});finish('exception'); }
    });
  }
  function xhrCase(kind) {
    return new Promise(function(resolve) {
      var xhr, events=[], sawDone=false, typed=null, exception=null, terminal=null, timer, finished=false;
      function emit(type) { if(events.length<16)events.push({type:type,readyState:xhr?xhr.readyState:null,elapsedMs:elapsed()}); }
      function snapshot() {
        if(!xhr)return null;
        var result={status:xhr.status,responseType:xhr.responseType};
        try {
          if(kind==='text'){result.textLength=xhr.responseText.length;result.bodyMatched=xhr.responseText==="<!doctype html><html><head><title>&amp;&amp;&lt;</title></head><body>typed diagnostic</body></html>";}
          if(kind==='document'){result.title=xhr.responseXML?xhr.responseXML.title:null;result.documentPresent=!!xhr.responseXML;}
          if(kind==='arraybuffer'){result.arrayBuffer=xhr.response instanceof ArrayBuffer;result.byteLength=xhr.response?xhr.response.byteLength:null;}
          if(kind==='blob'){result.blob=xhr.response instanceof Blob;result.byteLength=xhr.response?xhr.response.size:null;result.mimeType=xhr.response?xhr.response.type:null;}
        }catch(error){result.responseException=errorName(error);}
        return result;
      }
      function finish() {
        if(finished)return;finished=true;clearTimeout(timer);
        if(xhr){xhr.onreadystatechange=xhr.onload=xhr.onerror=xhr.ontimeout=xhr.onabort=xhr.onloadend=null;if(!sawDone)try{xhr.abort();}catch(_){} }
        resolve(add({family:'xhr',id:kind,terminal:terminal||(sawDone?'readystatechange-done':'observer-deadline'),events:events,
          upstreamWouldSettle:sawDone||!!exception,readyState4Observed:sawDone,response:typed,exception:exception,requestCleanupRequested:true}));
      }
      timer=setTimeout(finish,6500);
      try {
        xhr=new XMLHttpRequest();requests.push(xhr);
        xhr.onreadystatechange=function(){emit('readystatechange');if(xhr.readyState===4){sawDone=true;typed=snapshot();}};
        ['load','error','timeout','abort','loadend'].forEach(function(type){xhr['on'+type]=function(){emit(type);if(type==='loadend'){terminal=sawDone?'readystatechange-done':'loadend-without-done';finish();}};});
        xhr.open('GET','/detect.html?nonce='+nonce+'&case='+kind,true);xhr.responseType=kind;xhr.timeout=5000;xhr.send();
      }catch(error){exception=errorName(error);terminal='exception';finish();}
    });
  }
  async function idbCase() {
    var name='lume-event-diagnostic-'+nonce, db=null, events=[], completed=false, opened=false, deleted=false, failure=null, active=true;
    function event(type,extra){if(events.length<24)events.push(Object.assign({type:type,elapsedMs:elapsed()},extra||{}));}
    function request(factory,label,upgrade) {
      return new Promise(function(resolve,reject){
        var request, done=false;
        var timer=setTimeout(function(){if(done)return;done=true;event(label+'-observer-deadline');reject({name:'ObserverDeadline'});},4000);
        function fail(error){if(done)return;done=true;clearTimeout(timer);reject(error||{name:'UnknownError'});}
        try {
          request=factory();
          request.onblocked=function(){if(done||!active)return;event(label+'-blocked');};
          request.onerror=function(){if(done||!active)return;event(label+'-error',{name:errorName(request.error)});fail(request.error);};
          request.onupgradeneeded=function(){if(done||!active)return;event(label+'-upgrade');if(upgrade)try{upgrade(request.result);}catch(error){fail(error);}};
          request.onsuccess=function(){
            if(done||!active){if(label==='open' && request.result)try{request.result.close();}catch(_){}return;}
            done=true;clearTimeout(timer);event(label+'-success');resolve(request.result);
          };
        }catch(error){event(label+'-exception',{name:errorName(error)});fail(error);}
      });
    }
    try {
      if(!window.indexedDB || typeof indexedDB.deleteDatabase!=='function')throw {name:'APIUnavailable'};
      await request(function(){return indexedDB.deleteDatabase(name);},'initial-delete');
      db=await request(function(){return indexedDB.open(name,1);},'open',function(value){value.createObjectStore('store');});opened=true;
      var transaction=db.transaction('store','readwrite'),store=transaction.objectStore('store');
      // Same two genuine object types used by the upstream background test.
      var blob=store.put(new Blob(['fixture'],{type:'text/plain'}),'blob');
      var buffer=store.put(new Uint8Array([2,4,6]).buffer,'arraybuffer');
      event('put-requests-created',{blobRequestPresent:!!blob,arrayBufferRequestPresent:!!buffer});
      await new Promise(function(resolve,reject){
        var done=false,timer=setTimeout(function(){if(done)return;done=true;event('transaction-observer-deadline');try{transaction.abort();}catch(_){}reject({name:'ObserverDeadline'});},4000);
        function finish(error){if(done)return;done=true;clearTimeout(timer);error?reject(error):resolve();}
        transaction.oncomplete=function(){if(done||!active)return;event('transaction-complete');finish();};
        transaction.onerror=function(){if(done||!active)return;event('transaction-error',{name:errorName(transaction.error)});finish(transaction.error||{name:'TransactionError'});};
        transaction.onabort=function(){if(done||!active)return;event('transaction-abort');finish({name:'AbortError'});};
      });
      completed=true;
    }catch(error){failure=errorName(error);}
    finally{
      if(db)try{db.close();event('connection-closed');}catch(error){event('close-exception',{name:errorName(error)});}
      try{if(window.indexedDB){await request(function(){return indexedDB.deleteDatabase(name);},'cleanup-delete');deleted=true;}}
      catch(error){event('cleanup-delete-failed',{name:errorName(error)});}
      active=false;
    }
    return add({family:'idb',id:'delete-open-store-cleanup',terminal:completed?'transaction-complete':failure||'not-completed',events:events,
      upstreamWouldSettle:opened||failure!=='ObserverDeadline',openSucceeded:opened,transactionCompleted:completed,
      freshDatabaseName:true,connectionClosed:!db||events.some(function(e){return e.type==='connection-closed';}),cleanupDeleteSucceeded:deleted,failure:failure});
  }
  async function run() {
    var work=[];
    FIXTURES.forEach(function(fixture){work.push(imageCase(fixture,false));work.push(imageCase(fixture,true));});
    ['text','document','arraybuffer','blob'].forEach(function(kind){work.push(xhrCase(kind));});
    work.push(idbCase());
    try{await Promise.all(work);}catch(error){add({family:'observer',id:'run',terminal:errorName(error),upstreamWouldSettle:false});}
    requests.forEach(function(xhr){try{if(xhr.readyState!==4)xhr.abort();}catch(_){}});
    retainedImages.forEach(function(img){img.onload=img.onerror=null;if(img.parentNode)img.remove();});
    var result={schemaVersion:1,stage:'completed',nonce:nonce,scope:'Independent async-event diagnostic; not an HTML5 score',elapsedMs:elapsed(),
      records:records,requestCleanupRequested:true,imageListenersRemoved:true,originalProfilesAccessed:false,
      benchmarkExecuted:false,benchmarkScore:null,detectorModified:false,featureResultsStubbed:false};
    document.getElementById('status').textContent='Diagnostic complete — observed events below';
    // The native console has a bounded line length. Keep each genuine case
    // separate, then reconstruct all17 in order before correlating the POST.
    records.forEach(function(record,index){
      console.log('LUME_HTML5_EVENT_ROW '+JSON.stringify({schemaVersion:1,nonce:nonce,index:index,record:record}));
    });
    var header=Object.assign({},result,{recordCount:records.length});delete header.records;
    console.log('LUME_HTML5_EVENTS '+JSON.stringify(header));
    console.log('LUME_CAPTURE_REQUEST overview');
    try{await fetch('/result?nonce='+nonce,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(result)});}catch(_){ }
  }
  run();
})();
