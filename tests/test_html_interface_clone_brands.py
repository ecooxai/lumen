#!/usr/bin/env python3
"""Run real V8/DOM HTML-interface clone tests in an existing native jsrun binary."""
import argparse,hashlib,json,os,pathlib,re,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',default='build/jsrun');p.add_argument('--expected-sha256',required=True);a=p.parse_args()
binary=(ROOT/a.binary).resolve();binary.relative_to(ROOT)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
if sha(binary)!=a.expected_sha256:raise SystemExit('Native jsrun binary hash mismatch')
fixture=ROOT/'tests/js/html-interface-clone-brands.html';environment=dict(os.environ,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-clone-brands-done')
try:r=subprocess.run([str(binary),str(fixture),'1000'],cwd=ROOT,env=environment,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=20)
except subprocess.TimeoutExpired:raise SystemExit('Native constructor/clone fixture exceeded20s')
print(r.stdout,end='')
match=re.search(r'RESULTS html-interface-clone-brands (PASS|FAIL) \((\d+)/(\d+) checks\)',r.stdout)
receipt={'binarySHA256':a.expected_sha256,'fixtureSHA256':sha(fixture),'naturalExitCode':r.returncode,'resultFound':bool(match),'scope':'Native V8/DOM interface exports and clone brands; not complete interface semantics'}
if match:receipt.update(passed=int(match[2]),total=int(match[3]))
print('CLONE_BRANDS_RECEIPT '+json.dumps(receipt));sys.exit(0 if r.returncode==0 and match and int(match[2])==132 and int(match[3])==132 else 1)
