(()=>{
const params=new URLSearchParams(window.location.search),provider=params.get('provider'),state=params.get('state'),parentOrigin=params.get('parentOrigin');
const allowed=['google','apple'].includes(provider)&&typeof state==='string'&&state.length>=16&&parentOrigin===window.location.origin&&Boolean(window.opener);
document.getElementById('title').textContent=`${provider==='apple'?'Apple':'Google'}-style popup test`;
document.getElementById('details').textContent=allowed?'The opener must verify this window, its exact origin, and the one-time state value.':'Invalid or missing simulation parameters. Open this flow from the lab.';
for(const id of ['approve','cancel'])document.getElementById(id).disabled=!allowed;
function finish(status){if(!allowed)return;window.opener.postMessage({type:'lume-mock-oauth',provider,state,status},parentOrigin);document.getElementById('result').textContent='Simulated response returned. This window will close.';setTimeout(()=>window.close(),150);}
document.getElementById('approve').addEventListener('click',()=>finish('approved'));document.getElementById('cancel').addEventListener('click',()=>finish('cancelled'));

})();
