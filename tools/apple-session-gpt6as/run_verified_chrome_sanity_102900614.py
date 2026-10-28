"""Launch the unchanged native sanity probe only after full publisher/trust verification."""
from pathlib import Path
import hashlib, json, subprocess

base=Path('output/session-102900614/chrome-official-reference-v2')
receipt=json.loads((base/'receipt.json').read_text())
if not receipt.get('readyForSanityProbe') or not receipt.get('payloadUnchanged') or not receipt.get('quarantineAndOtherAttributesUnchanged'):
    raise RuntimeError('Official reference copy has not passed required verification')
steps={row['name']:row for row in receipt['steps']}
if any(steps[name]['exitCode']!=0 for name in ('copied-deep-strict-final','copied-publisher-final','copied-gatekeeper-final')):
    raise RuntimeError('A required signature/publisher/Gatekeeper step failed')
binary=Path(receipt['binary'])
if hashlib.sha256(binary.read_bytes()).hexdigest()!=receipt['binarySHA256']:
    raise RuntimeError('Verified reference executable changed')
command=['python3','tools/monitor_process.py','--duration','60','--settle','0',
         '--output','output/session-102900614/chrome-native-sanity-1-monitor','--',
         'python3','tools/apple-session-gpt6as/probe_chrome_native_url_10290244.py',
         '--chromium',str(binary),'--expected-sha256',receipt['binarySHA256'],
         '--output','output/session-102900614/chrome-native-sanity-1']
result=subprocess.run(command,timeout=75,check=False)
raise SystemExit(result.returncode)
