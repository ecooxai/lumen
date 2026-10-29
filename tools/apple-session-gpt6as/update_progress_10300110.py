"""Publish a new task label while retaining separately identified accepted evidence.

The existing transport route and private inbox remain unchanged. No candidate
WPT or performance credit is derived from a successful build.
"""
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-summary', type=Path, required=True)
    parser.add_argument('--boundary-receipt', type=Path, required=True)
    parser.add_argument('--burst-receipt', type=Path,
                        help='Exact native queued-return regression receipt; required when this candidate includes that gate')
    parser.add_argument('--iteration', type=int, required=True)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if str(root) != '/Users/devin/repos/lumen' or not 1 <= args.iteration <= 1000:
        parser.error('Use the observed Mac workspace and an actual task iteration')
    state = root / 'output/session-20261029-cpu30/progress-state'
    target = state / 'status.json'
    old = target.read_bytes()
    status = json.loads(old)
    if status.get('taskId') != '20261029-cpu30' or status.get('fullOfficialWptPassPercent') is not None:
        raise RuntimeError('Existing route identity or WPT scope differs')
    if status.get('activeTaskId') not in ('10292019r', '10300110'):
        raise RuntimeError('Unexpected active task; refusing an unrelated status replacement')
    card = copy.deepcopy(status['currentTaskScorecard'])
    if card.get('task_id') not in ('10292019r', '10300110') or abs(card.get('score', 0) - 66.42694844715456) > 1e-9:
        raise RuntimeError('Baseline score differs from the observed accepted assessment')
    current = status.get('priorityObserved') or status['currentWpt']
    expected = 'f906769431c7a25b28ba53a364ffb5027ca0dd04fa0f8accc1161407203d3b95'
    if (current.get('binarySha256') != expected or current.get('recorded') != 1515
            or current.get('fullyPassing') != 1454 or current.get('unrun') != 0):
        raise RuntimeError('Accepted baseline changed; review a fresh status composition')
    build_path = args.build_summary.resolve()
    build_path.relative_to(root / 'out')
    build = json.loads(build_path.read_text())
    if not build.get('passed') or not all(stage['passed'] for stage in build['stages']):
        raise RuntimeError('The candidate build/regression gate has not passed')
    for artifact in build['artifacts'].values():
        path = (root / artifact['path']).resolve()
        path.relative_to(root / 'out')
        if sha(path) != artifact['sha256']:
            raise RuntimeError('A frozen candidate artifact differs')
    boundary_path = args.boundary_receipt.resolve()
    boundary_path.relative_to(root / 'out')
    boundary = json.loads(boundary_path.read_text())
    if (boundary.get('binarySHA256') != build['artifacts']['jsrun']['sha256']
            or not boundary.get('nativeLumenExecuted') or boundary.get('infrastructureErrors')):
        raise RuntimeError('Boundary regression receipt is not tied to this native candidate')
    boundary_text = (f"Independent SharedWorker boundary controls: {boundary['passed']}/{boundary['total']} passed"
        + ('; remaining failures are under repair. ' if boundary['failed'] else '. '))
    burst, burst_path = None, None
    if any(stage['name'] == 'shared-return-burst' for stage in build['stages']) and args.burst_receipt is None:
        raise RuntimeError('The queued-return gate requires its exact source-bound regression receipt')
    if args.burst_receipt is not None:
        burst_path = args.burst_receipt.resolve(); burst_path.relative_to(root / 'out')
        burst = json.loads(burst_path.read_text())
        if (burst.get('kind') != 'shared-worker-native-input-queue-migration'
                or burst.get('binarySHA256') != build['artifacts']['jsrun']['sha256']
                or not burst.get('nativeLumenExecuted') or burst.get('infrastructureErrors')
                or burst.get('total') != 3 or burst.get('passed') != 3 or burst.get('failed') != 0):
            raise RuntimeError('The exact native queued-return acceptance is incomplete')
        boundary_text += 'Queued-return burst controls: 3/3 passed on this candidate. '
    now = datetime.now(timezone.utc).isoformat()
    card.update(task_id='10300110', human_task_id='10.30 01:10', updated_utc=now,
        score_state='inherited_accepted_baseline_candidate_acceptance_pending',
        accepted_artifact_status='inherited_baseline', goal_reached=False,
        inherited_from_task='10292019r',
        scope='Inherited accepted native baseline under the fixed rubric; the new candidate has no transferred WPT or resource acceptance')
    status.update(activeTaskId='10300110', humanTaskId='10.30 01:10', iteration=args.iteration,
        updatedUTC=now, currentTaskScorecard=card,
        acceptedBaseline={'taskId': '10292019r', 'binarySha256': expected,
            'sourceCommit': current['sourceCommit'], 'score': card['score']},
        developmentCandidate={'binarySha256': build['artifacts']['lumen']['sha256'],
            'sourceCommit': build['sourceBaseCommit'], 'reviewedProductSource': build.get('reviewedProductSource'),
            'buildSummarySHA256': sha(build_path),
            'stage': f"{len(build['stages'])}/{len(build['stages'])} initial native build/regression gates passed; " + boundary_text
                + 'exact-candidate WPT, SharedWorker lifetime stress and CPU/RAM acceptance pending',
            'boundaryReceiptSHA256': sha(boundary_path),
            'boundary': {key: boundary[key] for key in ('passed', 'total', 'failed')},
            'accepted': False, 'scope': 'Actual native macOS candidate, not a shared signed device IPA'})
    if burst is not None:
        status['developmentCandidate']['queuedReturn'] = {'passed': 3, 'total': 3, 'failed': 0,
            'receiptSHA256': sha(burst_path), 'previousCandidateHoldClearedByNewMeasuredArtifact': True}
    status['phase'] = (
        'Task 10.30 01:10: the accepted baseline remains 1454/1515 frozen priority variants fully passing (95.974%). '
        'The new native SharedWorker candidate has passed its build and focused regression gates using prebuilt V8. '
        + boundary_text + 'Its complete WPT comparison, 100-cycle lifecycle stress and resource measurements are being verified separately. '
        'Baseline assessment 66.43/100; no candidate score or performance credit is transferred. '
        'A real iOS 27 iPad simulator is present, but no Lumen app or matching mobile V8/dependency closure was found. '
        'Signed shared-IPA execution remains unverified. Full official WPT pass rate is unknown.')
    status['performance'] = {**status.get('performance', {}), 'binarySha256': expected,
                             'inheritedFromTask': '10292019r'}
    status['apple'] = {'macNative': 'Accepted prior native Mac004 remains available; new SharedWorker candidate is undergoing exact-artifact acceptance',
        'ipadSimulator': 'Genuine Apple iOS 27 iPad runtime observed; no Lumen app found in known target bundle slots; SDK lookup and mobile dependencies remain blocked',
        'sameIpa': 'Unverified: no signed device IPA; zero signing identities and no configured mobile V8/static-dependency closure. Native .app and Simulator .app are distinct artifacts.'}
    out = root / f'out/10300110-apple/progress-update-{args.iteration:03d}'
    out.mkdir(parents=True, exist_ok=False)
    (out / 'before-status.json').write_bytes(old)
    (out / 'before-status.json').chmod(0o600)
    new = (json.dumps(status, indent=2) + '\n').encode()
    pending = target.with_suffix('.10300110.tmp')
    if pending.exists():
        raise RuntimeError('Another task status write is pending')
    if target.read_bytes() != old:
        raise RuntimeError('Live status changed during composition')
    with pending.open('xb') as stream:
        stream.write(new)
    pending.chmod(0o600)
    os.replace(pending, target)
    receipt = {'updatedUTC': now, 'taskId': '10300110', 'transportTaskId': status['taskId'],
        'iteration': args.iteration, 'baselineBinarySHA256': expected,
        'candidateBinarySHA256': build['artifacts']['lumen']['sha256'],
        'candidateAccepted': False, 'inheritedBaselineScore': card['score'],
        'statusSHA256': hashlib.sha256(new).hexdigest(), 'privateInboxModified': False,
        'sourceArchiveModified': False, 'passed': False}
    try:
        server = json.loads((state / 'server-receipt.json').read_text())
        for name, origin in (('local', 'http://127.0.0.1:18785'), ('public', server['publicURL'])):
            with urllib.request.urlopen(origin + '/api/progress', timeout=5) as response:
                observed = json.loads(response.read(262144))['status']
            if (observed.get('activeTaskId') != '10300110' or observed.get('currentTaskScorecard') != card
                    or observed.get('developmentCandidate') != status['developmentCandidate']):
                raise RuntimeError(name + ' endpoint did not expose the exact updated task receipt')
            receipt[name + 'Verified'] = True
        receipt['passed'] = True
    except Exception as error:
        receipt['error'] = type(error).__name__ + ': ' + str(error)[:300]
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
