#!/usr/bin/env python3
"""Apply one guarded getUserMedia selection fix without rewriting other work."""
import difflib
import hashlib
import json
from pathlib import Path

relative = Path('src/js/prelude/08-window.js')
target = Path.cwd() / relative
source = target.read_text()
expected_sha256 = 'e7e7d973df4378c907be2a74f43536351f37a9863cfa74fedb2bb19bfdc022f5'
anchor = "    const cameraReady = id => new Promise((resolve, reject) => {\n"
helper = """    /* ConstrainDOMString: bare values are preferences, exact values filter
     * devices, and empty lists impose no restriction (SelectSettings). */
    const selectCamera = (devices, constraint) => {
        const list = value => value === undefined ? [] : (Array.isArray(value) ? value : [value]).map(String);
        const parameters = constraint !== null && typeof constraint === 'object' && !Array.isArray(constraint);
        const exact = parameters ? list(constraint.exact) : [];
        const preferred = parameters ? list(constraint.ideal) : list(constraint);
        let candidates = devices.map((_, index) => index);
        if (exact.length) candidates = candidates.filter(index => exact.includes('cam' + index));
        if (!candidates.length) {
            const error = new DOMException('No camera satisfies the requested deviceId', 'OverconstrainedError');
            Object.defineProperty(error, 'constraint', { value: 'deviceId', enumerable: true });
            throw error;
        }
        for (const id of preferred) {
            const index = candidates.find(index => 'cam' + index === id);
            if (index !== undefined) return index;
        }
        return candidates[0];
    };
"""
old = """            if (c.video) { const cams = N.capCameras(); if (!cams.length) throw NF('camera'); if (!ask('camera')) throw NA();
                const v = typeof c.video === 'object' ? c.video : {}; let idx = 0; const did = ideal(v.deviceId); if (typeof did === 'string' && /^cam\\d+$/.test(did)) idx = +did.slice(3);
"""
new = """            if (c.video) { const cams = N.capCameras(); if (!cams.length) throw NF('camera');
                const v = typeof c.video === 'object' ? c.video : {}; const idx = selectCamera(cams, v.deviceId);
                if (!ask('camera')) throw NA();
"""
if helper in source and new in source:
    print(json.dumps({'status': 'already-applied', 'path': str(relative)}))
    raise SystemExit(0)
if hashlib.sha256(source.encode()).hexdigest() != expected_sha256:
    raise SystemExit('Source changed since verified snapshot; no changes made')
if source.count(anchor) != 1 or source.count(old) != 1:
    raise SystemExit('Expected unique capture implementation does not match; no changes made')
updated = source.replace(anchor, helper + anchor, 1).replace(old, new, 1)
evidence = Path('output/qa-apple-gpt6/native-capture-selection')
evidence.mkdir(parents=True, exist_ok=True)
(evidence / '08-window.js.before').write_text(source)
(evidence / 'session.patch').write_text(''.join(difflib.unified_diff(
    source.splitlines(keepends=True), updated.splitlines(keepends=True),
    fromfile='a/' + str(relative), tofile='b/' + str(relative), n=3)))
target.write_text(updated)
print(json.dumps({'status': 'applied', 'path': str(relative),
                  'sha256_before': hashlib.sha256(source.encode()).hexdigest(),
                  'sha256_after': hashlib.sha256(updated.encode()).hexdigest(),
                  'delta': str(evidence / 'session.patch')}))
