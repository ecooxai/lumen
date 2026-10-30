#!/usr/bin/env python3
"""One deterministic reproduction of a sampler append between wallclock and stat."""
import ast,json,types
from pathlib import Path

def verify(source):
 tree=ast.parse(source.read_text());functions=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='heartbeat_age'];assert len(functions)==1
 clock={'now':42.0,'order':[]}
 def walltime():clock['order'].append('clock');return clock['now']
 class UpdatingFile:
  def stat(self):
   clock['order'].append('stat');clock['now']=42.0002
   return types.SimpleNamespace(st_mtime=42.0001)
 path=UpdatingFile();old=walltime()-path.stat().st_mtime
 assert old<0 and clock['order']==['clock','stat'],'Fixture must reproduce the actual race'
 clock.update(now=42.0,order=[])
 ns={'time':types.SimpleNamespace(time=walltime)}
 exec(compile(ast.Module(body=functions,type_ignores=[]),str(source),'exec'),ns)
 corrected=ns['heartbeat_age'](path)
 assert clock['order']==['stat','clock'] and 0<=corrected<6,'Actual corrected reader must retain the strict freshness gate'
 return {'passed':True,'fixtureCount':1,'oldRaceAgeSeconds':old,'correctedAgeSeconds':corrected,'strictBoundsUnchanged':True}
if __name__=='__main__':
 import sys
 print(json.dumps(verify(Path(sys.argv[1]))))
