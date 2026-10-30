(function () {
  var started=false, tries=0, poll, device, uniform, readback, context;
  var report={schema:1,status:'waiting',coverage:'independent uniform-driven WebGPU native-canvas challenge',shaderParametersChanged:true,checks:[],resourcesReleased:false,compositorVerified:false};
  window.LumeGPUChallenge={snapshot:function(){return JSON.parse(JSON.stringify(report));}};
  function emit(stage,value){console.log('LUME_GPU_EVIDENCE '+JSON.stringify({stage:stage,value:value}));}
  function pause(ms){return new Promise(function(resolve){setTimeout(resolve,ms);});}
  function cleanup(){try{readback&&readback.destroy();}catch(_){}try{uniform&&uniform.destroy();}catch(_){}try{context&&context.unconfigure();}catch(_){}try{device&&device.destroy();}catch(_){}report.resourcesReleased=true;emit('cleanup',{resourcesReleased:true,status:report.status});}
  async function run(){
    if(started||!document.body)return;started=true;clearInterval(poll);report.status='running';
    if(!navigator.gpu){report.status='unsupported';emit('unsupported',{reason:'navigator.gpu is absent'});cleanup();return;}
    var box=document.createElement('section');box.style.cssText='padding:24px;background:#e8f1fb;color:#101d32;position:relative;z-index:1;';
    var title=document.createElement('h1');title.textContent='Independent WebGPU canvas challenge';title.style.cssText='font:600 28px sans-serif;margin:0 0 12px';box.appendChild(title);
    var text=document.createElement('p');text.style.cssText='font:18px sans-serif;margin:0 0 12px';box.appendChild(text);
    var canvas=document.createElement('canvas');canvas.width=256;canvas.height=144;canvas.style.cssText='display:block;width:768px;height:432px;max-width:100%;border:2px solid #193455';box.appendChild(canvas);
    document.body.insertBefore(box,document.body.firstChild);window.scrollTo(0,0);
    try{
      await pause(2500);
      var adapter=await navigator.gpu.requestAdapter();if(!adapter)throw new Error('No WebGPU adapter');device=await adapter.requestDevice();
      var format=navigator.gpu.getPreferredCanvasFormat();if(format!=='bgra8unorm'&&format!=='rgba8unorm')throw new Error('Unverified canvas format '+format);
      context=canvas.getContext('webgpu');if(!context)throw new Error('No native WebGPU canvas context');
      context.configure({device:device,format:format,alphaMode:'opaque',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});
      var shader=device.createShaderModule({code:'struct Uniforms { color: vec4<f32> }; @group(0) @binding(0) var<uniform> params: Uniforms; @vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4<f32> { var p=array<vec2<f32>,3>(vec2<f32>(-1.,-1.),vec2<f32>(3.,-1.),vec2<f32>(-1.,3.)); return vec4<f32>(p[i],0.,1.); } @fragment fn fs()->@location(0) vec4<f32> { return params.color; }'});
      var pipeline=device.createRenderPipeline({layout:'auto',vertex:{module:shader,entryPoint:'vs'},fragment:{module:shader,entryPoint:'fs',targets:[{format:format}]},primitive:{topology:'triangle-list'}});
      uniform=device.createBuffer({size:16,usage:GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST});
      var binding=device.createBindGroup({layout:pipeline.getBindGroupLayout(0),entries:[{binding:0,resource:{buffer:uniform}}]});
      readback=device.createBuffer({size:256*144*4,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
      var colors=[[0.15625,0.71875,0.28125,1],[0.8125,0.203125,0.65625,1]];
      for(var stage=0;stage<colors.length;stage++){
        var color=colors[stage],expected=color.map(function(v){return Math.round(v*255);});
        device.queue.writeBuffer(uniform,0,new Float32Array(color));
        var texture=context.getCurrentTexture(),encoder=device.createCommandEncoder();
        var pass=encoder.beginRenderPass({colorAttachments:[{view:texture.createView(),clearValue:{r:0,g:0,b:0,a:1},loadOp:'clear',storeOp:'store'}]});
        pass.setPipeline(pipeline);pass.setBindGroup(0,binding);pass.draw(3);pass.end();
        encoder.copyTextureToBuffer({texture:texture},{buffer:readback,bytesPerRow:1024,rowsPerImage:144},{width:256,height:144,depthOrArrayLayers:1});
        device.queue.submit([encoder.finish()]);await readback.mapAsync(GPUMapMode.READ);
        var pixels=new Uint8Array(readback.getMappedRange()),matching=0,maxError=0,observed=[];
        for(var i=0;i<pixels.length;i+=4){
          var actual=format==='bgra8unorm'?[pixels[i+2],pixels[i+1],pixels[i],pixels[i+3]]:[pixels[i],pixels[i+1],pixels[i+2],pixels[i+3]];
          var error=Math.max.apply(null,actual.map(function(v,index){return Math.abs(v-expected[index]);}));maxError=Math.max(maxError,error);if(error<=1)matching++;
          if(i===(72*256+128)*4)observed=actual;
        }
        readback.unmap();
        var check={phase:stage===0?'A':'B',expectedRGBA:expected,observedCenterRGBA:observed,matchingPixels:matching,totalPixels:256*144,maxChannelError:maxError,passed:matching===256*144,source:'native canvas texture COPY_SRC',format:format};
        report.checks.push(check);report.phase=check.phase;emit('readback',check);
        text.textContent='Phase '+check.phase+' · expected RGBA '+expected.join(', ')+' · '+matching+'/'+(256*144)+' pixels matched';
        if(!check.passed)throw new Error('Canvas texture did not reflect the changed shader uniform');
        window.scrollTo(0,0);await pause(700);console.log('LUME_CAPTURE_REQUEST '+(stage===0?'overview':'gpu'));await pause(9000);
      }
      report.status='pass';emit('completed',{status:report.status,checks:report.checks,compositorVerified:false});
    }catch(error){report.status='fail';report.error={name:error.name,message:String(error.message).slice(0,500)};emit('failed',report.error);}
    finally{cleanup();}
  }
  poll=setInterval(function(){run();if(++tries>100){clearInterval(poll);if(!started){report.status='startup-timeout';emit('startup-timeout',{});}}},200);
  window.addEventListener('DOMContentLoaded',run,{once:true});run();
}());
