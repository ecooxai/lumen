export const caseNames=["cold A is SyntaxError","cold B is SyntaxError","cold A selects D through B","cold B selects C through A","cold roots select distinct errors","cold A repeat retains selected identity","cold B repeat retains selected identity","warm C and D retain distinct errors","warm B selects C through A","warm A selects D through B","warm roots select distinct errors","warm B repeat retains identity","warm A repeat retains identity","forward declaration order selects C","reverse declaration order selects D","depth first traversal selects nested D","deep root repeat retains D","network failure outranks cached syntax","repeated network failure remains TypeError","network failures are distinct objects","valid module still evaluates"];
export async function runGraphChecks(base) {
 const results=[],check=(i,v,d='')=>results.push({name:caseNames[i],status:v?0:1,message:v?'':String(d||'assertion failed')});
 const caught=async p=>{try{await import(new URL(p,base).href);return null;}catch(e){return e;}};
 const ca=await caught('cold/a.js'),cb=await caught('cold/b.js'),cc=await caught('cold/c.js'),cd=await caught('cold/d.js');
 check(0,ca instanceof SyntaxError);check(1,cb instanceof SyntaxError);check(2,ca===cd);check(3,cb===cc);check(4,ca!==cb);
 check(5,await caught('cold/a.js')===ca);check(6,await caught('cold/b.js')===cb);
 const wc=await caught('warm/c.js'),wd=await caught('warm/d.js'),wb=await caught('warm/b.js'),wa=await caught('warm/a.js');
 check(7,wc!==wd);check(8,wb===wc);check(9,wa===wd);check(10,wa!==wb);
 check(11,await caught('warm/b.js')===wb);check(12,await caught('warm/a.js')===wa);
 const oc=await caught('order/c.js'),od=await caught('order/d.js');
 check(13,await caught('order/forward.js')===oc);check(14,await caught('order/reverse.js')===od);
 check(15,await caught('order/deep-root.js')===od);check(16,await caught('order/deep-root.js')===od);
 const n1=await caught('network.js'),n2=await caught('network.js');
 check(17,n1 instanceof TypeError);check(18,n2 instanceof TypeError);check(19,n1!==n2);
 let ok=false;try{ok=(await import(new URL('valid.js',base).href)).answer===42;}catch(e){}check(20,ok);
 return results;
}
