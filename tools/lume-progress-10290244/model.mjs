import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';

const execute = promisify(execFile);
const round = (value, places=3) => Number(value.toFixed(places));
const ID = /^[a-f0-9-]{36}$/;

export function cpuPercent(previous, current) {
  if (!previous || current.total <= previous.total) return null;
  return round(Math.max(0,Math.min(100,100 * (1 - (current.idle-previous.idle)/(current.total-previous.total)))), 2);
}

function cpuTotals() {
  return os.cpus().reduce((sum, cpu) => ({idle:sum.idle+cpu.times.idle,total:sum.total+Object.values(cpu.times).reduce((a,b)=>a+b,0)}), {idle:0,total:0});
}

export function parseDisks(output) {
  const disks=[];
  for (const line of output.split('\n').slice(1)) {
    const match=line.match(/^(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)%\s+(.+)$/);
    if (!match) continue;
    disks.push({filesystem:match[1],mount:match[6],totalGB:round(Number(match[2])*1024/1e9,4),usedGB:round(Number(match[3])*1024/1e9,4),availableGB:round(Number(match[4])*1024/1e9,4),capacityPercent:Number(match[5])});
  }
  return disks;
}

export function publicDisks(disks) {
  const standard=new Set(['/','/System/Volumes/Data','/System/Volumes/VM','/System/Volumes/Preboot','/System/Volumes/Update','/System/Volumes/xarts','/System/Volumes/iSCPreboot','/System/Volumes/Hardware','/System/Volumes/Recovery']);
  let privateCount=0;
  return disks.map(({mount,totalGB,usedGB,availableGB,capacityPercent})=>({mount:standard.has(mount)?mount:`Private volume ${++privateCount}`,totalGB,usedGB,availableGB,capacityPercent}));
}

export function createSampler() {
  let previous=null;
  return async () => {
    const current=cpuTotals();
    const cpu=cpuPercent(previous,current);previous=current;
    let disks=[], diskError=null;
    try {disks=publicDisks(parseDisks((await execute('df',['-kP'],{timeout:1500,maxBuffer:1024*1024})).stdout));}
    catch {diskError='Disk sampling unavailable';}
    const total=os.totalmem(),free=os.freemem();
    return {sampledAt:new Date().toISOString(),intervalSeconds:2,cpuPercent:cpu,cpuCores:os.cpus().length,memory:{totalGB:round(total/1e9),freeGB:round(free/1e9),usedGB:round((total-free)/1e9),definition:'Used is total minus free; filesystem caches are included. Not an application-memory measurement.'},disks,diskError,diskNote:'Volumes can share physical storage; do not sum their capacities.',serverRSSMiB:round(process.memoryUsage().rss/1048576),uptimeSeconds:Math.floor(os.uptime())};
  };
}

export async function readJson(filename, fallback) {
  try {return JSON.parse(await fs.readFile(filename,'utf8'));}
  catch {return fallback;}
}

export async function atomicJson(filename, value) {
  await fs.mkdir(path.dirname(filename),{recursive:true});
  const temp=filename+'.'+crypto.randomUUID()+'.tmp';
  await fs.writeFile(temp,JSON.stringify(value,null,2)+'\n',{mode:0o600});
  await fs.rename(temp,filename);
}

export function createMessageStore(stateDir) {
  const directory=path.join(stateDir,'messages');
  const keyHash=key=>crypto.createHash('sha256').update(key).digest('hex');
  const matches=(actual,expected)=>typeof actual==='string'&&/^[a-f0-9]{64}$/.test(actual)&&crypto.timingSafeEqual(Buffer.from(actual,'hex'),Buffer.from(expected,'hex'));
  const load=async id=>ID.test(id)?readJson(path.join(directory,id+'.json'),null):null;
  return {
    async submit(text) {
      if (typeof text!=='string'||!text.trim()||text.length>2000) throw Error('Message must contain between 1 and 2000 characters');
      await fs.mkdir(directory,{recursive:true});
      if ((await fs.readdir(directory)).filter(x=>x.endsWith('.json')).length>=1000) throw Error('Inbox capacity reached');
      const id=crypto.randomUUID(),key=crypto.randomBytes(24).toString('hex');
      await atomicJson(path.join(directory,id+'.json'),{id,text:text.trim(),createdAt:new Date().toISOString(),keyHash:keyHash(key),reply:null,repliedAt:null});
      return {id,key,createdAt:new Date().toISOString(),delivery:'Stored for the agent; a reply appears after the agent reviews it.'};
    },
    async read(id,key) {
      const value=await load(id);
      if (!value||typeof key!=='string'||!matches(value.keyHash,keyHash(key))) return null;
      const {keyHash:privateKey,...publicValue}=value;
      return publicValue;
    },
    async pending() {
      let files=[];try {files=await fs.readdir(directory);} catch {return [];}
      const rows=[];
      for (const file of files.filter(x=>ID.test(x.slice(0,-5))&&x.endsWith('.json'))) {
        const value=await readJson(path.join(directory,file),null);
        if (value&&!value.reply) rows.push({id:value.id,text:value.text,createdAt:value.createdAt});
      }
      return rows.sort((a,b)=>a.createdAt.localeCompare(b.createdAt));
    },
    async reply(id,text) {
      const value=await load(id);
      if (!value) throw Error('Unknown message');
      if (typeof text!=='string'||!text.trim()||text.length>10000) throw Error('Reply must contain between 1 and 10000 characters');
      await atomicJson(path.join(directory,id+'.json'),{...value,reply:text.trim(),repliedAt:new Date().toISOString()});
    }
  };
}
