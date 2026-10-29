"""Read-only, credential-redacted project Git delivery configuration."""
from datetime import datetime, timezone
from pathlib import Path
import json
import re
import subprocess
from urllib.parse import urlsplit

root = Path.cwd().resolve()
if str(root) != '/Users/devin/repos/lumen':
    raise SystemExit('Use the observed canonical Mac repository')

def git(*arguments):
    result = subprocess.run(['git', *arguments], capture_output=True, text=True, timeout=10)
    return result.returncode, result.stdout.strip()

def describe(value):
    if '://' in value:
        parsed = urlsplit(value)
        return {'transport': parsed.scheme, 'host': parsed.hostname, 'path': parsed.path,
                'embeddedCredentialsPresent': parsed.username is not None or parsed.password is not None,
                'queryOrFragmentPresent': bool(parsed.query or parsed.fragment)}
    match = re.fullmatch(r'[^@]+@([^:]+):(.+)', value)
    if match:
        return {'transport': 'ssh', 'host': match.group(1), 'path': match.group(2),
                'embeddedCredentialsPresent': False, 'queryOrFragmentPresent': False}
    return {'transport': 'local-or-unrecognized', 'locationPrinted': False}

_, branch = git('branch', '--show-current')
_, head = git('rev-parse', 'HEAD')
_, names = git('remote')
remotes = []
for name in names.splitlines():
    item = {'name': name}
    for label, flags in [('fetch', []), ('push', ['--push'])]:
        code, urls = git('remote', 'get-url', '--all', *flags, name)
        item[label] = [describe(url) for url in urls.splitlines()] if code == 0 else []
    remotes.append(item)
_, upstream = git('rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{upstream}')
helper_code, helper = git('config', '--get', 'credential.helper')
report = {'startedUTC': datetime.now(timezone.utc).isoformat(), 'scope': 'Read-only local Git configuration; no network, authentication probe, push, or credential change',
    'branch': branch, 'head': head, 'upstream': upstream or None, 'remotes': remotes,
    'credentialHelperConfigured': helper_code == 0 and bool(helper),
    'previousAutomaticPushRejectionStillRequiresResolution': True,
    'pushAttempted': False, 'credentialValuesPrinted': False}
print(json.dumps(report, indent=2))
