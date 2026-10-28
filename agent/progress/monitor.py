#!/usr/bin/env python3
"""One-second Linux process-tree CPU/RSS monitoring; no third-party dependencies."""
import argparse,csv,json,os,pathlib,signal,statistics,time,datetime

def proc_table():
    result={}
    for p in pathlib.Path('/proc').iterdir():
        if not p.name.isdigit(): continue
        try:
            text=(p/'stat').read_text(); end=text.rfind(')'); fields=text[end+2:].split()
            result[int(p.name)]={'comm':text[text.find('(')+1:end], 'ppid':int(fields[1]), 'ticks':int(fields[11])+int(fields[12]),'start':int(fields[19]),'rss':int(fields[21])*os.sysconf('SC_PAGE_SIZE')}
        except (OSError,ValueError,IndexError): pass
    return result

def host_sample():
    vals=list(map(int,pathlib.Path('/proc/stat').read_text().splitlines()[0].split()[1:])); vals=vals[:8]
    mem={k:int(v.split()[0])*1024 for k,v in (line.split(':',1) for line in pathlib.Path('/proc/meminfo').read_text().splitlines())}
    return sum(vals),vals[3]+(vals[4] if len(vals)>4 else 0),(mem['MemTotal']-mem['MemAvailable'])/1e6

def run():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root-pid',type=int,required=True); ap.add_argument('--comm',help='Exact comm to select within the process tree')
    ap.add_argument('--exe',help='Exact real executable path selected within the tracked process tree'); ap.add_argument('--output',required=True); ap.add_argument('--duration',type=float,default=18000); ap.add_argument('--interval',type=float,default=1)
    ap.add_argument('--pss-every',type=int,default=5); ap.add_argument('--label',default='test')
    args=ap.parse_args()
    if args.exe: args.exe=os.path.realpath(args.exe)
    def same_exe(pid):
        if not args.exe:return True
        try:return os.path.realpath(os.readlink(f'/proc/{pid}/exe'))==args.exe
        except OSError:return False
    if args.interval<1: ap.error('interval must be at least one second')
    output=pathlib.Path(args.output); output.parent.mkdir(parents=True,exist_ok=True)
    start=time.monotonic(); wall=datetime.datetime.now(datetime.timezone.utc).isoformat(); hz=os.sysconf('SC_CLK_TCK'); prev={}; prevtime=None; prevhost=None; rows=[]; stop=[False]; seenroot=False; count=0
    for sig in (signal.SIGINT,signal.SIGTERM): signal.signal(sig,lambda *_:stop.__setitem__(0,True))
    fields=['utc','elapsed_s','pids','cpu_pct','rss_mb','pss_mb','host_cpu_pct','host_mem_used_mb']
    with output.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=fields); writer.writeheader()
        while not stop[0] and time.monotonic()-start<=args.duration:
            now=time.monotonic(); table=proc_table(); root=table.get(args.root_pid)
            if root: seenroot=True
            if not root and seenroot: break
            if not root and now-start>3: break
            ids={args.root_pid}; changed=True
            while changed:
                new={pid for pid,p in table.items() if p['ppid'] in ids}; old=len(ids); ids|=new; changed=len(ids)!=old
            selected={pid:p for pid,p in table.items() if pid in ids and (not args.comm or p['comm']==args.comm) and same_exe(pid)}
            cpu=None
            if prevtime is not None:
                ticks=0
                for pid,p in selected.items():
                    before=prev.get(pid)
                    if before and before['start']==p['start']: ticks+=max(0,p['ticks']-before['ticks'])
                    elif p['start']/hz >= float(pathlib.Path('/proc/uptime').read_text().split()[0])-(now-prevtime)-0.1: ticks+=p['ticks']
                cpu=100*ticks/hz/(now-prevtime)
            pss=None
            if count%max(1,args.pss_every)==0:
                values=[]
                for pid in selected:
                    try:
                        line=next(x for x in pathlib.Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines() if x.startswith('Pss:')); values.append(int(line.split()[1])*1024/1e6)
                    except (OSError,StopIteration,ValueError): pass
                if values: pss=sum(values)
            host=host_sample(); hostcpu=None
            if prevhost and host[0]>prevhost[0]: hostcpu=100*(1-(host[1]-prevhost[1])/(host[0]-prevhost[0]))
            row={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'elapsed_s':round(now-start,3),'pids':';'.join(map(str,sorted(selected))),'cpu_pct':None if cpu is None else round(cpu,3),'rss_mb':round(sum(p['rss'] for p in selected.values())/1e6,3),'pss_mb':None if pss is None else round(pss,3),'host_cpu_pct':None if hostcpu is None else round(hostcpu,3),'host_mem_used_mb':round(host[2],3)}
            writer.writerow(row); f.flush(); rows.append(row); prev=selected; prevtime=now; prevhost=host; count+=1
            delay=start+count*args.interval-time.monotonic()
            if delay>0: time.sleep(delay)
    cpus=[r['cpu_pct'] for r in rows if r['cpu_pct'] is not None]; rss=[r['rss_mb'] for r in rows]; ps=[r['pss_mb'] for r in rows if r['pss_mb'] is not None]
    summary={'label':args.label,'started_utc':wall,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'root_pid':args.root_pid,'comm':args.comm,'exe':args.exe,'interval_seconds':args.interval,'samples':len(rows),'elapsed_seconds':round(time.monotonic()-start,3),'cpu_one_core_equals_100':True,'cpu_min':min(cpus) if cpus else None,'cpu_median':statistics.median(cpus) if cpus else None,'cpu_max':max(cpus) if cpus else None,'cpu_last10_median':statistics.median(cpus[-10:]) if cpus else None,'rss_peak_mb':max(rss) if rss else None,'rss_final_mb':rss[-1] if rss else None,'pss_peak_mb':max(ps) if ps else None,'csv':str(output),'caveat':'Interval snapshots can miss processes that start and exit between samples; RSS can double-count shared pages across processes. PSS sampled separately.'}
    output.with_suffix('.summary.json').write_text(json.dumps(summary,indent=2)+'\n'); print(json.dumps(summary,sort_keys=True))
if __name__=='__main__':run()
