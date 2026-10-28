import argparse,json,os,pathlib,subprocess,tempfile,time
from paired_wpt import monitor,endmonitor,full_url
p=argparse.ArgumentParser();p.add_argument('--binary',required=True);p.add_argument('--output',required=True);p.add_argument('--url',required=True);a=p.parse_args()
out=pathlib.Path(a.output).resolve();out.mkdir(exist_ok=True,parents=True)
with tempfile.TemporaryDirectory(prefix='profile-',dir=out) as prof:
 env=dict(os.environ,XDG_DATA_HOME=prof,XDG_CONFIG_HOME=prof,LUMEN_NO_SESSION='1',LUMEN_HOST_MAP='web-platform.test=127.0.0.1,not-web-platform.test=127.0.0.1',SSL_CERT_FILE='/tmp/lumen-wpt/ca.pem',LUMEN_WINDOW='800x640',LUMEN_CONSOLE='2',SDL_VIDEODRIVER='offscreen',LUMEN_NO_GPU='1',LUMEN_SHOT=str(out/'render.png'),LUMEN_SHOT_MS='13000',LUMEN_TESTDRIVER='1')
 t=time.time()
 with (out/'browser.log').open('wb') as f:
  q=subprocess.Popen([os.path.realpath(a.binary),full_url(a.url)],env=env,stdout=f,stderr=f);m=monitor(q.pid,out/'resources','controlled visual fixture')
  try:code=q.wait(25)
  except subprocess.TimeoutExpired:q.terminate();code=q.wait(5)
  endmonitor(m)
 json.dump({'exit_code':code,'elapsed_s':round(time.time()-t,3),'screenshot_exists':(out/'render.png').exists(),'binary':os.path.realpath(a.binary),'url':a.url},open(out/'result.json','w'),indent=2)
