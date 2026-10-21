// Deterministic synthetic-device regression test. Never evidence of physical hardware.
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
  const {createLabServer}=await import('../server.mjs');const server=createLabServer();await new Promise(resolve=>server.listen(8765,'127.0.0.1',resolve));let browser;
  try{
    browser=await chromium.launch({headless:true,executablePath:process.env.LUME_CHROME_EXECUTABLE||undefined,args:['--use-fake-device-for-media-stream','--use-fake-ui-for-media-stream']});
    const page=await browser.newPage();await page.goto('http://127.0.0.1:8765');await page.waitForFunction(()=>window.LumeLab);
    assert.equal((await page.evaluate(()=>LumeLab.run('camera'))).state,'pass');
    assert.equal((await page.evaluate(()=>LumeLab.run('microphone'))).state,'pass');
    assert.equal((await page.evaluate(()=>LumeLab.snapshot())).resources.activeMediaTracks,2);
    await page.evaluate(()=>LumeLab.stopAll());assert.equal((await page.evaluate(()=>LumeLab.snapshot())).resources.activeMediaTracks,0);
    assert.equal((await page.evaluate(()=>LumeLab.run('camera'))).state,'pass');
    await page.evaluate(()=>Object.defineProperty(document,'hidden',{configurable:true,get:()=>true}));
    await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
    const hidden=await page.evaluate(()=>LumeLab.snapshot());assert.equal(hidden.resources.activeMediaTracks,0);assert.equal(hidden.resources.activeScopes,0);
    console.log(JSON.stringify({passed:true,fixture:'synthetic Chrome camera/microphone; physical devices NOT tested',checks:['two live tracks acquired','stop closes both','capture restarts','hidden page closes all capture']},null,2));
  }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});
