#!/usr/bin/env node
'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const os = require('node:os');
const {spawn} = require('node:child_process');

const args = process.argv.slice(2);
const arg = (key, fallback) => { const i = args.indexOf('--' + key); return i < 0 ? fallback : args[i + 1]; };
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const harnessNames = ['OK', 'ERROR', 'TIMEOUT', 'PRECONDITION_FAILED'];
const subtestNames = ['PASS', 'FAIL', 'TIMEOUT', 'NOTRUN', 'PRECONDITION_FAILED'];
const root = __dirname;
const integration = `\nadd_completion_callback(function(tests, status) {
  var result = {wpt_test_id:location.pathname,status:status.status,message:status.message || null,
    subtests:tests.map(function(t) {return {name:t.name,status:t.status,message:t.message || null,stack:t.stack || null};})};
  window.__wptSubsetResult = result;
  var compact = {wpt_test_id:result.wpt_test_id,status:result.status,message:result.message,
    console_detail_policy:'Exact names and statuses; detailed messages/stacks remain in browser result',
    subtests:tests.map(function(t) {return {name:t.name,status:t.status};})};
  var json = JSON.stringify(compact), size = 256, total = Math.ceil(json.length/size), id = 'wpt-' + Date.now();
  if (total > 512) { console.log('LUME_LAB_REPORT_ERROR ' + JSON.stringify({error:'WPT result exceeds 128 KiB diagnostic bound'})); }
  else { for(var index=0;index<total;index++) console.log('LUME_LAB_REPORT_CHUNK ' + JSON.stringify({version:1,id:id,index:index,total:total,data:json.slice(index*size,(index+1)*size)})); }
  console.log('WPT-SUBSET-DONE ' + JSON.stringify({status:status.status,subtests:tests.length}));
});\n`;

async function createServer(selection, sourceLock) {
  const allowed = new Set(sourceLock.files.map(file => '/' + file.path));
  for (const file of sourceLock.files) {
    const bytes = await fs.readFile(path.join(root, 'upstream', file.path));
    if (hash(bytes) !== file.sha256) throw Error('Upstream source hash changed: ' + file.path);
  }
  return http.createServer(async (req, res) => {
    try {
      const pathname = new URL(req.url, 'http://127.0.0.1').pathname;
      if (Object.hasOwn(selection.generated_wrappers, pathname)) {
        const wrapper = selection.generated_wrappers[pathname];
        const scripts = ['/resources/testharness.js', '/resources/testharnessreport.js', ...wrapper.scripts, '/' + wrapper.source];
        res.writeHead(200, {'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store'});
        res.end('<!doctype html><meta charset="utf-8"><title>WPT window subset</title>' + scripts.map(src => '<script src="' + src + '"></script>').join(''));
        return;
      }
      if (!allowed.has(pathname)) { res.writeHead(404); res.end('Not in pinned WPT subset'); return; }
      let bytes = await fs.readFile(path.join(root, 'upstream', pathname.slice(1)));
      if (pathname === '/resources/testharnessreport.js') bytes = Buffer.concat([bytes, Buffer.from(integration)]);
      const mime = pathname.endsWith('.js') ? 'text/javascript' : pathname.endsWith('.xml') ? 'application/xml' : pathname.endsWith('.xhtml') ? 'application/xhtml+xml' : 'text/html';
      res.writeHead(200, {'Content-Type': mime + '; charset=utf-8', 'Cache-Control': 'no-store'});
      res.end(bytes);
    } catch (error) { res.writeHead(500); res.end('Pinned test server error'); }
  });
}

async function main() {
  const selection = JSON.parse(await fs.readFile(path.join(root, 'selection.json'), 'utf8'));
  const sourceLock = JSON.parse(await fs.readFile(path.join(root, 'source-lock.json'), 'utf8'));
  if (sourceLock.wpt_revision !== selection.wpt_revision) throw Error('Source revision does not match selection');
  const server = await createServer(selection, sourceLock);
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(Number(arg('port', '0')), '127.0.0.1', resolve); });
  const url = 'http://127.0.0.1:' + server.address().port;
  if (args.includes('--serve-only')) { console.log(JSON.stringify({url, selection:selection.test_ids})); return; }
  const out = path.resolve(arg('output', 'output/session-20261028/wpt-reference'));
  let chromeServer, browser, sampler;
  const started = Date.now();
  const report = {run_info: {product:'chromium', browser_version:null, os:os.platform(), os_version:os.release(), revision:selection.wpt_revision,
    selection_sha256:hash(JSON.stringify({test_ids:selection.test_ids, wpt_revision:selection.wpt_revision})),
    adapter_name:'lume-fixed-wpt-window-subset', adapter_version:'1', official_wpt_run:false}, results:[], time_start:started,
    scope:selection.scope, limitations:selection.limitations, source_lock_sha256:hash(await fs.readFile(path.join(root, 'source-lock.json'))),
    adapter_sha256:hash(await fs.readFile(__filename)), selection_file_sha256:hash(await fs.readFile(path.join(root, 'selection.json')))};
  try {
    await fs.mkdir(path.dirname(out), {recursive:true});
    await fs.mkdir(out, {recursive:false});
    const {chromium} = require('playwright');
    chromeServer = await chromium.launchServer({executablePath:arg('chrome'), headless:true, args:['--no-sandbox', '--disable-dev-shm-usage']});
    browser = await chromium.connect(chromeServer.wsEndpoint());
    report.run_info.browser_version = browser.version();
    report.chrome_pid = chromeServer.process().pid;
    const targets = path.join(out, 'targets.json');
    await fs.writeFile(targets, JSON.stringify({phase:'wpt-active', groups:[{name:'chromium', pids:[report.chrome_pid]}]}));
    sampler = spawn(process.env.CODEX_PRIMARY_RUNTIME_PYTHON || 'python3', [path.join(root, '..', '..', 'scripts', 'qa-apple-gpt6', 'telemetry.py'), 'collect', '--targets', targets, '--output', path.join(out, 'process-samples.jsonl'), '--seconds', '180'], {stdio:'ignore'});
    const context = await browser.newContext();
    for (const testId of selection.test_ids) {
      const page = await context.newPage();
      const begin = Date.now();
      const errors = [];
      page.on('pageerror', error => errors.push(String(error)));
      let row;
      try {
        await page.goto(url + testId, {waitUntil:'load', timeout:15000});
        await page.waitForFunction(() => window.__wptSubsetResult !== undefined, null, {timeout:20000});
        const result = await page.evaluate(() => window.__wptSubsetResult);
        row = {test:testId, status:harnessNames[result.status] || 'ERROR', message:result.message,
          subtests:result.subtests.map(item => ({...item, status:subtestNames[item.status] || 'FAIL'})), duration:Date.now()-begin};
      } catch (error) { row = {test:testId, status:error.name === 'TimeoutError' ? 'TIMEOUT' : 'ERROR', message:String(error), subtests:[], duration:Date.now()-begin}; }
      row.page_errors = errors;
      report.results.push(row);
      await page.close();
      console.log(JSON.stringify({test:testId,status:row.status,passed:row.subtests.filter(x=>x.status==='PASS').length,total:row.subtests.length}));
    }
    await context.close();
  } finally {
    if (browser) await browser.close().catch(()=>{});
    if (chromeServer) await chromeServer.close().catch(()=>{});
    if (sampler && sampler.exitCode === null) { sampler.kill('SIGTERM'); await new Promise(resolve => { sampler.once('exit', resolve); setTimeout(resolve, 1500); }); }
    await new Promise(resolve => server.close(resolve));
    report.time_end = Date.now();
    await fs.writeFile(path.join(out, 'wptreport.json'), JSON.stringify(report, null, 2)+'\n');
    console.log(JSON.stringify({output:out,files:report.results.length,browser:report.run_info.browser_version}));
  }
}
module.exports = {createServer, integration};
if (require.main === module) main().catch(error=>{console.error(error.stack);process.exitCode=1;});
