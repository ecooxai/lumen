import test from 'node:test';
import assert from 'node:assert/strict';
import {diagnosticConsoleLines} from '../public/model.js';
import {runtimeEngine} from '../public/view.js';

test('native engine descriptor takes precedence over a compatibility user agent',()=>{
  assert.equal(runtimeEngine({userAgent:'Mozilla/5.0 Chrome/130.0 Safari/537.36',lumenCapabilities:{engine:'Lumen'}}),'Lumen');
  assert.equal(runtimeEngine({userAgent:'Mozilla/5.0 Chrome/130.0 Safari/537.36'}),'Chrome');
  assert.equal(runtimeEngine({userAgent:'Version/26 Safari/605.1.15'}),'Safari / WebKit');
});

test('full diagnostic report survives a 2048-byte native console line cap',()=>{
  const report={exportedAt:'2026-10-28T00:00:00.000Z',context:{iteration:'x'.repeat(9000)},results:[{id:'wasm',state:'pass',evidence:'42 '+('\u0000→😀\\"'.repeat(100)),durationMs:1}],summary:{pass:1,total:1}};
  assert.ok(Buffer.byteLength('LUME_LAB_REPORT '+JSON.stringify(report))>2048);
  const lines=diagnosticConsoleLines(report);
  assert.ok(lines.every(line=>Buffer.byteLength(line)<2048));
  const chunks=lines.filter(line=>line.startsWith('LUME_LAB_REPORT_CHUNK ')).map(line=>JSON.parse(line.slice('LUME_LAB_REPORT_CHUNK '.length)));
  assert.ok(chunks.length>1);assert.equal(chunks.length,chunks[0].total);
  assert.deepEqual(JSON.parse(chunks.map(chunk=>chunk.data).join('')),report);
  const result=JSON.parse(lines[0].slice('LUME_LAB_RESULT '.length));
  assert.equal(result.id,'wasm');assert.equal(result.state,'pass');
});

test('oversized diagnostic output is explicitly rejected instead of emitting an incomplete pass report',()=>{
  const lines=diagnosticConsoleLines({context:'x'.repeat(128*1024),results:[]});
  assert.equal(lines.length,1);assert.match(lines[0],/^LUME_LAB_REPORT_ERROR /);
});
