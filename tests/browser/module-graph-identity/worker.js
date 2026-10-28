import {runGraphChecks,caseNames} from './checks.js';
runGraphChecks(new URL('./',import.meta.url).href).then(results=>postMessage({results}),error=>postMessage({results:caseNames.map(name=>({name,status:1,message:String(error)}))}));
