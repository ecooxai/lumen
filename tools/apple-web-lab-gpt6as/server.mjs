import http from 'node:http';
import https from 'node:https';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), 'public');
const MIME = {'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.json':'application/json; charset=utf-8','.wasm':'application/wasm','.svg':'image/svg+xml'};
const MAX_FRAME = 65536;
const WT_MANIFESTS=[path.join(path.dirname(fileURLToPath(import.meta.url)),'output','wt-certificate.json'),path.join(path.dirname(fileURLToPath(import.meta.url)),'..','web-capability-lab','output','wt-certificate.json')];

export function encodeFrame(opcode, payload = Buffer.alloc(0)) {
  let header;
  if(payload.length<126)header=Buffer.from([0x80|opcode,payload.length]);
  else if(payload.length<=65535)header=Buffer.from([0x80|opcode,126,payload.length>>8,payload.length&255]);
  else {header=Buffer.alloc(10);header[0]=0x80|opcode;header[1]=127;header.writeBigUInt64BE(BigInt(payload.length),2);}
  return Buffer.concat([header, payload]);
}

export function decodeFrames(buffer, {requireMask = true} = {}) {
  let offset = 0; const frames = [];
  while (buffer.length - offset >= 2) {
    const start = offset; const first = buffer[offset++]; const second = buffer[offset++];
    if (first & 0x70) throw Error('Unexpected extension bits');
    const masked = Boolean(second & 0x80);
    if (requireMask && !masked) throw Error('Client frames must be masked');
    let length = second & 127;
    if (length === 126) {
      if (buffer.length - offset < 2) {offset = start; break;}
      length = buffer.readUInt16BE(offset); offset += 2;
    } else if (length === 127) {
      if (buffer.length - offset < 8) {offset = start; break;}
      const n = buffer.readBigUInt64BE(offset); offset += 8;
      if (n > BigInt(MAX_FRAME)) throw Error('Frame too large');
      length = Number(n);
    }
    if (length > MAX_FRAME) throw Error('Frame too large');
    const opcode = first & 15;
    if (opcode >= 8 && (!(first & 0x80) || length > 125)) throw Error('Invalid control frame');
    if (buffer.length - offset < length + (masked ? 4 : 0)) {offset = start; break;}
    const mask = masked ? buffer.subarray(offset, offset + 4) : null;
    if (masked) offset += 4;
    const payload = Buffer.from(buffer.subarray(offset, offset + length)); offset += length;
    if (mask) for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i % 4];
    frames.push({opcode, final: Boolean(first & 0x80), payload});
  }
  return {frames, remaining: buffer.subarray(offset)};
}

function handler(req, res) {
  const scheme=req.socket.encrypted?'https':'http',port=req.socket.localPort;
  const delegated=`self "${scheme}://localhost:${port}" "${scheme}://127.0.0.1:${port}"`;
  const headers = {
    'Cache-Control':'no-store',
    'X-Content-Type-Options':'nosniff',
    'Referrer-Policy':'no-referrer',
    'Permissions-Policy':`camera=(${delegated}), microphone=(${delegated}), geolocation=(${delegated})`
  };
  let decoded;
  try { decoded = decodeURIComponent(new URL(req.url, 'http://localhost').pathname); }
  catch {res.writeHead(400, headers); return res.end('Bad URL');}
  if (decoded.includes('..') || decoded.includes('\0') || decoded.includes('\\')) {res.writeHead(403, headers); return res.end('Forbidden');}
  if (req.method !== 'GET' && req.method !== 'HEAD') {res.writeHead(405, {...headers, Allow:'GET, HEAD'}); return res.end();}
  if (decoded === '/api/health') {
    res.writeHead(200, {...headers, 'Content-Type':'application/json'});
    return res.end(JSON.stringify({ok:true,websocket:'/echo',webtransport:'Requires a separate HTTP/3 endpoint; HTTP/1 echo is not QUIC.'}));
  }
  if(decoded==='/api/webtransport-config'){
    let config={configured:false,note:'Start the existing web-capability-lab HTTP/3-only server.'};
    for(const file of WT_MANIFESTS){
      try{
        const manifest=JSON.parse(fs.readFileSync(file,'utf8'));
        if(manifest.transport==='HTTP/3 only'&&manifest.expiresAt>Date.now()&&Array.isArray(manifest.certificateHash)&&manifest.certificateHash.length===32&&manifest.certificateHash.every(x=>Number.isInteger(x)&&x>=0&&x<=255)){
          config={configured:true,url:'https://127.0.0.1:4433/echo',certificateHash:Buffer.from(manifest.certificateHash).toString('hex'),expiresAt:manifest.expiresAt,expectedProtocol:'HTTP/3 only',note:'Configuration only. Server reachability, fresh echoes, and QUIC audit evidence remain unverified.'};break;
        }
      }catch{}
    }
    res.writeHead(200,{...headers,'Content-Type':'application/json'});return res.end(JSON.stringify(config));
  }
  const filename = path.join(ROOT, decoded === '/' ? 'index.html' : decoded.slice(1));
  fs.stat(filename, (error, stats) => {
    if (error || !stats.isFile()) {res.writeHead(404, headers); return res.end('Not found');}
    res.writeHead(200, {...headers, 'Content-Type':MIME[path.extname(filename)] || 'application/octet-stream', 'Content-Length':stats.size});
    if (req.method === 'HEAD') return res.end();
    const stream = fs.createReadStream(filename); stream.on('error', () => res.destroy()); stream.pipe(res);
  });
}

export function createLabServer(options = {}) {
  const server = options.cert && options.key ? https.createServer(options, handler) : http.createServer(handler);
  server.on('upgrade', (req, socket, head) => {
    const key = req.headers['sec-websocket-key'];
    let allowedOrigin = false;
    try {const origin = new URL(req.headers.origin); allowedOrigin = origin.host === req.headers.host && ['http:', 'https:'].includes(origin.protocol);} catch {}
    if (req.url !== '/echo' || req.headers['sec-websocket-version'] !== '13' || typeof key !== 'string' || Buffer.from(key,'base64').length !== 16 || !allowedOrigin) {
      socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n'); return;
    }
    const accept = crypto.createHash('sha1').update(key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    let buffer = Buffer.alloc(0), fragmentOpcode = 0, fragments = [], fragmentBytes = 0;
    socket.setTimeout(30000, () => socket.end(encodeFrame(8, Buffer.from([0x03,0xe8]))));
    socket.on('error', () => {});
    socket.on('end', () => socket.end());
    const consume = data => {
      if(socket.destroyed||socket.writableEnded)return;
      try {
        for(let offset=0;offset<data.length;offset+=MAX_FRAME){
        buffer = Buffer.concat([buffer,data.subarray(offset,offset+MAX_FRAME)]);
        const parsed = decodeFrames(buffer); buffer = parsed.remaining;
        if (buffer.length > MAX_FRAME + 14) throw Error('Incomplete frame buffer too large');
        for (const frame of parsed.frames) {
          const {opcode, payload, final} = frame;
          if (opcode === 8) {socket.end(encodeFrame(8,payload)); return;}
          if (opcode === 9) {socket.write(encodeFrame(10,payload)); continue;}
          if (opcode === 10) continue;
          if (![0,1,2].includes(opcode)) throw Error('Unsupported opcode');
          if (opcode === 0) {
            if (!fragmentOpcode) throw Error('Unexpected continuation');
            fragments.push(payload); fragmentBytes += payload.length;
          } else {
            if (fragmentOpcode) throw Error('Unfinished fragment');
            fragmentOpcode = opcode; fragments = [payload]; fragmentBytes = payload.length;
          }
          if (fragmentBytes > MAX_FRAME) throw Error('Message too large');
          if (fragments.length > 128) throw Error('Too many fragments');
          if (final) {
            if (socket.writableLength > MAX_FRAME * 2) throw Error('Slow receiver');
            const complete = Buffer.concat(fragments);
            if (fragmentOpcode === 1) new TextDecoder('utf-8', {fatal:true}).decode(complete);
            socket.write(encodeFrame(fragmentOpcode,complete)); fragmentOpcode = 0; fragments = []; fragmentBytes = 0;
          }
        }
        }
      } catch {socket.end(encodeFrame(8, Buffer.from([0x03,0xea])));}
    };
    socket.on('data', consume); if (head.length) consume(head);
  });
  return server;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const options = process.env.TLS_CERT && process.env.TLS_KEY ? {cert:fs.readFileSync(process.env.TLS_CERT),key:fs.readFileSync(process.env.TLS_KEY)} : {};
  const server = createLabServer(options); const port = Number(process.env.PORT || 8765); const host = process.env.HOST || '127.0.0.1';
  server.listen(port, host, () => process.stdout.write(`Lume Web Lab listening on ${options.cert?'https':'http'}://${host}:${port}\n`));
  const close = () => {server.close(); server.closeAllConnections();};
  process.on('SIGTERM',close); process.on('SIGINT',close);
}
