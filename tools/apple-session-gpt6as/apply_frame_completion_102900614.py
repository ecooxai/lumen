"""Guarded task-queue correction for child document completion; no source writes before RED."""
from pathlib import Path
import argparse, datetime, hashlib, json, subprocess
p=argparse.ArgumentParser();p.add_argument('--red-prefix',type=Path,required=True);p.add_argument('--output',type=Path,default=Path('output/session-102900614/frame-completion-candidate-1'));a=p.parse_args()
root=Path.cwd();source=root/'src/js/js.cc';sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
expected='6d3b89ce2960532dff22c5a888385a69152de26cff1cd8e1f2cd09099ce18d26';fixture=root/'tests/test_frame_load_tasks.py';fixture_sha='64edfda6febcfe6d4c634faeae0587024a4b5df57fdbd79746e2ed08f2694ed8'
if sha(source)!=expected or sha(fixture)!=fixture_sha:raise RuntimeError('Frame source or frozen fixture changed')
redlog=a.red_prefix.with_suffix('.log');redmonitor=a.red_prefix.with_suffix('.json');red=json.loads(redmonitor.read_text())
if 'RESULTS frame-load-tasks FAIL (2/7 checks)'not in redlog.read_text():raise RuntimeError('Expected five failing completion cases were not established')
if red.get('natural_exit_code')!=1 or red.get('collector_error') or red.get('ended_by_monitor'):raise RuntimeError('RED fixture infrastructure did not complete naturally')
text=source.read_text();needle='''    frames_scan(c);
    finish_document_load(c);
}

double js_next_deadline(JsCtx *c) {'''
replacement='''    frames_scan(c);
    /* A child document's completion is a task, not a callback nested inside
     * appendChild/srcdoc processing. Keep the blocker until js_tick finishes
     * the child, after its current script and microtask checkpoint. Initial
     * about:blank insertion has explicit synchronous iframe-load semantics. */
    if (c->parent && c->doc->url && strcmp(c->doc->url, "about:blank")) {
        if (js_wakeup) js_wakeup();
        return;
    }
    finish_document_load(c);
}

double js_next_deadline(JsCtx *c) {'''
if text.count(needle)!=1:raise RuntimeError('Completion source anchor changed')
text=text.replace(needle,replacement)
needle='''double js_next_deadline(JsCtx *c) {
    if (!c) return INFINITY;'''
replacement='''double js_next_deadline(JsCtx *c) {
    if (!c || c->dead || js_window_is_closed(c->window_id)) return INFINITY;'''
if text.count(needle)!=1:raise RuntimeError('Deadline lifecycle guard changed')
text=text.replace(needle,replacement)
needle='''    double d = INFINITY;
    // V8's asynchronous compiler'''
replacement='''    double d = INFINITY;
    /* Wake only while a completion task is runnable. A parent blocked on an
     * unfinished child or network request must not create an idle busy loop. */
    if (c->document_load_pending && !frames_loading(c)) d = now_ms();
    // V8's asynchronous compiler'''
if text.count(needle)!=1:raise RuntimeError('Completion deadline anchor changed')
text=text.replace(needle,replacement)
if any(line.endswith((' ','\t'))for line in text.splitlines()):raise RuntimeError('Unexpected authored whitespace')
a.output.mkdir(parents=True,exist_ok=False)
receipt={'schema':1,'taskId':'102900614','startedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'sourceBaseCommit':subprocess.check_output(['git','--no-pager','rev-parse','HEAD'],text=True).strip(),'before':{'src/js/js.cc':expected},'after':{'src/js/js.cc':hashlib.sha256(text.encode()).hexdigest()},'fixture':'tests/test_frame_load_tasks.py','fixtureSHA256':fixture_sha,'red':{'passed':2,'total':7,'failed':5,'log':str(redlog),'logSHA256':sha(redlog),'monitor':str(redmonitor),'monitorSHA256':sha(redmonitor)},'scope':'Child non-about:blank document completion is delivered on the host event loop, with existing removal/replacement/parent blockers. Initial blank insertion stays synchronous. This does not implement fully asynchronous srcdoc navigation/parsing.'}
source.write_text(text);receipt['finishedUTC']=datetime.datetime.now(datetime.timezone.utc).isoformat();(a.output/'patch-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
