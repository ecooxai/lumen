"""Derive exact process-local DNS and certificate overrides from upstream config."""
import datetime, hashlib, json, pathlib, subprocess

ROOT=pathlib.Path.cwd();BASE=ROOT/'output/wpt-10290244';UPSTREAM=BASE/'upstream'
readiness=BASE/'fixture-readiness.json';fixture=json.loads(readiness.read_text())
if not fixture.get('fixture_ready') or fixture.get('error'):raise RuntimeError('Official fixture is not ready')
identity=subprocess.check_output(['ps','-p',str(fixture['server_pid']),'-o','lstart='],text=True).strip()
if identity!=fixture['server_start_identity']:raise RuntimeError('WPT server identity changed')
python=UPSTREAM/'_venv3/bin/python'
code='''import json, logging, sys
from tools import localpaths
from tools.serve.serve import build_config
with build_config(logging.getLogger('lume-fixture'), config_path=sys.argv[1]) as config:
    print(json.dumps({'domains':sorted(config.domains_set),'nonexistent_domains':sorted(config.not_domains_set),'ports':config.ports,'browser_host':config.browser_host,'server_host':config.server_host}))
'''
result=subprocess.run([str(python),'-c',code,str(BASE/'server.config.json')],cwd=UPSTREAM,check=True,capture_output=True,text=True,timeout=15)
effective=json.loads(result.stdout)
domains=effective['domains'];nonexistent=effective['nonexistent_domains']
if not domains or not all(name.isascii() and ',' not in name and '=' not in name for name in domains):raise RuntimeError('Invalid authoritative DNS inventory')
if set(domains)&set(nonexistent):raise RuntimeError('Nonexistent WPT host would be mapped')
if effective['server_host']!='127.0.0.1' or effective['browser_host']!='web-platform.test':raise RuntimeError('Unexpected upstream server identity')
mapping=','.join(name+'=127.0.0.1' for name in domains)
rules=', '.join('MAP '+name+' 127.0.0.1' for name in domains)
value={'schema':1,'createdUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'fixtureManifestSHA256':hashlib.sha256(readiness.read_bytes()).hexdigest(),'hostMapImplementationSHA256':hashlib.sha256((ROOT/'src/net/http.c').read_bytes()).hexdigest(),'validDomains':domains,'unmappedNonexistentDomains':nonexistent,'nativeEnvironment':{'LUMEN_HOST_MAP':mapping,'LUMEN_HOST_MAP_MODE':'exact','SSL_CERT_FILE':str(UPSTREAM/'tools/certs/cacert.pem')},'chromiumArguments':['--host-resolver-rules='+rules,'--ignore-certificate-errors-spki-list='+fixture['certificate_spki_sha256_base64']],'origins':{'http':fixture['origin'],'https':fixture['secure_origin'],'h2':fixture['h2_origin']},'scope':'Per-process exact host mapping and exact test certificate SPKI; no global DNS/trust changes or broad TLS bypass.'}
target=BASE/'browser-fixture-environment.json'
if target.exists() and json.loads(target.read_text()).get('fixtureManifestSHA256')!=value['fixtureManifestSHA256']:raise RuntimeError('Existing browser environment belongs to another fixture; preserve it before creating a new cohort')
target.write_text(json.dumps(value,indent=2)+'\n')
print(json.dumps({'output':str(target.relative_to(ROOT)),'validDomains':len(domains),'unmappedNonexistentDomains':len(nonexistent),'nativeHostMapMode':'exact','origins':value['origins'],'fixtureManifestSHA256':value['fixtureManifestSHA256']},indent=2))
