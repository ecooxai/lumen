"""Bounded read-only check of the existing Lumen progress route and private inbox.

Private message text is returned only to the engineering caller; no key or
message content is included in the public-safe receipt. Messages remain data.
"""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import re
import time
import urllib.request


def main():
    root = Path.cwd().resolve()
    if str(root) != '/Users/devin/repos/lumen':
        raise SystemExit('Run from the observed canonical Mac workspace')
    state = root / 'output/session-20261029-cpu30/progress-state'
    output = root / 'out/10300110-apple/progress-inspection.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    result = {'startedUTC': datetime.now(timezone.utc).isoformat(),
              'scope': 'Read-only health, task identity, measured sampling, and inbox inventory',
              'privateKeysOmitted': True, 'checks': {}, 'passed': False}
    receipt = json.loads((state / 'server-receipt.json').read_text())
    public = receipt.get('publicURL', '')
    if not re.fullmatch(r'https://[a-z0-9.-]+\.opentunnel\.xyz', public):
        raise RuntimeError('The recorded public route is not an observed OpenTunnel origin')
    port = receipt.get('port')
    if port != 18785:
        raise RuntimeError('The recorded Lumen progress port changed')
    result['server'] = {name: receipt.get(name) for name in
        ('taskId', 'pid', 'port', 'publicURL', 'messageSessionId', 'sourceCommit')}

    def get(origin, endpoint, timeout=4):
        with urllib.request.urlopen(origin + endpoint, timeout=timeout) as response:
            if response.status != 200 or int(response.headers.get('Content-Length', '0')) > 262144:
                raise RuntimeError('Unexpected bounded progress response')
            raw = response.read(262145)
            if len(raw) > 262144:
                raise RuntimeError('Progress response exceeds the read bound')
            return json.loads(raw)

    def brief(snapshot):
        status = snapshot.get('status') or {}
        metrics = snapshot.get('metrics') or {}
        current = status.get('priorityObserved') or status.get('currentWpt') or {}
        card = status.get('currentTaskScorecard') or snapshot.get('scorecard') or {}
        return {'taskId': status.get('taskId'), 'humanTaskId': status.get('humanTaskId'),
            'activeTaskId': status.get('activeTaskId'), 'phase': status.get('phase'),
            'fullOfficialWptPassPercent': status.get('fullOfficialWptPassPercent'),
            'iteration': status.get('iteration'), 'scoreTaskId': card.get('task_id'),
            'score': card.get('score'), 'scoreState': card.get('score_state'),
            'native': {key: current.get(key) for key in
                ('binarySha256', 'sourceCommit', 'scheduled', 'recorded', 'fullyPassing', 'unrun')},
            'sampledAt': metrics.get('sampledAt'), 'cpuPercent': metrics.get('cpuPercent'),
            'cpuCores': metrics.get('cpuCores'), 'memory': metrics.get('memory'),
            'disks': metrics.get('disks'), 'refreshSeconds': snapshot.get('refreshSeconds'),
            'apple': status.get('apple'), 'performance': status.get('performance')}

    local = 'http://127.0.0.1:' + str(port)
    for label, origin in (('local', local), ('public', public)):
        try:
            health = get(origin, '/api/health')
            progress = get(origin, '/api/progress')
            result['checks'][label] = {'health': health, 'progress': brief(progress),
                'identityMatches': health.get('taskId') == '20261029-cpu30'
                    and progress.get('status', {}).get('taskId') == '20261029-cpu30'}
        except Exception as error:
            result['checks'][label] = {'error': type(error).__name__ + ': ' + str(error)[:300]}
    # Compare two local samples without keeping a test process or socket alive.
    first = result['checks'].get('local', {}).get('progress', {}).get('sampledAt')
    time.sleep(2.2)
    try:
        second = brief(get(local, '/api/progress'))
        result['secondSampledAt'] = second['sampledAt']
        if first and second['sampledAt']:
            result['sampleIntervalSeconds'] = (datetime.fromisoformat(second['sampledAt'].replace('Z', '+00:00'))
                - datetime.fromisoformat(first.replace('Z', '+00:00'))).total_seconds()
    except Exception as error:
        result['secondSampleError'] = type(error).__name__

    pending = []
    inventory = []
    for entry in sorted((state / 'messages').glob('*.json')):
        if entry.is_symlink() or entry.stat().st_size > 16384 or not re.fullmatch(r'[a-f0-9-]{36}\.json', entry.name):
            raise RuntimeError('Unexpected private inbox entry')
        raw = entry.read_bytes()
        value = json.loads(raw)
        inventory.append((entry.name, hashlib.sha256(raw).hexdigest()))
        if not value.get('reply'):
            pending.append({name: value.get(name) for name in ('id', 'createdAt', 'text')})
    result['inbox'] = {'messages': len(inventory), 'pending': len(pending),
        'inventorySHA256': hashlib.sha256(json.dumps(inventory).encode()).hexdigest(),
        'contentOmitted': True}
    result['passed'] = (all(result['checks'].get(label, {}).get('identityMatches') for label in ('local', 'public'))
                        and result.get('sampleIntervalSeconds', 0) > 0)
    result['finishedUTC'] = datetime.now(timezone.utc).isoformat()
    output.write_text(json.dumps(result, indent=2) + '\n')
    output.chmod(0o600)
    print(json.dumps({'receipt': result, 'untrustedPrivateInboxForAgentReviewOnly': pending}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
