(function () {
  var started=false,tries=0,poll;
  function emit(kind,payload){console.log('LUME_LAB_EVIDENCE '+JSON.stringify({kind:kind,payload:payload}));}
  function numeric(value){return typeof value==='number'&&isFinite(value)?value:null;}
  function rect(node){var r=node.getBoundingClientRect();return {x:numeric(r.x),y:numeric(r.y),width:numeric(r.width),height:numeric(r.height)};}
  function inside(r,width,height){return r.x!==null&&r.y!==null&&r.width>0&&r.height>0&&r.x>=-1&&r.y>=-1&&r.x+r.width<=width+1&&r.y+r.height<=height+1;}
  function geometry(id,stage){
    var card=document.querySelector('[data-capability="'+id+'"]');
    var result={id:id,captureStage:stage,viewport:{width:numeric(innerWidth),height:numeric(innerHeight),documentClientWidth:numeric(document.documentElement.clientWidth),documentScrollWidth:numeric(document.documentElement.scrollWidth),phoneBreakpoint:matchMedia('(max-width:670px)').matches},card:rect(card)};
    var cards=document.querySelectorAll('#device-grid > .card');
    if(cards.length>=2){var first=rect(cards[0]),second=rect(cards[1]);result.singlePhoneColumnObserved=second.y>=first.y+first.height-1;}
    if(id==='webgl'||id==='webgpu'){
      var canvas=card.querySelector('canvas'),r=rect(canvas),style=getComputedStyle(canvas);
      result.canvas={rect:r,intrinsicWidth:canvas.width,intrinsicHeight:canvas.height,objectFit:style.objectFit,display:style.display,
        fullCanvasInsideViewport:inside(r,innerWidth,innerHeight),intrinsicAspectPreserved:r.width>0&&r.height>0&&Math.abs(r.width/r.height-2)<.025};
    } else {
      var frame=card.querySelector('iframe'),doc=frame.contentDocument,win=frame.contentWindow;
      if(!doc||!win)throw new Error('Same-origin child geometry is unavailable');
      var buttons=[],nodes=doc.querySelectorAll('button');
      for(var i=0;i<nodes.length;i++)buttons.push({label:nodes[i].textContent.trim(),rect:rect(nodes[i])});
      var frameRect=rect(frame),fw=numeric(win.innerWidth),fh=numeric(win.innerHeight);
      result.iframe={rect:frameRect,viewportWidth:fw,viewportHeight:fh,scrollWidth:numeric(doc.documentElement.scrollWidth),
        flexWrap:win.getComputedStyle(doc.querySelector('.actions')).flexWrap,buttons:buttons,
        fullFrameInsideViewport:inside(frameRect,innerWidth,innerHeight),allButtonsInsideChildViewport:fw>0&&fh>0&&buttons.length===4&&buttons.every(function(b){return inside(b.rect,fw,fh);})};
    }
    emit('responsive-geometry',result);
  }
  function stage(id,name){
    var node=document.querySelector('[data-capability="'+id+'"]');
    node.scrollIntoView({block:'center',behavior:'instant'});
    setTimeout(function(){try{geometry(id,name);console.log('LUME_CAPTURE_REQUEST '+name);}catch(error){emit('responsive-error',{name:error.name,message:error.message});}},700);
  }
  function start(){
    if(started||!window.LumeLab)return;started=true;clearInterval(poll);
    emit('responsive-start',{scope:'CSS layout and real GPU/iframe probes only',stageMapping:{overview:'webgl',gpu:'webgpu',network:'iframe'}});
    Promise.resolve().then(function(){return LumeLab.run('webgl');}).then(function(result){emit('result',result);return LumeLab.run('webgpu');}).then(function(result){emit('result',result);return LumeLab.run('iframe');}).then(function(result){
      emit('result',result);stage('webgl','overview');
      setTimeout(function(){stage('webgpu','gpu');},4500);
      setTimeout(function(){stage('iframe','network');},9000);
      setTimeout(function(){LumeLab.stopAll('focused responsive capture completed');emit('post-stop',{resources:LumeLab.snapshot().resources});emit('responsive-completed',{scope:'geometry and actual owned captures; human pixel review remains required'});},13500);
    },function(error){emit('responsive-error',{name:error.name,message:error.message});LumeLab.stopAll('focused responsive failure');emit('post-stop',{resources:LumeLab.snapshot().resources});});
  }
  window.addEventListener('lume-lab-ready',start,{once:true});
  poll=setInterval(function(){start();if(++tries>75){clearInterval(poll);if(!started)emit('responsive-error',{name:'TimeoutError',message:'Lab did not become ready'});}},200);
  start();
}());
