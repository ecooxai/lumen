#!/usr/bin/env python3
"""Sample authoritative shared-cgroup CPU counters every second; separate from process RSS/PSS."""
import argparse,csv,datetime,json,pathlib,signal,statistics,subprocess,time

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True)
    parser.add_argument('--cgroup',help='Absolute cgroup directory; default: shared Lumen compile slice')
    parser.add_argument('--duration',type=float,default=18000)
    args=parser.parse_args()
    group=args.cgroup
    if not group:
        relative=subprocess.run(['systemctl','--user','show','codex-lumen-compile.slice','-p','ControlGroup','--value'],check=True,capture_output=True,text=True).stdout.strip()
        if not relative.startswith('/'):raise RuntimeError('Shared cgroup path unavailable')
        group='/sys/fs/cgroup'+relative
    group=pathlib.Path(group);output=pathlib.Path(args.output);output.parent.mkdir(parents=True,exist_ok=True)
    stop=[False]
    for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,lambda *_:stop.__setitem__(0,True))
    started=time.monotonic_ns();rows=[];before=None;index=0
    fields=['utc','monotonic_ns','elapsed_s','usage_usec','user_usec','system_usec','nr_periods','nr_throttled','throttled_usec','cpu_pct','quota_usec','period_usec']
    with output.open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=fields);writer.writeheader()
        while not stop[0]:
            now=time.monotonic_ns()
            if now-started>args.duration*1e9:break
            values=dict((key,int(value)) for key,value in (line.split() for line in (group/'cpu.stat').read_text().splitlines()))
            quota,period=(group/'cpu.max').read_text().split();row={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'monotonic_ns':now,'elapsed_s':round((now-started)/1e9,6),'quota_usec':quota,'period_usec':int(period),'cpu_pct':None}
            row.update({key:values.get(key,0) for key in fields[3:9]})
            if before and now>before['monotonic_ns']:row['cpu_pct']=round((row['usage_usec']-before['usage_usec'])*100000/(now-before['monotonic_ns']),6)
            writer.writerow(row);stream.flush();rows.append(row);before=row;index+=1
            remaining=(started+index*1000000000-time.monotonic_ns())/1e9
            if remaining>0:time.sleep(min(remaining,1))
    cpus=[row['cpu_pct'] for row in rows if row['cpu_pct'] is not None]
    weighted=(rows[-1]['usage_usec']-rows[0]['usage_usec'])*100000/(rows[-1]['monotonic_ns']-rows[0]['monotonic_ns']) if len(rows)>1 else None
    result={'source':'Linux cgroup v2 cpu.stat usage_usec deltas','group':str(group),'interval_seconds':1,'cpu_one_core_equals_100':True,'samples':len(rows),'cpu_median':statistics.median(cpus) if cpus else None,'cpu_max':max(cpus) if cpus else None,'cpu_weighted_mean':weighted,'csv':str(output),'scope':'All processes in this shared slice, including concurrent builds/tests; process-tree CPU remains an estimate and RSS/PSS are collected separately.','caveat':'A1-second window may cross fractional100msquota periods; sustained weighted use and rawperiod/throttling counters distinguish quota enforcement from window alignment.'}
    output.with_suffix('.summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,sort_keys=True))

if __name__=='__main__':main()
