"""Reserve a new source-backup folder through FileHarbor's published public API.

Contract comes from the site's rendered public quickstart. Never retries a
possibly successful reservation, changes access, or requests a bypass token.
"""
import datetime,hashlib,json,os,pathlib,subprocess,urllib.error,urllib.request
ROOT=pathlib.Path.cwd();BASE=ROOT/'output/session-10290244';PUBLIC=BASE/'public'
ORIGIN='https://fileharbor-meak.basicgleam65.chatgpt.site'
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
manifest=json.loads((PUBLIC/'manifest.json').read_text())
if manifest['sourceCommit']!=head:raise RuntimeError('Refresh the reviewed committed source package before reserving a backup')
archive=PUBLIC/manifest['archive']
if archive.stat().st_size!=manifest['bytes']or hashlib.sha256(archive.read_bytes()).hexdigest()!=manifest['sha256']:raise RuntimeError('Immutable source archive differs from manifest')
folder='lume-10290244-gpt6-astra-pro-release-integrator-'+head[:12]
state=BASE/'fileharbor-private'/head;state.mkdir(parents=True,exist_ok=True)
try:state.chmod(0o700)
except OSError:pass
request={'username':'meaklocaldev','name':folder,'files':[{'path':archive.name,'size':archive.stat().st_size},{'path':'source-manifest.json','size':(PUBLIC/'manifest.json').stat().st_size}]}
request_path=state/'reservation-request.json';response_path=state/'reservation-response.json'
def write_private(path,value):
 data=(json.dumps(value,indent=2)+'\n').encode();descriptor=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)
 with os.fdopen(descriptor,'wb')as stream:stream.write(data)
def shape(value):
 if isinstance(value,dict):return {key:shape(item)for key,item in value.items()}
 if isinstance(value,list):return [shape(value[0])]if value else []
 return type(value).__name__
if response_path.exists():
 if json.loads(request_path.read_text())!=request:raise RuntimeError('Existing reservation state describes another source payload')
 response=json.loads(response_path.read_text());print(json.dumps({'existingReservation':True,'sourceCommit':head,'responseSchema':shape(response),'privateResponse':str(response_path.relative_to(ROOT))}));raise SystemExit(0)
if request_path.exists():raise RuntimeError('A previous reservation may have reached the server. Inspect its private receipt before any retry; do not reserve duplicates.')
write_private(request_path,request)
receipt={'schema':1,'startedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'sourceCommit':head,'sourceSHA256':manifest['sha256'],'origin':ORIGIN,'folderName':folder,'operation':'Reserve a new source-backup folder using the public quickstart API','reserved':False}
try:
 req=urllib.request.Request(ORIGIN+'/api/folders',data=json.dumps(request).encode(),headers={'Content-Type':'application/json','Accept':'application/json'},method='POST')
 with urllib.request.urlopen(req,timeout=25)as incoming:
  data=incoming.read(1024*1024+1)
  if len(data)>1024*1024:raise RuntimeError('Unexpectedly large reservation response')
  receipt['httpStatus']=incoming.status;response=json.loads(data)
 write_private(response_path,response);receipt['reserved']=True;receipt['responseSchema']=shape(response)
except urllib.error.HTTPError as error:
 receipt['httpStatus']=error.code;receipt['error']='Public API returned HTTP '+str(error.code)+'; no authentication or alternate route was attempted.'
except Exception as error:receipt['error']=type(error).__name__+': '+str(error)
receipt['finishedUTC']=datetime.datetime.now(datetime.timezone.utc).isoformat()
write_private(state/'reservation-receipt.json',receipt)
print(json.dumps({**receipt,'privateResponse':str(response_path.relative_to(ROOT))if response_path.exists()else None},indent=2))
raise SystemExit(0 if receipt['reserved']else 1)
