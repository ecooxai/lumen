// Reference test: two actual Chrome OS processes; this is not Lumen evidence.
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
(async()=>{
  const {createLabServer}=await import('../server.mjs');
  const server=createLabServer();await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
  const port=server.address().port,browsers=[],processes=[];
  try{
    const executablePath=process.env.LUME_CHROME_EXECUTABLE||chromium.executablePath();
    const options={headless:true,executablePath};
    for(let i=0;i<2;i++){
      const process=await chromium.launchServer(options);processes.push(process);
      browsers.push(await chromium.connect(process.wsEndpoint()));
    }
    const processIds=processes.map(server=>server.process().pid);
    assert.ok(processIds.every(Number.isInteger));assert.notEqual(processIds[0],processIds[1]);
    const offer=await browsers[0].newPage(),answer=await browsers[1].newPage(),room='chrome-independent-'+Date.now();
    await Promise.all([offer.goto(`http://127.0.0.1:${port}/rtc-peer.html?room=${room}&role=offer&autostart=1`),answer.goto(`http://127.0.0.1:${port}/rtc-peer.html?room=${room}&role=answer&autostart=1`)]);
    await Promise.all([offer.waitForFunction(()=>['pass','fail','unsupported','cancelled'].includes(window.LumeRTCPeer?.snapshot().state)&&window.LumeRTCPeer.snapshot().resourcesReleased,{timeout:25000}),answer.waitForFunction(()=>['pass','fail','unsupported','cancelled'].includes(window.LumeRTCPeer?.snapshot().state)&&window.LumeRTCPeer.snapshot().resourcesReleased,{timeout:25000})]);
    const result={testEnvironment:'Two independent headless Chromium processes; not Lumen',processIds,binarySHA256:crypto.createHash('sha256').update(fs.readFileSync(executablePath)).digest('hex'),processSeparationVerified:true,independentProcessInteroperability:false,crossBrowserInteroperability:false,offer:await offer.evaluate(()=>LumeRTCPeer.snapshot()),answer:await answer.evaluate(()=>LumeRTCPeer.snapshot())};
    for(const page of [result.offer,result.answer]){assert.equal(page.state,'pass',JSON.stringify(page));assert.equal(page.payloadMatched,true);assert.equal(page.independentPeerEcho,true);assert.equal(page.resourcesReleased,true);assert.equal(page.processSeparationVerified,false);assert.equal(page.crossBrowserInteroperability,false);}
    result.independentProcessInteroperability=true;
    fs.mkdirSync(path.join(__dirname,'../output'),{recursive:true});fs.writeFileSync(path.join(__dirname,'../output/chrome-independent-rtc.json'),JSON.stringify(result,null,2));
    console.log(JSON.stringify(result,null,2));
  }finally{
    await Promise.all(browsers.map(browser=>browser.close()));
    await Promise.all(processes.map(server=>server.close()));
    await new Promise(resolve=>server.close(resolve));
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
