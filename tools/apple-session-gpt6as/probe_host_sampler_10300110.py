"""Four-second passive native Mac telemetry readiness probe; no browser load."""
from pathlib import Path
import argparse
import json
import sys
import time


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path('out/10300110-apple/host-sampler-smoke'))
    args=parser.parse_args();root=Path.cwd().resolve()
    if sys.platform!='darwin' or str(root)!='/Users/devin/repos/lumen':
        parser.error('Use the observed canonical native Mac repository')
    sys.path.insert(0,str(root/'tools'))
    from monitor_macos_host_10300110 import HostSampler
    sampler=HostSampler(args.output,max_seconds=15);error=None
    try:
        sampler.start();time.sleep(3.2)
    except Exception as issue:
        error=type(issue).__name__+': '+str(issue)
    finally:
        report=sampler.finish()
    report['scope']='Passive host telemetry readiness only; no browser performance acceptance'
    if error:report['probeError']=error
    print(json.dumps(report,indent=2))
    return 0 if not error and report['validOneSecondCoverage'] else 1


if __name__=='__main__':raise SystemExit(main())
