// Deterministic synthetic-device regression test. Never evidence of physical hardware.
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
  const {createLabServer}=await import('../server.mjs');const server=createLabServer();await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));let browser;
  try{
    browser=await chromium.launch({headless:true,executablePath:process.env.LUME_CHROME_EXECUTABLE||undefined,args:['--use-fake-device-for-media-stream','--use-fake-ui-for-media-stream']});
    const context=await browser.newContext({permissions:['camera','microphone','geolocation'],geolocation:{latitude:1,longitude:1,accuracy:10}});
    const page=await context.newPage();await page.goto(`http://127.0.0.1:${server.address().port}`);await page.waitForFunction(()=>window.LumeLab);
    assert.equal((await page.evaluate(()=>LumeLab.run('camera'))).state,'pass');
    assert.equal((await page.evaluate(()=>LumeLab.run('microphone'))).state,'pass');
    assert.equal((await page.evaluate(()=>LumeLab.snapshot())).resources.activeMediaTracks,2);
    await page.evaluate(()=>LumeLab.stopAll());assert.equal((await page.evaluate(()=>LumeLab.snapshot())).resources.activeMediaTracks,0);
    assert.equal((await page.evaluate(()=>LumeLab.run('iframe'))).state,'pass');
    const child=page.frames().find(frame=>frame.url().endsWith('/iframe-probe.html'));
    // Retain synthetic track references so removal cannot hide a leaked live track.
    await child.evaluate(()=>{const acquire=navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);parent.fixtureChildTracks=[];navigator.mediaDevices.getUserMedia=async constraints=>{const stream=await acquire(constraints);parent.fixtureChildTracks.push(...stream.getTracks());return stream;};});
    for(const kind of ['camera','microphone']){
      await child.locator(`[data-capability="${kind}"]`).click();
      await page.waitForFunction(kind=>LumeLab.snapshot().results.iframe.evidence.childPermissionTests?.[kind]?.state==='pass',kind);
      assert.equal((await page.evaluate(()=>LumeLab.snapshot())).resources.activeMediaTracks,1);
    }
    assert.equal(await page.evaluate(()=>fixtureChildTracks[0].readyState),'ended','Switching child capture must stop the previous camera track');
    await child.locator('[data-capability="location"]').click();
    await page.waitForFunction(()=>LumeLab.snapshot().results.iframe.evidence.childPermissionTests?.location?.state==='pass');
    const locationEvidence=(await page.evaluate(()=>LumeLab.snapshot())).results.iframe.evidence.childPermissionTests.location.evidence;
    assert.equal(locationEvidence.accuracyMeters,10);
    assert.equal('latitude' in locationEvidence,false);
    assert.equal('longitude' in locationEvidence,false);
    assert.equal(await page.evaluate(()=>fixtureChildTracks.every(track=>track.readyState==='ended')),true);
    await child.locator('[data-capability="camera"]').click();
    await page.waitForFunction(()=>fixtureChildTracks.length===3&&fixtureChildTracks[2].readyState==='live');
    await page.evaluate(()=>LumeLab.stopAll('fixture child teardown'));
    assert.equal(await page.evaluate(()=>fixtureChildTracks.every(track=>track.readyState==='ended')),true,'Parent Stop must end child capture, even after the iframe is removed');
    assert.equal((await page.evaluate(()=>LumeLab.snapshot())).resources.activeMediaTracks,0);
    assert.equal((await page.evaluate(()=>LumeLab.run('camera'))).state,'pass');
    await page.evaluate(()=>Object.defineProperty(document,'hidden',{configurable:true,get:()=>true}));
    await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
    const hidden=await page.evaluate(()=>LumeLab.snapshot());assert.equal(hidden.resources.activeMediaTracks,0);assert.equal(hidden.resources.activeScopes,0);
    console.log(JSON.stringify({passed:true,fixture:'synthetic Chrome camera/microphone and injected geolocation; physical devices/GPS NOT tested',checks:['two live parent tracks acquired','stop closes both','child camera and microphone acquisition','switching child capture stops previous track','child location returns without exported coordinates','parent Stop ends retained child tracks','capture restarts','hidden page closes all capture']},null,2));
  }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});
