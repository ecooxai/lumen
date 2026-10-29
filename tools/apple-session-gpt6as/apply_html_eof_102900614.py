#!/usr/bin/env python3
"""Fix the observed EOF comment SIZE_MAX underflow, preserving source/RED guards."""
import argparse,difflib,hashlib,json,subprocess
from datetime import datetime,timezone
from pathlib import Path
PAYLOAD={'beforeSHA256': '99218b33f6190d865619f233164c7bb81627637b7d021ff39899ee8fa20e9a07', 'afterSHA256': '1863a73e2815a5da5c4e346366a08a3846458538d93e3709aa631d2376e9cf09', 'edits': [[619, 620, '                if (!e) { if (!final) break; e = s + n; }\n'], [623, 624, '                emit(p); i = (size_t)(e - s); if (i < n) ++i; continue;\n'], [630, 631, '            /* EOF is the exclusive end, not the preceding byte. In particular,\n             * the two-byte input "<!" has an empty comment; subtracting from\n             * n - 1 would wrap its length to SIZE_MAX. */\n'], [632, 634, '            if (!e) { if (!final) break; e = s + n; }\n            p->tt = T_COMMENT; sb_put(&p->text, s + i + 2, (size_t)(e - (s + i + 2))); emit(p); i = (size_t)(e - s); if (i < n) ++i; continue;\n'], [635, 636, "        if (d == '?') { const char *e = memchr(s + i, '>', n - i); if (!e) { if (!final) break; e = s + n; } p->tt = T_COMMENT; sb_put(&p->text, s + i + 1, (size_t)(e - (s + i + 1))); emit(p); i = (size_t)(e - s); if (i < n) ++i; continue; }\n"], [641, 642, "            if (end) { const char *e = memchr(s + q, '>', n - q); if (!e) { if (!final) break; e = s + n; } p->tt = T_COMMENT; sb_put(&p->text, s + q, (size_t)(e - (s + q))); emit(p); i = (size_t)(e - s); if (i < n) ++i; continue; }\n"]]}
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=Path('.'));p.add_argument('--output',type=Path,required=True);p.add_argument('--apply',action='store_true');a=p.parse_args()
r=a.source_root.resolve();sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();source=r/'src/html/html.c'
if sha(source)!=PAYLOAD['beforeSHA256']:raise SystemExit('Tokenizer source guard failed')
fixture=r/'tests/test_html_encoding_bounds.c'
if sha(fixture)!='ce828546e389a1af1bdc25e534c79d28189af4f8927086a703f5658895fd0c50':raise SystemExit('Frozen bounds fixture changed')
red=r/'output/session-102900614/html-meta-bounds-2/bounds';monitor=json.loads(red.with_suffix('.json').read_text());log=red.with_suffix('.log').read_text()
if monitor.get('natural_exit_code')!=-6 or monitor.get('ended_by_monitor') or monitor.get('collector_error') or 'AddressSanitizer: memcpy-param-overlap' not in log or 'size 0xffffffffffffffff' not in log:raise SystemExit('Observed sanitizer RED guard failed')
lines=source.read_text().splitlines(True)
for start,end,value in reversed(PAYLOAD['edits']):lines[start:end]=[value]
text=''.join(lines)
if hashlib.sha256(text.encode()).hexdigest()!=PAYLOAD['afterSHA256']:raise SystemExit('Payload guard failed')
out=(r/a.output).resolve();out.mkdir(parents=True,exist_ok=False)
(out/'source.diff').write_text(''.join(difflib.unified_diff(source.read_text().splitlines(True),text.splitlines(True),fromfile='a/src/html/html.c',tofile='b/src/html/html.c')))
if a.apply:source.write_text(text)
receipt={'startedUTC':datetime.now(timezone.utc).isoformat(),'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip(),'applied':a.apply,'before':{'src/html/html.c':PAYLOAD['beforeSHA256']},'after':{'src/html/html.c':PAYLOAD['afterSHA256']},'fixtureSHA256':sha(fixture),'redMonitorSHA256':sha(red.with_suffix('.json')),'redLogSHA256':sha(red.with_suffix('.log')),'scope':'Existing tokenizer EOF endpoint arithmetic; metadata algorithm and frozen fixtures unchanged.'}
(out/'patch-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
