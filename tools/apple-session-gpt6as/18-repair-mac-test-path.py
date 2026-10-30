from pathlib import Path
import hashlib,json,subprocess,sys
source=Path('tools/mkdevapp.py');test=Path('tests/test_mkdevapp.py')
if hashlib.sha256(source.read_bytes()).hexdigest()!='cdb6080a912ca7056e8c07666dd6c5ad96511a4dd0744cf7208410660b2ab6b4':raise SystemExit('Reviewed production packaging candidate differs')
if hashlib.sha256(test.read_bytes()).hexdigest()!='937638a9c0526221f1ac932ba993e01b67236bd6c3b9504ee4480a621e643c36':raise SystemExit('Regression source differs; inspect before path repair')
if subprocess.check_output(['git','diff','--cached','--name-only'],text=True).strip():raise SystemExit('Index contains another owner change')
old="        self.root = Path(self.temporary.name) / 'project'"
new="        self.root = (Path(self.temporary.name) / 'project').resolve()"
text=test.read_text()
if text.count(old)!=1:raise SystemExit('Unique test-path anchor differs')
test.write_text(text.replace(old,new,1))
out=Path('output/session-20261029/native-bundle-provenance/path-normalized-green')
p=subprocess.run([sys.executable,'tools/monitor_process.py','--duration','30','--settle','0','--output',str(out),'--',sys.executable,str(test),'-v'],capture_output=True,text=True,timeout=45)
out.with_suffix('.collector.log').write_text(p.stdout+p.stderr)
metrics=json.loads(out.with_suffix('.json').read_text());log=out.with_suffix('.log').read_text()
if metrics.get('natural_exit_code')!=0 or metrics.get('collector_error') or 'Ran 7 tests' not in log:raise SystemExit('Normalized-path packaging regressions failed')
subprocess.run(['git','diff','--check','--',str(source),str(test)],check=True)
subprocess.run(['git','add','--',str(source),str(test)],check=True)
subprocess.run(['git','commit','-m','fix: package the exact tested macOS executable with hash provenance'],check=True)
receipt={'commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'test_sha256':hashlib.sha256(test.read_bytes()).hexdigest(),'passed':7,'total':7,'scope':'Portable packaging provenance regressions with mocked signing; actual Mac bundle gate is separate','test_repair':'Resolve macOS /var to /private/var fixture aliases before expected-path comparisons; production unchanged'}
out.with_suffix('.receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
