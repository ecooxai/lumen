"""Expose a newly measured candidate failure without changing accepted credit."""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import copy
import hashlib
import json
import os
import urllib.request


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-summary',required=True,type=Path)
    parser.add_argument('--burst-receipt',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    root=Path.cwd().resolve()
    if str(root)!='/Users/devin/repos/lumen':
        parser.error('Use the observed canonical Mac repository')
    for path in (args.build_summary.resolve(),args.burst_receipt.resolve(),args.output.resolve()):
        path.relative_to(root/'out')
    build=json.loads(args.build_summary.read_text());burst=json.loads(args.burst_receipt.read_text())
    if (not build.get('passed') or not all(row.get('passed') for row in build.get('stages',[]))
            or burst.get('kind')!='shared-worker-native-input-queue-migration'
            or not burst.get('nativeLumenExecuted') or burst.get('infrastructureErrors')
            or burst.get('total')!=3 or burst.get('failed')!=3 or burst.get('passed')!=0
            or burst.get('binarySHA256')!=build['artifacts']['jsrun']['sha256']):
        raise RuntimeError('The observed candidate burst regression does not match the supplied receipt')
    for artifact in build['artifacts'].values():
        if sha(root/artifact['path'])!=artifact['sha256']:
            raise RuntimeError('A frozen candidate artifact differs')
    state=root/'output/session-20261029-cpu30/progress-state'
    target=state/'status.json';before=target.read_bytes();status=json.loads(before)
    candidate=copy.deepcopy(status.get('developmentCandidate',{}))
    if (status.get('taskId')!='20261029-cpu30' or status.get('activeTaskId')!='10300110'
            or candidate.get('binarySha256')!=build['artifacts']['lumen']['sha256']
            or candidate.get('accepted') is not False):
        raise RuntimeError('Live candidate identity differs; no status changed')
    now=datetime.now(timezone.utc).isoformat()
    hold={'active':True,'measuredUTC':burst['ended'],'passed':0,'failed':3,'total':3,
          'kind':burst['kind'],'receiptSHA256':sha(args.burst_receipt),
          'reason':'Queued SharedWorker messages are lost when a transferred endpoint returns and the former recipient terminates.'}
    candidate['hold']=hold
    candidate['earlierFocusedStage']=candidate.get('earlierFocusedStage',candidate.get('stage'))
    candidate['stage']=f"HELD: queued-return burst controls 0/3 passed; message loss is under repair. Earlier {len(build['stages'])} initial gates passed but did not cover this queue-migration case. Full candidate acceptance remains pending."
    status['developmentCandidate']=candidate;status['updatedUTC']=now
    status['phase']='Task 10.30 01:10: candidate 002b is held after a new queued-message return regression passed 0/3 controls. Earlier focused gates passed; the queued-return defect is being repaired and checked separately. Accepted baseline 1454/1515 priority variants and 66.43/100 task score remain unchanged. No candidate release or shared-device IPA execution is claimed.'
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'before-status.json').write_bytes(before);(out/'before-status.json').chmod(0o600)
    encoded=(json.dumps(status,indent=2)+'\n').encode()
    temporary=target.with_suffix('.hold-10300110.tmp')
    if target.read_bytes()!=before:
        raise RuntimeError('Live status changed during composition')
    with temporary.open('xb') as stream:
        stream.write(encoded)
    temporary.chmod(0o600);os.replace(temporary,target)
    receipt={'updatedUTC':now,'passed':False,'iteration':status.get('iteration'),
             'candidateBinarySHA256':candidate['binarySha256'],'hold':hold,
             'acceptedBaselineCreditChanged':False,'privateInboxChanged':False,
             'statusSHA256':hashlib.sha256(encoded).hexdigest()}
    try:
        server=json.loads((state/'server-receipt.json').read_text())
        for name,origin in [('local','http://127.0.0.1:18785'),('public',server['publicURL'])]:
            with urllib.request.urlopen(origin+'/api/progress',timeout=5) as response:
                seen=json.loads(response.read(262144))['status']
            if seen.get('developmentCandidate')!=candidate or seen.get('phase')!=status['phase']:
                raise RuntimeError(name+' did not expose the current candidate hold')
            receipt[name+'Verified']=True
        receipt['passed']=True
    except Exception as error:
        receipt['error']=type(error).__name__+': '+str(error)[:300]
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))
    return 0 if receipt['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
