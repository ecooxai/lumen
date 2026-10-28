import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const here=path.dirname(fileURLToPath(import.meta.url));
const args=process.argv.slice(2);
const value=name=>args[args.indexOf(name)+1];
if(!args.includes('--live-base')||!args.includes('--output'))throw Error('Provide --live-base HTTPS_URL --output DIRECTORY');
const endpoint=new URL(value('--live-base'));
if(endpoint.protocol!=='https:')throw Error('Published live endpoint must use HTTPS');
const destination=path.resolve(value('--output'));
await fs.mkdir(destination,{recursive:true});
for(const name of['index.html','app.js','style.css'])await fs.copyFile(path.join(here,'public',name),path.join(destination,name));
await fs.writeFile(path.join(destination,'config.json'),JSON.stringify({liveBase:endpoint.origin})+'\n');
process.stdout.write(JSON.stringify({output:destination,liveBase:endpoint.origin,scope:'Static view; telemetry and messages require the separate live endpoint.'})+'\n');
