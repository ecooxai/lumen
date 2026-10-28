#!/usr/bin/env python3
"""Version2 derived reporting: preserve assertion comparison, separate coverage units."""
import argparse,contextlib,hashlib,io,json,pathlib,runpy,sys,tempfile

def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def testharness_urls(manifest):
    urls=set()
    def walk(node,parts):
        if isinstance(node,dict):
            for name,child in node.items():walk(child,parts+[name])
        elif isinstance(node,list):
            for item in node[1:]:
                if isinstance(item,list) and item:
                    url=item[0] or '/'+ '/'.join(parts)
                    urls.add('/'+str(url).lstrip('/'))
    walk(manifest.get('items',{}).get('testharness',{}),[])
    return urls

def main():
    p=argparse.ArgumentParser(add_help=False)
    p.add_argument('--manifest');p.add_argument('--output',required=True)
    options,rest=p.parse_known_args();out=pathlib.Path(options.output).resolve();out.parent.mkdir(parents=True,exist_ok=True)
    legacy=pathlib.Path.cwd()/'agent/validation/compare_wpt_runs.py';legacy_sha=sha(legacy)
    original_argv=sys.argv[:]
    with tempfile.TemporaryDirectory(prefix='compare-v2-',dir=out.parent) as tmp:
        intermediate=pathlib.Path(tmp)/'legacy-report.json'
        try:
            sys.argv=[str(legacy),*rest,'--output',str(intermediate)]
            with contextlib.redirect_stdout(io.StringIO()):runpy.run_path(str(legacy),run_name='__main__')
        finally:sys.argv=original_argv
        report=json.loads(intermediate.read_text())
    assert sha(legacy)==legacy_sha,'Legacy comparison source changed during derivation'
    report.pop('full_automated_corpus',None);report.pop('fresh_verified_url_coverage_pct',None)
    selected=json.loads(pathlib.Path(report['selection']).read_text());unique=set(selected)
    report['schema_version']=2
    report['units']={'selection':'runnable URL/variant entries','subtests':'assertions identified by URL, full name and duplicate-name occurrence','coverage':'distinct selected testharness URLs divided by all URLs in the pinned testharness manifest category; not a pass rate or proof of complete execution'}
    report['selected_unique_urls']=len(unique)
    report['selected_duplicate_entries']=len(selected)-len(unique)
    report['candidate_fully_passing_selected_url_pct']=round(100*report['summary']['candidate_fully_passing_urls']/len(selected),5) if selected else None
    report['source_hashes']={'legacy_comparer':legacy_sha,'version2_reporter':sha(__file__),'selection':sha(report['selection'])}
    report['input_hashes']={k:sha(v) for k,v in report['inputs'].items() if v}
    if options.manifest:
        path=pathlib.Path(options.manifest);known=testharness_urls(json.loads(path.read_text()));matched=unique & known
        report['selection_coverage']={'category':'testharness','manifest':str(path),'manifest_sha256':sha(path),'manifest_url_variants':len(known),'selected_matching_urls':len(matched),'selected_unmatched_urls':sorted(unique-known),'selected_category_coverage_pct':round(100*len(matched)/len(known),5) if known else None,'interpretation':'Selection coverage only. Execution completeness, harness outcomes and pass rates are reported separately.'}
    else:report['selection_coverage']=None
    report['caveats'].append('Version2 removes the legacy field that divided fully passing selected URLs by a mixed-category automated corpus and called it coverage; raw results and identity comparisons are unchanged.')
    out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'schema_version':2,'selected':len(selected),'summary':report['summary'],'gained':len(report['gained_passes']),'lost':len(report['lost_passes']),'coverage':report['selection_coverage'],'output':str(out)}))
if __name__=='__main__':main()
