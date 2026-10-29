"""Passive one-second host CPU/RAM telemetry for the remaining Mac gates.

CPU uses Mach HOST_CPU_LOAD_INFO deltas across all logical CPUs. RAM uses the
bounded vm_stat utility and hw.memsize; used means total minus free, including
caches. This is a separate small process, so its own CPU/RSS overhead is measured
without attributing the application's child processes to the sampler.

Apple ABI references: apple-oss-distributions/xnu osfmk/mach/host_info.h and
osfmk/mach/machine.h (HOST_CPU_LOAD_INFO=3; four natural_t CPU counters; idle=2).
"""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import csv
import ctypes
import hashlib
import json
import math
import os
import re
import resource
import signal
import subprocess
import sys
import time


def cpu_percent(previous,current):
    if previous is None:return None
    if len(previous)!=4 or len(current)!=4:raise ValueError('Mach CPU counter count differs')
    deltas=[(value-old)&0xffffffff for old,value in zip(previous,current)]
    if any(value>100000000 for value in deltas):raise ValueError('Mach CPU counters reset or differ unexpectedly')
    total=sum(deltas)
    return 100*(total-deltas[2])/total if total else None


def parse_memory(output,total_bytes):
    match=re.search(r'page size of (\d+) bytes',output)
    if match is None:raise ValueError('vm_stat page size is missing')
    page_size=int(match.group(1))
    pages={match.group(1).strip():int(match.group(2)) for match in
           re.finditer(r'^([^:\n]+):\s+(\d+)\.',output,re.M)}
    if 'Pages free' not in pages or page_size<=0 or total_bytes<=0:
        raise ValueError('Physical memory counters are incomplete')
    free=pages['Pages free']*page_size
    if not 0<=free<=total_bytes:raise ValueError('Free physical memory exceeds total RAM')
    return {'memory_total_gb':total_bytes/1e9,'memory_free_gb':free/1e9,
        'memory_used_gb':(total_bytes-free)/1e9,'page_size_bytes':page_size,
        'free_pages':pages['Pages free'],'inactive_pages':pages.get('Pages inactive'),
        'wired_pages':pages.get('Pages wired down'),'compressor_resident_pages':pages.get('Pages occupied by compressor')}


def digest(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def save(path,value):
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value,indent=2)+'\n');temporary.replace(path)


class HostSampler:
    def __init__(self,output,max_seconds=600):
        self.root=Path.cwd().resolve();self.output=Path(output).resolve()
        self.output.relative_to(self.root/'out')
        self.output.mkdir(parents=True,exist_ok=False)
        self.process=None;self.max_seconds=max_seconds;self.log=None

    def start(self):
        started=time.monotonic();self.log=(self.output/'collector.log').open('x')
        self.process=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--collect',
            '--output',str(self.output),'--owner-pid',str(os.getpid()),'--max-seconds',str(self.max_seconds)],
            stdin=subprocess.DEVNULL,stdout=self.log,stderr=subprocess.STDOUT,start_new_session=True)
        while time.monotonic()-started<5:
            if (self.output/'ready.json').is_file():
                self.startup_seconds=time.monotonic()-started;return self
            if self.process.poll() is not None:raise RuntimeError('Host telemetry initialization failed; collector log retained')
            time.sleep(.05)
        self.process.terminate();self.process.wait(timeout=4)
        raise RuntimeError('Host telemetry initialization exceeded five seconds')

    def finish(self):
        (self.output/'stop-requested').touch(exist_ok=True)
        cleanup=None
        if self.process is not None:
            try:self.process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:cleanup='Owned host sampler did not stop promptly'
        if self.log is not None:self.log.close()
        summary=self.output/'summary.json'
        result=json.loads(summary.read_text()) if summary.is_file() else {'error':'Host telemetry summary is missing'}
        result['collectorStartupSeconds']=round(getattr(self,'startup_seconds',0),6)
        result['ownedCollectorCleanupIssue']=cleanup
        result['summaryPath']=str(summary.relative_to(self.root))
        result['summarySHA256']=digest(summary) if summary.is_file() else None
        result['validOneSecondCoverage']=(self.process is not None and self.process.returncode==0
            and not result.get('error') and not cleanup and result.get('intervalSamples',0)>=1
            and result.get('maxSamplingGapSeconds',math.inf)<=1.5
            and result.get('stopReason')=='requested')
        return result


def collect(args):
    if sys.platform!='darwin':raise RuntimeError('Actual Mac host telemetry requires Darwin')
    output=args.output.resolve();output.relative_to(Path.cwd().resolve()/'out')
    if os.getppid()!=args.owner_pid:raise RuntimeError('Host sampler parent identity differs')
    began=time.monotonic();usage_before=resource.getrusage(resource.RUSAGE_SELF)
    child_before=resource.getrusage(resource.RUSAGE_CHILDREN)
    report={'startedUTC':datetime.now(timezone.utc).isoformat(),'samplingIntervalSeconds':1,
        'cpuUnit':'Percent of total logical CPU capacity,0..100; distinct from per-process one-core percent',
        'memoryUnit':'Decimal GB','memoryDefinition':'Physical total minus free pages; includes inactive/file caches',
        'sourceSHA256':digest(Path(__file__)),'ownerPID':args.owner_pid,'collectorPID':os.getpid(),
        'processInventoryEachTick':False,'priorEvidenceRetroactivelyChanged':False}
    rows=[];requested=False
    def stop(*_):
        nonlocal requested
        requested=True
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    try:
        total=int(subprocess.check_output(['/usr/sbin/sysctl','-n','hw.memsize'],text=True,timeout=2).strip())
        library=ctypes.CDLL('/usr/lib/libSystem.B.dylib')
        library.mach_host_self.argtypes=[];library.mach_host_self.restype=ctypes.c_uint32
        library.host_statistics.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.POINTER(ctypes.c_int32),ctypes.POINTER(ctypes.c_uint32)]
        library.host_statistics.restype=ctypes.c_int
        host=library.mach_host_self();previous=None;next_at=time.monotonic()
        with (output/'samples.csv').open('x',newline='') as stream:
            writer=None
            while time.monotonic()-began<args.max_seconds:
                if requested or (output/'stop-requested').exists():report['stopReason']='requested';break
                if os.getppid()!=args.owner_pid:report['stopReason']='owner_exited';break
                sampled=time.monotonic();ticks=(ctypes.c_uint32*4)();count=ctypes.c_uint32(4)
                code=library.host_statistics(host,3,ctypes.cast(ticks,ctypes.POINTER(ctypes.c_int32)),ctypes.byref(count))
                if code or count.value!=4:raise RuntimeError('Mach host CPU counter read failed')
                current=list(ticks)
                memory=parse_memory(subprocess.check_output(['/usr/bin/vm_stat'],text=True,timeout=2),total)
                value=cpu_percent(previous,current);previous=current
                row={'elapsed_s':round(sampled-began,6),'sampled_utc':datetime.now(timezone.utc).isoformat(),
                    'system_cpu_percent':round(value,6) if value is not None else None,
                    **memory,'cpu_user_ticks':current[0],'cpu_system_ticks':current[1],
                    'cpu_idle_ticks':current[2],'cpu_nice_ticks':current[3],
                    'collection_seconds':round(time.monotonic()-sampled,6)}
                if writer is None:writer=csv.DictWriter(stream,fieldnames=list(row));writer.writeheader()
                writer.writerow(row);stream.flush();rows.append(row)
                if len(rows)==1:save(output/'ready.json',{'collectorPID':os.getpid(),'startedUTC':report['startedUTC']})
                next_at+=1
                while next_at<=time.monotonic():next_at+=1
                time.sleep(max(0,next_at-time.monotonic()))
            else:report['stopReason']='declared_duration_limit'
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error)[:300]
        report['stopReason']='collector_error'
    finally:
        duration=time.monotonic()-began;own=resource.getrusage(resource.RUSAGE_SELF);children=resource.getrusage(resource.RUSAGE_CHILDREN)
        cpu=(own.ru_utime+own.ru_stime-usage_before.ru_utime-usage_before.ru_stime
             +children.ru_utime+children.ru_stime-child_before.ru_utime-child_before.ru_stime)
        samples=[row['system_cpu_percent'] for row in rows if row['system_cpu_percent'] is not None]
        gaps=[second['elapsed_s']-first['elapsed_s'] for first,second in zip(rows,rows[1:])]
        p95=lambda values:sorted(values)[math.ceil(.95*len(values))-1] if values else None
        report.update(finishedUTC=datetime.now(timezone.utc).isoformat(),durationSeconds=round(duration,6),
            samples=len(rows),intervalSamples=len(samples),maxSamplingGapSeconds=round(max(gaps),6) if gaps else None,
            systemCPU_P95Percent=p95(samples),systemCPU_PeakPercent=max(samples) if samples else None,
            memoryUsedGB_Peak=max((row['memory_used_gb'] for row in rows),default=None),memoryTotalGB=rows[0]['memory_total_gb'] if rows else None,
            collectionSecondsP95=p95([row['collection_seconds'] for row in rows]),
            collectionSecondsPeak=max((row['collection_seconds'] for row in rows),default=None),
            overheadCPUSeconds=round(cpu,6),overheadMeanOneCoreCPUPercent=round(100*cpu/duration,6),
            overheadPeakCollectorRSSMiB=own.ru_maxrss/1048576,overheadPeakChildUtilityRSSMiB=children.ru_maxrss/1048576,
            overheadScope='Collector process since main entered plus its sysctl/vm_stat children; interpreter startup before main is excluded')
        if (output/'samples.csv').is_file():report['samplesSHA256']=digest(output/'samples.csv')
        save(output/'summary.json',report)
    return 1 if report.get('error') else 0


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--collect',action='store_true',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--owner-pid',type=int,required=True)
    parser.add_argument('--max-seconds',type=float,default=600)
    args=parser.parse_args()
    if not 1<=args.max_seconds<=900:parser.error('Bound host collection to1..900seconds')
    raise SystemExit(collect(args))
