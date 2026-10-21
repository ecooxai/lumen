const { chromium } = require('playwright');
const assert = require('node:assert/strict');

(async () => {
  const { createLabServer } = await import('../server.mjs');
  const server = createLabServer();
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  let browser;
  try {
    browser = await chromium.launch({ headless: true, executablePath: process.env.LUME_CHROME_EXECUTABLE || undefined });
    const page = await browser.newPage();
    await page.addInitScript(() => {
      Object.defineProperty(navigator, 'lumenCapabilities', { value: Object.freeze({ engine: 'Lumen fixture', webrtcNetworkTransport: false, experimentalRTC: 'in-process-loopback', webTransport: false, geolocationProvider: 'ip-approximation' }) });
      window.RTCPeerConnection = function () { throw new Error('Experimental RTC constructor must not be used'); };
      window.WebTransport = function () { throw new Error('Unsupported transport constructor must not be used'); };
    });
    const origin = `http://127.0.0.1:${server.address().port}`;
    await page.goto(origin);
    await page.waitForFunction(() => !!window.LumeLab);
    for (const id of ['webrtc', 'webtransport']) {
      const result = await page.evaluate(id => LumeLab.run(id), id);
      assert.equal(result.state, 'unsupported', result.detail);
    }
    assert.equal((await page.evaluate(() => LumeLab.snapshot())).resources.activeScopes, 0);
    await page.goto(`${origin}/rtc-peer.html?room=declaration-guard-fixture&role=offer&autostart=1`);
    await page.waitForFunction(() => window.LumeRTCPeer?.snapshot().state === 'unsupported');
    const peer = await page.evaluate(() => LumeRTCPeer.snapshot());
    assert.equal(peer.crossBrowserInteroperability, false);
    assert.equal(peer.resourcesReleased, true);
    console.log(JSON.stringify({ passed: true, scope: 'Synthetic engine declarations in Chromium; not a native Lumen measurement', rtc: 'unsupported', webtransport: 'unsupported', peerResourcesReleased: true }));
  } finally {
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
