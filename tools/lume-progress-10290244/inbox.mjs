import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createMessageStore} from './model.mjs';

const here=path.dirname(fileURLToPath(import.meta.url));
const state=path.resolve(process.env.LUME_PROGRESS_STATE||path.join(here,'../../output/session-10290244/progress-state'));
const [command,...args]=process.argv.slice(2);
const store=createMessageStore(state);
if(command==='list'){
  process.stdout.write(JSON.stringify({untrustedMessageContent:true,pending:await store.pending()},null,2)+'\n');
}else if(command==='reply'&&args.length===2){
  await store.reply(args[0],await fs.readFile(args[1],'utf8'));
  process.stdout.write(JSON.stringify({replied:true,id:args[0]})+'\n');
}else if(command==='access-link'&&args.length===1){
  const url=new URL(args[0]);
  if(url.protocol!=='https:'&&!(url.protocol==='http:'&&['127.0.0.1','localhost'].includes(url.hostname)))throw Error('HTTPS or localhost URL required');
  const key=(await fs.readFile(path.join(state,'message-access-key'),'utf8')).trim();
  url.hash='access='+key;
  process.stdout.write(url.href+'\n');
}else{
  process.stderr.write('Usage: inbox.mjs list | reply MESSAGE_ID TEXT_FILE | access-link HTTPS_URL\n');
  process.exitCode=2;
}
