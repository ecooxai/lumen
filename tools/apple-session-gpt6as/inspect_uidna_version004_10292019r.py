"""Bounded exact-library IDNA version/error diagnostic; product binary unchanged."""
from pathlib import Path
import json,subprocess,sys
root=Path.cwd().resolve()
if str(root)!='/Users/devin/repos/lumen':raise ValueError('Unexpected workspace')
out=root/'out/10292019r-mac/uidna-version-004';out.mkdir(exist_ok=False)
payload=r'''from pathlib import Path
from datetime import datetime,timezone
import ctypes,hashlib,json,subprocess
root=Path.cwd();out=root/'out/10292019r-mac/uidna-version-004';build=json.loads((root/'out/10292019r-mac/native-worker-004/summary.json').read_text());binary=root/build['artifacts']['lumen']['path'];sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
if build['sourceBaseCommit']!='dc7a137d79551df3f5d3dc5cc914d06f0b7be468'or sha(binary)!='f906769431c7a25b28ba53a364ffb5027ca0dd04fa0f8accc1161407203d3b95':raise ValueError('Product identity changed')
libpath=Path('/opt/homebrew/Cellar/icu4c@78/78.3/lib/libicuuc.78.3.dylib');expected='a78b3424a391c7afad52c0d69df9cdb7818b6c20f909a14d193ae0c66a5c1119'
if sha(libpath)!=expected:raise ValueError('Accepted dependency identity differs')
links=subprocess.check_output(['otool','-L',str(binary)],text=True,timeout=10);imports=subprocess.check_output(['nm','-m','-u',str(binary)],text=True,timeout=10);symbol_lines=[l for l in imports.splitlines()if 'uidna_'in l]
if '/opt/homebrew/opt/icu4c@78/lib/libicuuc.78.dylib'not in links or not any('uidna_nameToASCII_UTF8_78'in l for l in symbol_lines):raise ValueError('Native UIDNA linkage not proven')
lib=ctypes.CDLL(str(libpath));version=(ctypes.c_uint8*4)();unicode=(ctypes.c_uint8*4)();lib.u_getVersion_78(version);lib.u_getUnicodeVersion_78(unicode)
class Info(ctypes.Structure):_fields_=[('size',ctypes.c_int16),('different',ctypes.c_uint8),('reservedB',ctypes.c_uint8),('errors',ctypes.c_uint32),('reservedI2',ctypes.c_int32),('reservedI3',ctypes.c_int32)]
assert ctypes.sizeof(Info)==16
op=lib.uidna_openUTS46_78;op.argtypes=[ctypes.c_uint32,ctypes.POINTER(ctypes.c_int32)];op.restype=ctypes.c_void_p
convert=lib.uidna_nameToASCII_UTF8_78;convert.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_int32,ctypes.c_void_p,ctypes.c_int32,ctypes.POINTER(Info),ctypes.POINTER(ctypes.c_int32)];convert.restype=ctypes.c_int32
close=lib.uidna_close_78;close.argtypes=[ctypes.c_void_p];err=ctypes.c_int32(0);handle=op(60,ctypes.byref(err))
if not handle or err.value>0:raise ValueError('UIDNA open failed')
rows=[]
try:
 for a in json.loads((root/'out/10292019r-mac/final-url-failures-004.json').read_text())['rows'][0]['failedAssertions']:
  domain=a['name'][len('ToASCII("'):-2];data=domain.encode('utf-8');buffer=ctypes.create_string_buffer(2048);info=Info();info.size=ctypes.sizeof(Info);status=ctypes.c_int32(0);n=convert(handle,data,len(data),buffer,len(buffer),ctypes.byref(info),ctypes.byref(status));rows.append({'name':a['name'],'UErrorCode':status.value,'UIDNAInfoErrors':info.errors,'UIDNAInfoErrorsHex':hex(info.errors),'returnedBytes':n,'partialOutput':buffer.raw[:max(0,min(n,len(buffer)))].decode('utf-8','replace')})
finally:close(handle)
header=(Path('/opt/homebrew/opt/icu4c@78/include/unicode/uidna.h')).read_text();errorDefinition=[x.strip()for x in header.splitlines()if 'UIDNA_ERROR_DISALLOWED='in x.replace(' ','')]
record={'recordedUTC':datetime.now(timezone.utc).isoformat(),'productSource':build['sourceBaseCommit'],'productSHA256':sha(binary),'library':str(libpath),'librarySHA256':expected,'nativeUIDNAImports':symbol_lines,'ICUVersion':list(version),'UnicodeDataVersion':list(unicode),'sourceUTS46Options':60,'disallowedHeaderDefinition':errorDefinition,'cases':rows,'scope':'Exact accepted library queried in a separate bounded diagnostic process; no product rebuild, library replacement or URL validation change.'};(out/'receipt.json').write_text(json.dumps(record,ensure_ascii=True,indent=2)+'\n')
'''
(out/'probe.py').write_text(payload)
with (out/'wrapper.log').open('wb')as log:
 p=subprocess.run([sys.executable,str(root/'tools/monitor_process.py'),'--duration','20','--settle','0','--output',str(out/'probe'),'--',sys.executable,str(out/'probe.py')],stdout=log,stderr=subprocess.STDOUT,timeout=30)
if p.returncode:raise ValueError('Read-only probe failed; preserve wrapper log')
r=json.loads((out/'receipt.json').read_text());print(json.dumps({'ICUVersion':r['ICUVersion'],'UnicodeDataVersion':r['UnicodeDataVersion'],'errors':[x['UIDNAInfoErrorsHex']for x in r['cases']],'librarySHA256':r['librarySHA256'],'receipt':str((out/'receipt.json').relative_to(root))},separators=(',',':')))
