#!/usr/bin/env python3
"""Preserve WebGL interface shape without creating a graphics context.

The native host must complete and exit zero. Bounded console records are then
checked for completeness, unique property identity, exact values/flags/function
metadata, and original own-property ordering. Never accept only a PASS string.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',required=True,type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parent
    expected=json.loads((root/'data/webgl-interface-shape.json').read_text())
    env=dict(os.environ)
    for key in ['LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_PRE_FILE']:env.pop(key,None)
    env.update(LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-gl-interface-shape',JSRUN_EXPECT_ATTR_VALUE='PASS')
    try:
        result=subprocess.run([str(args.binary.resolve()),str(root/'js/webgl-interface-shape.html'),'2000'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=20)
    except subprocess.TimeoutExpired:
        print('WebGL interface fixture did not complete normal teardown.',file=sys.stderr);return 1
    print('WEBGL_SHAPE_NATIVE_EXIT',result.returncode)
    if result.returncode:
        print(result.stdout[-6000:],file=sys.stderr);return result.returncode if result.returncode>0 else 1
    rows=[];counts=[];implementation=[]
    try:
        for line in result.stdout.splitlines():
            if line.startswith('[js log] GL_INTERFACE_ROW '):rows.append(json.loads(line.split('GL_INTERFACE_ROW ',1)[1]))
            elif line.startswith('[js log] GL_INTERFACE_COUNT '):counts.append(json.loads(line.split('GL_INTERFACE_COUNT ',1)[1]))
            elif line.startswith('[js log] GL_CONSTRUCTOR_IMPLEMENTATION '):implementation.append(json.loads(line.split('GL_CONSTRUCTOR_IMPLEMENTATION ',1)[1]))
    except json.JSONDecodeError as error:
        print('Incomplete/malformed native shape record:',error,file=sys.stderr);return 1
    identities=[(r['name'],r['group'],r.get('key')) for r in rows]
    complete=(len(counts)==1 and counts[0]['rows']==len(rows)==expected['property_records'] and len(set(identities))==len(rows))
    mismatches=[{'index':i,'expected':a,'actual':b} for i,(a,b) in enumerate(zip(expected['records'],rows)) if a!=b]
    success=complete and rows==expected['records']
    print('WEBGL_SHAPE_RECEIPT '+json.dumps({'complete_unique_records':complete,'records':len(rows),'expected_records':expected['property_records'],'mismatched_records':len(mismatches),'constructor_implementation':implementation,'pass':success}))
    if not success:print(json.dumps(mismatches[:12],indent=2),file=sys.stderr)
    return 0 if success else 1

if __name__=='__main__':raise SystemExit(main())
