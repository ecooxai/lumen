#!/usr/bin/env python3
"""Prepare only public aggregate status and the audited accepted-source archive."""
import datetime,hashlib,html,json,pathlib,shutil,subprocess,os,stat
ROOT=pathlib.Path(__file__).resolve().parent
RUNTIME=ROOT/'runtime'
DEST=RUNTIME/'lumen-linux-gpt-6-astra-builtin-agents-rubywebterm-pages'
LIVE='https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz'
def esc(value):return html.escape(str(value),quote=True)
def main():
    status=json.loads((ROOT/'status.json').read_text())
    archive=RUNTIME/'src.tar.gz'
    provenance=json.loads((RUNTIME/'src.tar.manifest.json').read_text())
    archive_bytes=archive.read_bytes()
    digest=hashlib.sha256(archive_bytes).hexdigest()
    if digest!=provenance['sha256'] or archive.stat().st_size!=provenance['bytes']:
        raise RuntimeError('Accepted source archive does not match its reviewed manifest')
    runtime_commit=status.get('checkpoint','')
    subprocess.run(['git','merge-base','--is-ancestor',runtime_commit,provenance['commit']],cwd=ROOT.parents[1],check=True,stdout=subprocess.DEVNULL)
    changed=subprocess.check_output(['git','diff','--name-only',runtime_commit,provenance['commit']],cwd=ROOT.parents[1],text=True).splitlines()
    docs={'AGENTS.md','devsummary.md','agent/task.md'}
    if any(name not in docs and not name.startswith(('agent/progress/','agent/iteration-records/')) for name in changed):
        raise RuntimeError('Archive changes runtime/build inputs beyond the accepted runtime checkpoint')
    trees={key:{label:subprocess.check_output(['git','rev-parse',commit+':'+path],cwd=ROOT.parents[1],text=True).strip() for label,commit in [('runtime',runtime_commit),('archive',provenance['commit'])]} for key,path in [('source_tree','src'),('cmake_blob','CMakeLists.txt')]}
    if any(pair['runtime']!=pair['archive'] for pair in trees.values()):
        raise RuntimeError('Compiled source/build-input Git objects differ')
    video=RUNTIME/'lumen-linux-gpt-6-astra-builtin-agents-rubywebterm-showcase.mp4'
    video_meta_path=RUNTIME/'showcase-manifest.json'
    video_meta=json.loads(video_meta_path.read_text()) if video_meta_path.exists() else None
    reviewed_video=bool(video_meta and video_meta.get('reviewed_publication') is True)
    video_bytes=None
    if reviewed_video:
        size=video_meta.get('bytes')
        if not isinstance(size,int) or isinstance(size,bool) or not 0<size<=64*1024*1024:
            raise RuntimeError('Reviewed showcase size is invalid')
        fd=os.open(video,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
        with os.fdopen(fd,'rb') as stream:
            info=os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size!=size:
                raise RuntimeError('Reviewed showcase must be an exact regular file')
            video_bytes=stream.read(size+1)
        if video_meta.get('binary_sha256')!=status.get('accepted_binary_sha256') or len(video_bytes)!=size or hashlib.sha256(video_bytes).hexdigest()!=video_meta['sha256']:
            raise RuntimeError('Reviewed showcase does not match the final binary/artifact manifest')
        if not (0 < video_meta.get('duration_seconds',0) < 60) or video_meta.get('width',0)*3!=video_meta.get('height',0)*4:
            raise RuntimeError('Showcase must be under60seconds and4:3')
    DEST.mkdir(exist_ok=True)
    allowed={'index.html','status.json','src.tar.gz','.nojekyll','publication-manifest.json','showcase.mp4'}
    if any(p.name not in allowed or p.is_symlink() for p in DEST.iterdir()):
        raise RuntimeError('Unexpected file in the dedicated publication directory')
    rows=''.join('<tr><th>'+esc(a)+'</th><td>'+esc(b)+'</td></tr>' for a,b in status.get('current_evidence',[]))
    findings=''.join('<li>'+esc(v)+'</li>' for v in status.get('findings',[]))
    now=datetime.datetime.now(datetime.timezone.utc).isoformat()
    page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Lumen Linux · reviewed progress</title>
<style>:root{color-scheme:dark;font:16px/1.6 system-ui,sans-serif;background:#0b1424;color:#e8eef7}*{box-sizing:border-box}body{margin:0}main{max-width:1080px;margin:auto;padding:32px 22px}h1{font-size:clamp(28px,5vw,42px);line-height:1.15;margin:10px 0}h2{font-size:22px}p{color:#bfd0e4}a{color:#9bd2ff}.eyebrow{color:#9bd2ff;letter-spacing:2px;font-size:12px;text-transform:uppercase}.cards{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin:24px 0}.card{border:1px solid #34506e;background:#101f33;border-radius:15px;padding:20px}.number{font-size:34px;font-weight:700}.small{font-size:13px;color:#a6bdd6}.actions{display:flex;flex-wrap:wrap;gap:14px;margin:22px 0}.button{background:#275f99;color:#fff;text-decoration:none;padding:12px 17px;border-radius:8px}table{border-collapse:collapse;width:100%;font-size:14px}th,td{padding:13px 8px;border-bottom:1px solid #304863;text-align:left;vertical-align:top}th{width:32%;color:#b5d6f4}code{overflow-wrap:anywhere;font-size:12px}.notice{border-left:3px solid #69b4ff;padding-left:16px}.credit{background:#000;color:#fff;font-size:22px;font-weight:650;padding:22px;margin-top:30px}li{margin-bottom:10px}@media(max-width:720px){.cards{grid-template-columns:1fr}th,td{display:block;width:100%}th{padding-bottom:0;border:0}main{padding:24px 16px}.credit{font-size:18px}}</style>
<main><div class="eyebrow">Lumen Linux · saved engineering evidence</div><h1>Browser engine progress</h1><p>'''+esc(status['phase'])+'''</p><p class="notice">This page preserves the reviewed aggregate checkpoint. Open the live workspace for two-second CPU, memory, disk and connection telemetry and the public task message board. The saved page remains readable when the live tunnel is unavailable. This static snapshot has no message or telemetry backend; the live link provides those functions while the tunnel is running. Messages remain queued between sessions, and replies require an active agent session.</p>
<div class="actions"><a class="button" href="'''+LIVE+'''" rel="noopener">Open live progress and messages</a><a class="button" href="src.tar.gz" download="lumen-linux-gpt-6-astra-builtin-agents-rubywebterm-src.tar.gz">Download reviewed source + Git bundle</a></div>
<div class="cards"><section class="card"><div class="small">Whole-goal readiness estimate</div><div class="number">'''+esc(status.get('score','Pending'))+'''/100</div><p>Provisional estimate, not a measured WPT pass rate. Target 98/100 remains unproven.</p></section><section class="card"><div class="small">Latest accepted focused iteration</div><div class="number">'''+esc(status.get('iteration_quality','Pending'))+'''/100</div><p>Iteration '''+esc(status.get('iteration','—'))+''' · bounded patch quality, reviewed separately from whole-browser conformance.</p></section><section class="card"><div class="small">Aggregate evidence updated</div><p>'''+esc(status.get('updated_utc','Pending'))+'''</p><div class="small">Page prepared '''+esc(now)+'''</div></section></div>
<h2>Scope and measured evidence</h2><p class="notice">'''+esc(status.get('coverage',''))+'''</p><table>'''+rows+'''</table><h2>Findings and limits</h2><ul>'''+findings+'''</ul><h2>Reproducible accepted source</h2><p>Branch <code>'''+esc(status.get('branch',''))+'''</code></p><p>Accepted runtime commit <code>'''+esc(runtime_commit)+'''</code></p><p>Archive commit (including reviewed handoff files) <code>'''+esc(provenance['commit'])+'''</code></p><p>Source SHA-256 <code>'''+esc(digest)+'''</code></p><p class="small">The archive contains accepted Git sources and a portable Git bundle. Runtime credentials, browser profiles, private messages and untracked experimental data are excluded. Raw test-result archives are not part of this publication.</p><div class="credit">GPT-6 Astra · built-in agents · RubyWebTerm</div></main></html>'''
    badge='<div style="position:fixed;right:12px;bottom:12px;z-index:10000;max-width:calc(100vw - 24px);background:#000;color:#fff;border:1px solid #5883ad;border-radius:8px;padding:8px 12px;font:12px/1.4 system-ui,sans-serif">Iteration '+esc(status.get('iteration','—'))+' · patch '+esc(status.get('iteration_quality','pending'))+'/100 · overall '+esc(status.get('score','pending'))+'/100</div>'
    page=page.replace('</main></html>','</main>'+badge+'</html>')
    if reviewed_video:
        section='<h2>Actual native browser showcase</h2><p>Recorded from the reviewed combined native binary; actual screen capture with model/agent/tool credits.</p><video controls preload="metadata" style="width:100%;max-width:960px;background:#000" src="showcase.mp4"></video><p><a href="showcase.mp4" download="lumen-linux-gpt-6-astra-builtin-agents-rubywebterm-showcase.mp4">Download the 4:3 showcase video</a></p>'
        page=page.replace('<h2>Scope and measured evidence</h2>',section+'<h2>Scope and measured evidence</h2>')
        (DEST/'showcase.mp4').write_bytes(video_bytes)
    elif (DEST/'showcase.mp4').exists():
        (DEST/'showcase.mp4').unlink()
    (DEST/'index.html').write_text(page)
    (DEST/'status.json').write_text(json.dumps(status,indent=2)+'\n')
    (DEST/'.nojekyll').write_text('')
    (DEST/'src.tar.gz').write_bytes(archive_bytes)
    manifest={'prepared_utc':now,'accepted_commit':provenance['commit'],'accepted_runtime_commit':runtime_commit,'compiled_input_identity':trees,'allowed_handoff_only_changes':changed,'frozen_binary_sha256':status.get('accepted_binary_sha256'),'binary_scope':status.get('accepted_binary_scope'),'rebuild_claim':'Source/build-input equality only; no bit-for-bit reproducible rebuild claim. No git describe/rev-parse/GIT_SHA version embedding found in inspected CMake/src/tools.','source_sha256':digest,'live_url':LIVE,'publication_status':'prepared locally; not pushed or enabled','files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in DEST.iterdir() if p.name!='publication-manifest.json'},'showcase':video_meta if reviewed_video else None,'excluded':'Raw-result archive, credentials, profiles, inbox and QA screenshots'}
    (DEST/'publication-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'directory':str(DEST),'accepted_commit':provenance['commit'],'files':sorted(p.name for p in DEST.iterdir()),'bytes':sum(p.stat().st_size for p in DEST.iterdir()),'published':False}))
if __name__=='__main__':main()
