"""Exercise native SDL capture -> MediaStream -> WebAudio -> analyser.

Input is an explicit generated PCM fixture, never a physical microphone.
"""
import argparse
import array
import functools
import http.server
import json
import math
import os
from pathlib import Path
import subprocess
import threading


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--jsrun',default='build/jsrun')
    parser.add_argument('--output',default='out/native-verification')
    args=parser.parse_args()
    output=Path(args.output);output.mkdir(parents=True,exist_ok=True)
    raw=output/'microphone-440hz-f32.raw'
    signal=array.array('f',(0.25*math.sin(2*math.pi*440*i/48000) for i in range(48000*4)))
    if __import__('sys').byteorder!='little':signal.byteswap()
    raw.write_bytes(signal.tobytes())
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Quiet,directory='tests/js'))
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    env=os.environ.copy();env.update({'SDL_AUDIO_DRIVER':'disk','SDL_AUDIO_DISK_INPUT_FILE':str(raw.resolve()),'LUMEN_PERMISSIONS':'grant','LUMEN_CONSOLE':'1','JSRUN_UNTIL_ATTR':'data-result'})
    try:
        proc=subprocess.run([args.jsrun,f'http://127.0.0.1:{server.server_port}/audio-capture-source.html','3500'],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=12)
        (output/'native-microphone-fixture.log').write_text(proc.stdout)
        ok=proc.returncode==0 and 'RESULTS native-microphone-fixture PASS' in proc.stdout
        result={'pass':ok,'processExit':proc.returncode,'physicalMicrophoneTested':False,'input':'generated 440 Hz Float32 PCM through SDL disk recording driver'}
        (output/'native-microphone-fixture.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
        if not ok:print(proc.stdout[-3500:])
        return 0 if ok else 1
    finally:server.shutdown();server.server_close()


if __name__=='__main__':raise SystemExit(main())
