"""Create separate LF-only derived results; preserve original reports and logs."""
import argparse,collections,datetime,hashlib,json,pathlib
from wpt_console_frames import parse_console_report

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signature(row):return json.dumps(row,ensure_ascii=True,separators=(',',':'))
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--run',required=True,type=pathlib.Path);ap.add_argument('--output',required=True,type=pathlib.Path);a=ap.parse_args()
    a.run=a.run.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
    original=a.run/'results.jsonl';source_rows=[json.loads(l) for l in original.read_text().split('\n') if l.strip()]
    derived=[];audit=[]
    for old in source_rows:
        log=pathlib.Path(old['evidence_dir'])/'browser.log'
        parsed=parse_console_report(log.read_text(errors='replace'))
        new=dict(old)
        if parsed is not None:
            old_count=collections.Counter(signature(s) for s in old['subtests']);new_count=collections.Counter(signature(s) for s in parsed['subtests'])
            if old_count-new_count:raise RuntimeError('Original parsed subtest outcome would be removed: '+old['url'])
            new.update(parsed)
            restored=new_count-old_count
        else:restored=collections.Counter()
        provenance={'original_result_sha256':sha(original),'raw_log':str(log),'raw_log_sha256':sha(log),'derivation':'LF-only JSON record framing; no browser rerun or expectation change'}
        new['collection_derivation']=provenance;derived.append(new)
        audit.append({'url':old['url'],'repeat':old.get('repeat',0),'original_subtests':len(old['subtests']),'derived_subtests':len(new['subtests']),'raw_log_sha256':provenance['raw_log_sha256'],'completed_frame_available':parsed is not None,'restored':[{'subtest':json.loads(k),'occurrences':v} for k,v in restored.items()]})
    with (a.output/'results.jsonl').open('x') as f:
        for r in derived:f.write(json.dumps(r,ensure_ascii=True)+'\n')
    metadata=json.loads((a.run/'metadata.json').read_text());metadata.update(derived_from=str(a.run),derived_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),original_results_sha256=sha(original),collector_source_sha256=sha(pathlib.Path(__file__).with_name('wpt_console_frames.py')),derivation_script_sha256=sha(pathlib.Path(__file__)),no_browser_rerun=True)
    (a.output/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n');(a.output/'derivation-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    summary={'records':len(derived),'original_reported_subtests':sum(len(r['subtests']) for r in source_rows),'derived_reported_subtests':sum(len(r['subtests']) for r in derived),'derived_statuses':dict(collections.Counter(s[1] for r in derived for s in r['subtests'])),'harness_statuses':dict(collections.Counter(r['status'] for r in derived)),'restored_statuses':dict(collections.Counter(s['subtest'][1] for r in audit for s in r['restored'] for _ in range(s['occurrences']))),'interpretation':'Restored records are measurement recovery, not engine gains. Completion-vector expected counts are unavailable in old headers.'}
    (a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
if __name__=='__main__':main()
