#!/usr/bin/env python3
"""Encode a previously verified public-page capture; no browser or protected helper inspection."""
import argparse,csv,datetime,hashlib,json,math,os,pathlib,re,signal,subprocess,threading,time
p=argparse.ArgumentParser();p.add_argument('--capture',type=pathlib.Path,required=True);a=p.parse_args()
out=a.capture.resolve();receipt=json.loads((out/'receipt.json').read_text())
if receipt.get('status')!='PASS' or not receipt.get('cleanup_complete'):raise SystemExit('Capture must pass and drain first')
frames=receipt['frames'];duration=float(receipt['duration_seconds'])
if not 30<=duration<=45 or not frames:raise SystemExit('Unexpected capture duration')
def sha(q):return hashlib.sha256(q.read_bytes()).hexdigest()
def verify_frames():
 for f in frames:
  q=out/f['file']
  if q.parent!=out or not q.is_file() or sha(q)!=f['sha256']:raise RuntimeError('Frame identity changed')
verify_frames()
if any(not re.fullmatch(r'[A-Za-z0-9_.-]+\.png',f['file']) or not math.isfinite(float(f['hold_seconds'])) or float(f['hold_seconds'])<=0 for f in frames):raise SystemExit('Invalid verified frame metadata')
if abs(sum(float(f['hold_seconds']) for f in frames)-duration)>0.001:raise SystemExit('Frame durations do not match the plan')
concat=out/'verified-frames.ffconcat'
lines=['ffconcat version 1.0']
for f in frames:lines+=['file '+f['file'],'duration %.9f'%float(f['hold_seconds'])]
lines+=['file '+frames[-1]['file']]
concat.write_text('\n'.join(lines)+'\n');concat_pin=sha(concat)
name='lumen-linux-gpt-6-astra-pro-builtin-rubywebterm-chromium-ffmpeg-checkpoint214.mp4'
video=out/name
if video.exists():raise SystemExit('Refuse overwriting a video')
argv=['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-n','-filter_threads','1','-f','concat','-safe','1','-i',str(concat),'-vf','fps=30,format=yuv420p,setsar=1','-c:v','libx264','-threads','2','-preset','medium','-crf','18','-t',str(duration),'-an','-movflags','+faststart',str(video)]
start=datetime.datetime.now(datetime.timezone.utc).isoformat();clock=time.monotonic();done=threading.Event();rows=[];errors=[]
with (out/'encoder.log').open('wb') as log:
 proc=subprocess.Popen(argv,stdin=subprocess.DEVNULL,stdout=log,stderr=log,start_new_session=True)
 def proc_stat(pid):
  data=pathlib.Path('/proc/'+str(pid)+'/stat').read_text();fields=data[data.rfind(')')+2:].split()
  return {'pid':pid,'ppid':int(fields[1]),'pgrp':int(fields[2]),'cpu_ticks':int(fields[11])+int(fields[12]),'start_ticks':int(fields[19]),'rss_bytes':int(fields[21])*os.sysconf('SC_PAGE_SIZE')}
 identity=proc_stat(proc.pid)['start_ticks'];known={};previous={}
 def sample():
  try:
   now=time.monotonic();owned=[]
   # FFmpeg uses threads; include any process it creates in its dedicated process group.
   for entry in pathlib.Path('/proc').iterdir():
    if not entry.name.isdigit():continue
    try:
     q=proc_stat(int(entry.name))
     if q['pgrp']==proc.pid:owned.append(q)
    except (FileNotFoundError,ProcessLookupError,PermissionError,ValueError):pass
   cpu=0.0;rss=0
   for q in owned:
    k=(q['pid'],q['start_ticks']);old=previous.get(k)
    if old:cpu+=max(0,q['cpu_ticks']-old[0])/os.sysconf('SC_CLK_TCK')/(now-old[1])*100
    previous[k]=(q['cpu_ticks'],now);known[k]=True;rss+=q['rss_bytes']
   if owned:rows.append({'elapsed_seconds':now-clock,'cpu_percent_one_core':cpu,'rss_bytes':rss,'owned_process_count':len(owned)})
  except Exception as e:errors.append(type(e).__name__+': '+str(e))
 def watch():
  sample()
  while not done.wait(1.0):sample()
 thread=threading.Thread(target=watch,name='public-showcase-encoder-monitor');thread.start()
 try:
  code=proc.wait(timeout=180)
 except subprocess.TimeoutExpired:
  os.killpg(proc.pid,signal.SIGTERM)
  try:code=proc.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);code=proc.wait(timeout=5)
 finally:
  done.set();thread.join(timeout=5)
verify_frames()
if sha(concat)!=concat_pin:raise RuntimeError('Verified concat changed')
remaining=[]
for entry in pathlib.Path('/proc').iterdir():
 if not entry.name.isdigit():continue
 try:
  q=proc_stat(int(entry.name))
  if q['pgrp']==proc.pid:remaining.append(q['pid'])
 except (FileNotFoundError,ProcessLookupError,PermissionError,ValueError):pass
with (out/'encoder-resources.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=['elapsed_seconds','cpu_percent_one_core','rss_bytes','owned_process_count']);w.writeheader();w.writerows(rows)
probe={}
if code==0:
 probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_entries','stream=codec_name,width,height,pix_fmt,sample_aspect_ratio:format=duration,size','-of','json',str(video)]))
 (out/'video-probe.json').write_text(json.dumps(probe,indent=2)+'\n')
streams=probe.get('streams',[])
valid=code==0 and len(streams)==1 and streams[0].get('codec_name')=='h264' and streams[0].get('width')==1024 and streams[0].get('height')==768 and streams[0].get('pix_fmt')=='yuv420p' and streams[0].get('sample_aspect_ratio')=='1:1' and abs(float(probe.get('format',{}).get('duration','0'))-duration)<0.2
result={'status':'PASS' if valid and rows and not remaining and not thread.is_alive() and not errors else 'FAIL','started_utc':start,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'elapsed_seconds':time.monotonic()-clock,'exit_code':code,'resource_sample_interval_seconds':1,'resource_samples':len(rows),'peak_owned_rss_bytes':max((x['rss_bytes'] for x in rows),default=0),'peak_owned_cpu_percent_one_core':max((x['cpu_percent_one_core'] for x in rows),default=0),'remaining_owned_pids':remaining,'monitor_joined':not thread.is_alive(),'monitor_errors':errors,'frames_unchanged':True,'verified_concat_sha256':concat_pin,'video_filename':name,'video_sha256':sha(video) if video.exists() else None,'video_bytes':video.stat().st_size if video.exists() else None,'duration_seconds':probe.get('format',{}).get('duration'),'dimensions':[streams[0].get('width'),streams[0].get('height')] if streams else None,'method':'Public-results still-frame walkthrough rendered in Chromium and encoded with FFmpeg; not native Lumen UI footage or a performance benchmark.'}
(out/'encoder-receipt.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:result[k] for k in ['status','exit_code','resource_samples','monitor_joined','frames_unchanged','video_filename','video_sha256','video_bytes','duration_seconds','dimensions']}))
raise SystemExit(0 if result['status']=='PASS' else 1)
