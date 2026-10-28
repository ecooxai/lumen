#!/usr/bin/env python3
"""Read-only WPT audit. Never treats unrun tests or JS-language tests as web API passes."""
import argparse, collections, hashlib, json, os, subprocess, time
P='/mnt/p3/project/browser/wpt-data'
a=argparse.ArgumentParser();a.add_argument('--output',required=True);a.add_argument('--limit-bytes',type=int);a=a.parse_args()
os.makedirs(a.output,exist_ok=True)
types=json.load(open(P+'/tests.json'))['types']; types={'/'+k.lstrip('/'):v for k,v in types.items()}
base=json.load(open(P+'/chrome-base.json'))
p=P+'/lumen/lumen.jsonl';limit=a.limit_bytes or os.stat(p).st_size
latest={};previous={};n=bad=0;start=time.time()
with open(p,'rb') as f:
 while f.tell()<limit:
  line=f.readline()
  if not line.endswith(b'\n'):break
  n+=1
  try:r=json.loads(line)
  except ValueError:bad+=1;continue
  u=r.get('url');c=base.get(u)
  if c is None:continue
  ls={s[0]:s[1] for s in r.get('subtests',[])}
  cp={k for k,v in c['st'].items() if v=='PASS'}
  lp={k for k,v in ls.items() if v=='PASS'}
  if not c['st']:
   cp={'(harness)'} if c['s']=='OK' else set()
   lp={'(harness)'} if r.get('status')=='OK' else set()
  missing=cp-lp
  rec={'status':r.get('status'),'line':n,'ms':r.get('ms'),'cp':len(cp),'lcpass':len(cp&lp),'missing':len(missing),'ls_total':len(ls),'ls_pass':sum(s=='PASS' for s in ls.values()),'chrome_full':c['s']=='OK' and all(v=='PASS' for v in c['st'].values()),'lumen_full':r.get('status')=='OK' and not missing and all(v=='PASS' for v in ls.values()),'first_missing':[[s[0],s[1],s[2]] for s in r.get('subtests',[]) if s[0] in missing][:3]}
  if not rec['first_missing'] and missing:rec['first_missing']=[[x,'MISSING',r.get('message')] for x in sorted(missing)[:3]]
  if u in latest:previous[u]=latest[u]
  latest[u]=rec
agg=collections.defaultdict(collections.Counter);areas=collections.defaultdict(collections.Counter)
for u,c in base.items():
 typ=types.get(u,'unmapped');r=latest.get(u);cp=sum(v=='PASS' for v in c['st'].values()) if c['st'] else int(c['s']=='OK')
 chrome_full=c['s']=='OK' and all(v=='PASS' for v in c['st'].values())
 for d in (agg[typ],areas['/'+u.split('/')[1]+'/']):
  d['reference_urls']+=1;d['chrome_passing_assertions']+=cp;d['chrome_fully_passing_urls']+=chrome_full
  if r:
   d['observed_urls']+=1;d['matched_chrome_assertions']+=r['lcpass'];d['matched_chrome_fraction_sum']+=(r['lcpass']/cp if cp else 0)
   d['fully_matching_chrome_urls']+=int(chrome_full and r['lumen_full']);d['status_'+str(r['status'])]+=1
  else:d['unobserved_urls']+=1
for group in (agg,areas):
 for d in group.values():
  d['assertion_parity_pct']=round(100*d['matched_chrome_assertions']/d['chrome_passing_assertions'],5) if d['chrome_passing_assertions'] else None
  d['fully_passing_url_parity_pct']=round(100*d['fully_matching_chrome_urls']/d['chrome_fully_passing_urls'],5) if d['chrome_fully_passing_urls'] else None
counts=collections.Counter(types.values())
# Distinct WPT automated URL categories, excluding test262 and manual/visual/support.
auto_types=('testharness','reftest','crashtest','print-reftest','wdspec','aamtest')
auto_total=sum(counts[k] for k in auto_types)
verified=sum(1 for u,r in latest.items() if types.get(u)=='testharness' and r['lumen_full'])
# Conservative completion metric: lower of web testharness assertion parity and verified full automated URL coverage.
score=min(agg['testharness']['assertion_parity_pct'] or 0,100*verified/auto_total)
summary={'utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'elapsed_s':round(time.time()-start,2),'lumen_jsonl_bytes_scanned':limit,'lines':n,'invalid_lines':bad,'reference':json.load(open(P+'/runs.json'))[0],'corpus_url_types':dict(counts),'groups':dict(agg),'automated_corpus_urls':auto_total,'verified_fully_passing_automated_urls':verified,'conservative_task_score':round(score,4),'score_formula':'min(WPT testharness Chrome-passing assertion parity, 100*verified fully passing automated WPT URLs / all automated WPT manifest URLs); all untested categories count zero; test262 excluded','caveats':['Historical latest-per-URL results span several Lumen revisions, not a fresh full baseline run.','Reference Chrome158 uses wpt.fyi Linux report; local Chrome156/Chromium150 are separate comparisons.','Reftest, crashtest, print-reftest, WebDriver and AAM remain unvalidated by custom Lumen harness.','Manual and visual tests require separate validation and are excluded from automated score, not declared passing.']}
for name,data in [('summary.json',summary),('latest.json',latest),('previous.json',previous),('areas.json',dict(areas))]:json.dump(data,open(a.output+'/'+name,'w'),indent=2)
print(json.dumps({'groups':summary['groups'],'score':summary['conservative_task_score'],'automated_urls':auto_total,'latest_urls':len(latest),'lines':n,'seconds':summary['elapsed_s']},indent=2))
