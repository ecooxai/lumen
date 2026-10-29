#!/usr/bin/env python3
"""Exercise publication boundaries in isolated temporary Git histories."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def command(root, *args):
    return subprocess.run(args, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, timeout=20, check=True)


def scenario(packer, files, removed, expect_allowed, name):
    with tempfile.TemporaryDirectory(prefix='lume-publication-policy-') as directory:
        root = Path(directory)
        command(root, 'git', 'init', '-q', '-b', 'publication-policy-test')
        for relative, data in {'src/main.c': 'int main(void){return 0;}\n', **files}.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data)
        command(root, 'git', 'add', '--all')
        commit = ['git', '-c', 'user.name=Lume publication fixture',
                  '-c', 'user.email=lume-fixture@example.invalid', 'commit', '-qm']
        command(root, *commit, 'fixture initial source')
        for relative in removed:
            command(root, 'git', 'rm', '-q', '--', relative)
        if removed:
            command(root, *commit, 'fixture remove generated material from HEAD')
        environment = dict(os.environ, LUME_TASK_ID='publication-policy-test',
                           LUME_TASK_BRANCH='publication-policy-test', GIT_PAGER='cat', PAGER='cat')
        began = time.monotonic()
        result = subprocess.run(['python3', str(packer)], cwd=root, env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, timeout=30)
        aliases = list(root.glob('output/session-*/public/src.tar.gz'))
        if expect_allowed:
            passed = result.returncode == 0 and len(aliases) == 1
        else:
            # Both current and historical generated files must be refused before
            # an archive can expose them. No specific implementation is assumed.
            passed = result.returncode != 0 and not aliases
        return {'name': name, 'expectedAllowed': expect_allowed, 'exitCode': result.returncode,
                'archiveCreated': bool(aliases), 'passed': passed,
                'wallSeconds': round(time.monotonic() - began, 3),
                'failureDetail': '' if passed else (result.stderr[-1200:] or 'Unexpected publication result')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packer', type=Path, default=Path('tools/apple-session-gpt6as/package_source_10290244.py'))
    parser.add_argument('--receipt', type=Path)
    args = parser.parse_args()
    packer = args.packer.resolve()
    began = time.monotonic()
    started = datetime.now(timezone.utc).isoformat()
    cases = [
        ('current out directory is excluded', {'out/lume-test/private-build.log': 'task-private fixture marker\n'}, [], False),
        ('removed out directory is excluded from portable history', {'out/lume-test/private-build.log': 'task-private historical fixture marker\n'}, ['out/lume-test/private-build.log'], False),
        ('existing output exclusion still holds', {'output/task/runtime.log': 'runtime fixture\n'}, [], False),
        ('existing historical output exclusion still holds', {'output/task/runtime.log': 'historical runtime fixture\n'}, ['output/task/runtime.log'], False),
        ('source directories and prefix lookalikes remain publishable', {'src/out/helpers.c': 'int helper(void){return 1;}\n', 'outcome.txt': 'valid project source\n'}, [], True),
    ]
    rows = [scenario(packer, files, removed, allowed, name) for name, files, removed, allowed in cases]
    receipt = {'schemaVersion': 1, 'startedUTC': started,
               'finishedUTC': datetime.now(timezone.utc).isoformat(),
               'wallSeconds': round(time.monotonic() - began, 3),
               'passed': sum(row['passed'] for row in rows), 'total': len(rows), 'tests': rows}
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))
    raise SystemExit(0 if receipt['passed'] == receipt['total'] else 1)


if __name__ == '__main__':
    main()
