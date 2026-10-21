const { chromium }=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');

(async()=>{
  const {createLabServer}=await import('../server.mjs');
  const server=createLabServer();await new Promise(resolve=>server.listen(8765,'127.0.0.1',resolve));
  let browser;
  try{
    browser=await chromium.launch({headless:true,executablePath:process.env.LUME_CHROME_EXECUTABLE||undefined,args:['--enable-unsafe-swiftshader',...(process.env.LUME_ENABLE_SOFTWARE_WEBGPU==='1'?['--enable-unsafe-webgpu','--enable-features=Vulkan','--use-angle=vulkan','--use-vulkan=swiftshader','--use-webgpu-adapter=swiftshader','--disable-vulkan-surface']:[])]});
    const page=await browser.newPage({viewport:{width:1440,height:1080},deviceScaleFactor:1});
    const errors=[];page.on('pageerror',error=>errors.push(error.message));
    await page.goto('http://127.0.0.1:8765');await page.waitForFunction(()=>!!window.LumeLab);
    if(process.env.LUME_ENABLE_SOFTWARE_WEBGPU==='1')await page.getByLabel('WebGPU presentation mode').selectOption('readback');
    const result=await page.evaluate(()=>LumeLab.runAutomatic());
    for(const id of ['wasm','webgl','iframe','webrtc','websocket'])assert.equal(result.results[id].state,'pass',`${id}: ${result.results[id].detail}`);
    assert.ok(['pass','unsupported'].includes(result.results.webgpu.state),result.results.webgpu.detail);
    assert.ok(['pass','unsupported','configuration'].includes(result.results.webtransport.state),result.results.webtransport.detail);
    assert.equal(errors.length,0,JSON.stringify(errors));
    fs.mkdirSync(path.join(__dirname,'../output'),{recursive:true});
    fs.writeFileSync(path.join(__dirname,'../output/local-chromium-smoke.json'),JSON.stringify({...result,testEnvironment:`headless Chromium with SwiftShader; software WebGPU flag ${process.env.LUME_ENABLE_SOFTWARE_WEBGPU==='1'}; not a native Apple performance measurement`},null,2));
    await page.screenshot({path:path.join(__dirname,'../output/GPT6-AstraPro-subagents-Web-Lume-lab-desktop.png'),fullPage:true});
    await page.evaluate(()=>LumeLab.stopAll('smoke-test teardown'));const stopped=await page.evaluate(()=>LumeLab.snapshot());assert.equal(stopped.resources.activeScopes,0);assert.equal(stopped.resources.registeredCleanups,0);
    fs.writeFileSync(path.join(__dirname,'../output/local-chromium-post-stop.json'),JSON.stringify({...stopped,testEnvironment:'headless Chromium regression; explicit post-stop lifecycle evidence'},null,2));
    assert.equal((await page.evaluate(()=>LumeLab.run('webgl'))).state,'pass','WebGL must restart cleanly after context teardown');
    await page.setViewportSize({width:390,height:844});await page.screenshot({path:path.join(__dirname,'../output/GPT6-AstraPro-subagents-Web-Lume-lab-mobile.png'),fullPage:true});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,'Mobile page must not overflow horizontally');
    const dialog=page.locator('#oauth-dialog');await page.locator('[data-test="run-google"]').click();await dialog.waitFor({state:'visible'});await dialog.getByRole('button',{name:'Continue demo',exact:true}).click();await page.waitForFunction(()=>LumeLab.snapshot().results.google.state==='configuration');assert.equal((await page.evaluate(()=>LumeLab.snapshot())).results.google.evidence.authenticated,false);
    await page.evaluate(()=>LumeLab.stopAll());console.log(JSON.stringify({passed:true,summary:result.summary,webgpu:result.results.webgpu,webtransport:result.results.webtransport,teardown:stopped.resources},null,2));
  }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});
