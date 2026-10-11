/* GLSL ES 1.00 / 3.00 -> GLSL 450 (naga) translator used by WebGL. Uniforms are packed into one std140 block,
   samplers split into texture + sampler bindings, attributes/varyings get explicit locations, and clip space
   is flipped (y) and remapped (z) so WebGPU memory layout matches GL's bottom-up conventions. */
const LGLSL = (() => {
    const TOK = /[A-Za-z_]\w*|0[xX][0-9a-fA-F]+[uU]?|\d+\.?\d*(?:[eE][+-]?\d+)?[fFuU]?|\.\d+(?:[eE][+-]?\d+)?[fF]?|##|<<=|>>=|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\^\^|[-+*/%&|^]=|\s+|[\s\S]/g;
    const tokens = s => s.match(TOK) || [];
    const isId = t => /^[A-Za-z_]/.test(t);
    const stripComments = s => s.replace(/\\\r?\n/g, '').replace(/\/\*[\s\S]*?\*\//g, m => m.replace(/[^\n]/g, ' ')).replace(/\/\/[^\n]*/g, '');
    function expand(toks, M, hide) {
        const out = [];
        for (let i = 0; i < toks.length; i++) {
            const t = toks[i], m = isId(t) && !hide.has(t) && M.get(t);
            if (!m) { out.push(t); continue; }
            if (m.params) {
                let j = i + 1; while (j < toks.length && /^\s+$/.test(toks[j])) j++;
                if (toks[j] !== '(') { out.push(t); continue; }
                const args = []; let cur = [], d = 0; j++;
                for (; j < toks.length; j++) { const u = toks[j]; if (u === '(') d++; else if (u === ')') { if (!d) break; d--; } else if (u === ',' && !d) { args.push(cur); cur = []; continue; } cur.push(u); }
                args.push(cur); i = j;
                const ex = args.map(a => expand(a, M, hide)), body = [];
                for (const b of m.body) { const k = m.params.indexOf(b); body.push(...(k >= 0 ? ex[k] || [] : [b])); }
                const h = new Set(hide); h.add(t); out.push(...expand(body, M, h));
            } else { const h = new Set(hide); h.add(t); out.push(...expand(m.body, M, h)); }
        }
        return out;
    }
    function evalIf(expr, M) {
        let t = tokens(expr), r = [];
        for (let i = 0; i < t.length; i++) {
            if (t[i] === 'defined') { let j = i + 1; while (/^\s+$/.test(t[j] || '')) j++; let par = t[j] === '('; if (par) { j++; while (/^\s+$/.test(t[j])) j++; }
                const n = t[j]; if (par) { j++; while (/^\s+$/.test(t[j] || '')) j++; } r.push(M.has(n) ? '1' : '0'); i = j - (par ? 0 : 0); if (!par) i = j; continue; }
            r.push(t[i]);
        }
        r = expand(r, M, new Set()).map(x => isId(x) ? '0' : x.replace(/^(\d+)[uU]$/, '$1'));
        const e = r.join('');
        if (!/^[\d\s+\-*/%<>=!&|^~()?:.xXa-fA-F]*$/.test(e)) return false;
        try { return !!Function('return (' + e + ')')(); } catch { return false; }
    }
    function preprocess(src, defs) {
        const M = new Map(Object.entries(defs || {}).map(([k, v]) => [k, { body: tokens(String(v)) }]));
        M.set('GL_ES', { body: ['1'] }); M.set('GL_FRAGMENT_PRECISION_HIGH', { body: ['1'] });
        const lines = stripComments(src).split('\n'), out = [], st = []; let version = 100, err = '';
        const active = () => st.every(s => s.on);
        for (const line of lines) {
            const m = /^\s*#\s*(\w*)\s*(.*)$/.exec(line);
            if (m) {
                const [, d, rest] = m;
                if (d === 'if' || d === 'ifdef' || d === 'ifndef') { const par = active(); const c = par && (d === 'if' ? evalIf(rest, M) : d === 'ifdef' ? M.has(rest.trim().split(/\s/)[0]) : !M.has(rest.trim().split(/\s/)[0])); st.push({ on: c, taken: c, par }); }
                else if (d === 'elif') { const s = st[st.length - 1]; if (s) { const c = s.par && !s.taken && evalIf(rest, M); s.on = c; s.taken = s.taken || c; } }
                else if (d === 'else') { const s = st[st.length - 1]; if (s) { s.on = s.par && !s.taken; s.taken = true; } }
                else if (d === 'endif') st.pop();
                else if (!active()) {}
                else if (d === 'define') { const dm = /^([A-Za-z_]\w*)(\(([^)]*)\))?\s*(.*)$/.exec(rest); if (dm) M.set(dm[1], { params: dm[2] ? dm[3].split(',').map(x => x.trim()).filter(Boolean) : null, body: tokens(dm[4]) }); }
                else if (d === 'undef') M.delete(rest.trim());
                else if (d === 'version') version = parseInt(rest) || 100;
                else if (d === 'error') err = rest;
                out.push('');
                continue;
            }
            out.push(active() ? expand(tokens(line), M, new Set()).join('') : '');
        }
        return { code: out.join('\n'), version, err };
    }
    const PRIM = {}; // name -> [components, columns, kind]  (kind f/i/u/b)
    for (const [p, k] of [['', 'f'], ['i', 'i'], ['u', 'u'], ['b', 'b']]) { PRIM[k === 'f' ? 'float' : k === 'i' ? 'int' : k === 'u' ? 'uint' : 'bool'] = [1, 1, k]; for (let n = 2; n <= 4; n++) PRIM[p + 'vec' + n] = [n, 1, k]; }
    for (let c = 2; c <= 4; c++) { PRIM['mat' + c] = [c, c, 'f']; for (let r = 2; r <= 4; r++) PRIM['mat' + c + 'x' + r] = [r, c, 'f']; }
    const SAMPLERS = { sampler2D: ['2d', 'f'], samplerCube: ['cube', 'f'], sampler3D: ['3d', 'f'], sampler2DArray: ['2d-array', 'f'], sampler2DShadow: ['2d', 'd'], samplerCubeShadow: ['cube', 'd'], sampler2DArrayShadow: ['2d-array', 'd'],
        isampler2D: ['2d', 'i'], isampler3D: ['3d', 'i'], isamplerCube: ['cube', 'i'], isampler2DArray: ['2d-array', 'i'], usampler2D: ['2d', 'u'], usampler3D: ['3d', 'u'], usamplerCube: ['cube', 'u'], usampler2DArray: ['2d-array', 'u'], samplerExternalOES: ['2d', 'f'] };
    const TEXTYPE = { '2d': 'texture2D', cube: 'textureCube', '3d': 'texture3D', '2d-array': 'texture2DArray' };
    const QUAL = new Set(['highp', 'mediump', 'lowp', 'invariant', 'precise', 'smooth', 'centroid', 'flat', 'noperspective', 'const', 'in', 'out', 'inout', 'attribute', 'varying', 'uniform']);
    const RENAME = { texture2D: 'texture', textureCube: 'texture', texture3D: 'texture', texture2DLod: 'textureLod', texture2DLodEXT: 'textureLod', textureCubeLod: 'textureLod', textureCubeLodEXT: 'textureLod',
        texture2DProj: 'textureProj', texture2DProjEXT: 'textureProj', texture2DProjLod: 'textureProjLod', texture2DProjLodEXT: 'textureProjLod', texture2DGradEXT: 'textureGrad', textureCubeGradEXT: 'textureGrad',
        gl_FragDepthEXT: 'gl_FragDepth', gl_VertexID: 'gl_VertexIndex', gl_InstanceID: 'gl_InstanceIndex', gl_PointSize: '_lps', main: '_lmain' };
    const constEval = (t, consts) => { const e = t.map(x => consts.has(x) ? consts.get(x) : x).join('').replace(/(\d)[uU]\b/g, '$1'); return /^[\d\s+\-*/()]+$/.test(e) ? Function('return (' + e + ')|0')() : NaN; };
    /* split preprocessed tokens into global items: {toks} each ending in ';' or a function body '}' */
    function globals(T) {
        const items = []; let cur = [], d = 0, p = 0, fn = false;
        for (const t of T) {
            cur.push(t);
            if (t === '(') p++; else if (t === ')') p--;
            else if (t === '{') { if (!d && !cur.some(x => x === 'struct' || x === 'uniform' || x === 'buffer') && cur.includes('(')) fn = true; else if (!d && !fn && cur.some(x => x === 'uniform' || x === 'in' || x === 'out') && !cur.includes('struct')) fn = false; d++; }
            else if (t === '}') { d--; if (!d && fn) { items.push({ toks: cur, fn: true }); cur = []; fn = false; } }
            else if (t === ';' && !d && !p) { items.push({ toks: cur }); cur = []; }
        }
        if (cur.some(x => !/^\s+$/.test(x))) items.push({ toks: cur });
        return items;
    }
    const sig = toks => toks.filter(t => !/^\s+$/.test(t));
    /* parse "<quals> type name[N], name2 ;" */
    function decl(s, consts) {
        let i = 0, layout = null; const q = new Set();
        for (;;) { if (s[i] === 'layout') { let j = i + 2, d = 1; const st = j; for (; d; j++) { if (s[j] === '(') d++; else if (s[j] === ')') d--; } layout = s.slice(st, j - 1).join(''); i = j; continue; } if (QUAL.has(s[i])) { q.add(s[i++]); continue; } break; }
        const type = s[i++]; const names = [];
        while (i < s.length && s[i] !== ';') {
            const name = s[i++]; let arr = 0;
            if (s[i] === '[') { let j = i + 1; const st = j; while (s[j] !== ']') j++; arr = constEval(s.slice(st, j), consts); i = j + 1; }
            while (i < s.length && s[i] !== ',' && s[i] !== ';') i++;
            names.push({ name, arr }); if (s[i] === ',') i++;
        }
        return { q, type, names, layout };
    }
    /* std140 layout */
    function layoutOf(type, arr, structs) {
        let al, sz;
        if (PRIM[type]) { const [n, c] = PRIM[type]; if (c > 1) { al = 16; sz = 16 * c; } else { al = n === 1 ? 4 : n === 2 ? 8 : 16; sz = 4 * n; } }
        else if (structs[type]) { const L = structs[type].layout(); al = L.al; sz = L.sz; }
        else throw new Error('unknown uniform type ' + type);
        if (arr) { const stride = Math.ceil(sz / 16) * 16; return { al: 16, sz: stride * arr, stride, elem: sz }; }
        return { al, sz, stride: 0, elem: sz };
    }
    function structDef(s, structs, consts) {
        const name = s[1], members = []; let i = 3;
        while (s[i] !== '}') { const st = i; while (s[i] !== ';') i++; const d = decl(s.slice(st, i + 1), consts); for (const n of d.names) members.push({ type: d.type, name: n.name, arr: n.arr }); i++; }
        let L = null;
        return { name, members, layout() { if (L) return L; let off = 0, al = 16; for (const m of members) { const l = layoutOf(m.type, m.arr, structs); off = Math.ceil(off / l.al) * l.al; m.off = off; m.l = l; off += l.sz; al = Math.max(al, l.al); } return L = { al: Math.max(16, al), sz: Math.ceil(off / 16) * 16 }; } };
    }
    /* leaves for getUniformLocation / getActiveUniform */
    function leaves(name, type, arr, off, structs, out) {
        const l = layoutOf(type, arr, structs);
        if (structs[type]) { const S = structs[type]; S.layout(); const n = arr || 1;
            for (let k = 0; k < n; k++) for (const m of S.members) leaves(name + (arr ? '[' + k + ']' : '') + '.' + m.name, m.type, m.arr, off + k * (arr ? l.stride : 0) + m.off, structs, out); return; }
        out.push({ name: arr ? name + '[0]' : name, base: name, type, size: arr || 1, off, stride: arr ? l.stride : 0 });
    }
    function parse(src, stage, defs) {
        const pp = preprocess(src, defs);
        if (pp.err) throw new Error('#error ' + pp.err);
        const T = tokens(pp.code).filter(t => t !== 'highp' && t !== 'mediump' && t !== 'lowp');
        const items = globals(T), consts = new Map();
        const R = { stage, version: pp.version, structs: {}, structOrder: [], uniforms: [], samplers: [], blocks: [], ins: [], outs: [], body: [], fragData: new Set(), usesFragColor: false };
        for (const it of items) {
            const s = sig(it.toks);
            if (!s.length) continue;
            if (s[0] === 'precision' || (s[0] === 'invariant' && s.length <= 3)) continue;
            if (!it.fn && s[0] === 'struct') { const S = structDef(s, R.structs, consts); R.structs[S.name] = S; R.structOrder.push(it.toks.join('')); continue; }
            if (!it.fn && s[0] === 'const') { const d = decl(s, consts); const eq = s.indexOf('='); if (eq > 0 && d.names.length === 1 && PRIM[d.type] && PRIM[d.type][0] === 1) { const v = constEval(s.slice(eq + 1, -1), consts); if (!isNaN(v)) consts.set(d.names[0].name, String(v)); } R.structOrder.push(it.toks.join('')); continue; }
            if (it.fn) { R.body.push(it.toks); continue; }
            const q = new Set(s.filter(t => QUAL.has(t)));
            const blockIdx = s.indexOf('{');
            if (q.has('uniform') && blockIdx > 0) { /* uniform block */
                const bname = s[blockIdx - 1], end = s.lastIndexOf('}'), inner = s.slice(blockIdx + 1, end), inst = s[end + 1] !== ';' ? s[end + 1] : '';
                const S = structDef(['struct', bname, '{', ...inner, '}'], R.structs, consts); R.blocks.push({ name: bname, inst, S, text: inner.join(' ') }); continue; }
            if (q.has('uniform')) { const d = decl(s, consts);
                for (const n of d.names) { if (SAMPLERS[d.type]) R.samplers.push({ name: n.name, type: d.type, arr: n.arr }); else R.uniforms.push({ name: n.name, type: d.type, arr: n.arr }); } continue; }
            const isIn = q.has('attribute') || (q.has('in') && !q.has('out')), isOut = q.has('out') && !q.has('in');
            if (q.has('varying') || isIn || isOut) { const d = decl(s, consts); const loc = d.layout && /location\s*=\s*(\d+)/.exec(d.layout);
                const dir = q.has('varying') ? (stage === 'v' ? 'out' : 'in') : isIn ? 'in' : 'out';
                for (const n of d.names) (dir === 'in' ? R.ins : R.outs).push({ name: n.name, type: d.type, arr: n.arr, loc: loc ? +loc[1] : -1, flat: q.has('flat'), attr: stage === 'v' && dir === 'in' });
                continue; }
            R.body.push(it.toks);
        }
        for (const b of R.body) for (let i = 0; i < b.length; i++) { if (b[i] === 'gl_FragColor') R.usesFragColor = true; else if (b[i] === 'gl_FragData') { let j = i + 1; while (b[j] !== '[') j++; R.fragData.add(+b[j + 1]); } }
        return R;
    }
    /* link both stages; returns program info + generator for vertex shader variants (attribute kinds) */
    function link(vsrc, fsrc) {
        const V = parse(vsrc, 'v'), F = parse(fsrc, 'f');
        const structs = Object.assign({}, V.structs, F.structs);
        const umap = new Map(); for (const u of [...V.uniforms, ...F.uniforms]) if (!umap.has(u.name)) umap.set(u.name, u);
        const smap = new Map(); for (const u of [...V.samplers, ...F.samplers]) if (!smap.has(u.name)) smap.set(u.name, u);
        /* uniform block layout */
        let off = 0; const members = [], leafs = [];
        for (const u of umap.values()) { const l = layoutOf(u.type, u.arr, structs); off = Math.ceil(off / l.al) * l.al; u.off = off; off += l.sz; members.push(u); leaves(u.name, u.type, u.arr, u.off, structs, leafs); }
        const uboSize = Math.ceil(off / 16) * 16;
        /* samplers -> bindings */
        let bind = 1; const samplers = [];
        for (const s of smap.values()) { const n = s.arr || 1; s.binds = []; for (let k = 0; k < n; k++) { s.binds.push(bind); bind += 2; } samplers.push(s); }
        const blocks = []; const bmap = new Map(); for (const b of [...V.blocks, ...F.blocks]) if (!bmap.has(b.name)) { b.binding = bind++; b.S.layout(); bmap.set(b.name, b); blocks.push(b); }
        /* attributes & varyings */
        const attrs = V.ins.map(a => ({ ...a }));
        const vary = new Map(); let vl = 0; for (const o of V.outs) { const n = PRIM[o.type] && PRIM[o.type][1] > 1 ? PRIM[o.type][1] : 1; vary.set(o.name, vl); vl += n * (o.arr || 1); }
        const fouts = F.outs.length ? F.outs.map((o, i) => ({ name: o.name, loc: o.loc >= 0 ? o.loc : i, type: o.type })) : F.usesFragColor ? [{ name: '_lfc0', loc: 0, type: 'vec4', gl: true }] : [...F.fragData].map(n => ({ name: '_lfc' + n, loc: n, type: 'vec4', gl: true }));
        const boolU = new Set(members.filter(u => PRIM[u.type] && PRIM[u.type][2] === 'b').map(u => u.name));
        const uboType = t => PRIM[t] && PRIM[t][2] === 'b' ? (PRIM[t][0] === 1 ? 'int' : 'ivec' + PRIM[t][0]) : t;
        function header(R) {
            let h = '#version 450\n' + R.structOrderAll.join(';\n').replace(/;\s*;/g, ';') + (R.structOrderAll.length ? ';\n' : '');
            if (uboSize) h += 'layout(std140, set = 0, binding = 0) uniform _LU {\n' + members.map(u => '  ' + uboType(u.type) + ' ' + (boolU.has(u.name) ? '_ub_' : '') + u.name + (u.arr ? '[' + u.arr + ']' : '') + ';').join('\n') + '\n};\n';
            for (const s of samplers) { const [dim, kind] = SAMPLERS[s.type], tp = (kind === 'i' ? 'i' : kind === 'u' ? 'u' : '') + TEXTYPE[dim];
                s.binds.forEach((b, k) => { h += `layout(set = 0, binding = ${b}) uniform ${tp} _lt_${s.name}${k};\nlayout(set = 0, binding = ${b + 1}) uniform sampler${kind === 'd' ? 'Shadow' : ''} _ls_${s.name}${k};\n`; }); }
            for (const b of blocks) h += `layout(std140, set = 0, binding = ${b.binding}) uniform ${b.name} { ${b.text} } ${b.inst};\n`;
            return h;
        }
        function body(R, extra) {
            const out = [];
            for (const toks of R.body) {
                for (let i = 0; i < toks.length; i++) {
                    const t = toks[i];
                    if (!isId(t)) { out.push(t); continue; }
                    let p = out.length - 1; while (p >= 0 && /^\s+$/.test(out[p])) p--; const member = p >= 0 && out[p] === '.';
                    if (member) { out.push(t); continue; }
                    if (smap.has(t)) { const s = smap.get(t), [dim, kind] = SAMPLERS[s.type], comb = (kind === 'i' ? 'i' : kind === 'u' ? 'u' : '') + (dim === '2d' ? 'sampler2D' : dim === 'cube' ? 'samplerCube' : dim === '3d' ? 'sampler3D' : 'sampler2DArray') + (kind === 'd' ? 'Shadow' : '');
                        let k = 0; if (s.arr) { let j = i + 1; while (/^\s+$/.test(toks[j])) j++; if (toks[j] === '[') { let e = j + 1; while (toks[e] !== ']') e++; k = constEval(toks.slice(j + 1, e), new Map()); if (isNaN(k)) throw new Error('sampler array ' + t + ' needs a constant index'); i = e; } }
                        out.push(`${comb}(_lt_${t}${k}, _ls_${t}${k})`); continue; }
                    if (boolU.has(t)) { out.push(`bool(_ub_${t})`); continue; }
                    if (t === 'gl_FragColor') { out.push('_lfc0'); continue; }
                    if (t === 'gl_FragData') { let j = i + 1; while (toks[j] !== '[') j++; let e = j + 1; while (toks[e] !== ']') e++; out.push('_lfc' + toks.slice(j + 1, e).join('').trim()); i = e; continue; }
                    out.push(RENAME[t] || t);
                }
                out.push('\n');
            }
            return out.join('') + (extra || '');
        }
        V.structOrderAll = V.structOrder; F.structOrderAll = F.structOrder;
        const fsHead = header(F) + F.ins.map(v => `layout(location = ${vary.has(v.name) ? vary.get(v.name) : vl++}) ${v.flat ? 'flat ' : ''}in ${v.type} ${v.name}${v.arr ? '[' + v.arr + ']' : ''};\n`).join('') +
            fouts.map(o => `layout(location = ${o.loc}) out ${o.type} ${o.name};\n`).join('') + 'float _lps;\n';
        const fs = fsHead + body(F, '\nvoid main() { _lmain(); }\n');
        /* attribute locations get assigned by the WebGL program at link; kinds decide shader input types */
        function vertex(locs, kinds) {
            let h = header(V), pre = '';
            for (const a of attrs) { const L = locs[a.name]; const P = PRIM[a.type] || [4, 1, 'f']; const [n, c] = P;
                if (c > 1) { for (let k = 0; k < c; k++) h += `layout(location = ${L + k}) in vec${n} _la_${a.name}${k};\n`; h += `${a.type} ${a.name};\n`; pre += `${a.name} = ${a.type}(${Array.from({ length: c }, (_, k) => '_la_' + a.name + k).join(', ')}); `; continue; }
                const want = P[2] === 'b' ? 'f' : P[2], have = kinds[a.name] || 'f';
                if (want === have) { h += `layout(location = ${L}) in ${a.type} ${a.name};\n`; continue; }
                const it = (have === 'i' ? 'i' : have === 'u' ? 'u' : '') + (n === 1 ? (have === 'i' ? 'nt' : have === 'u' ? 'int' : 'float') : 'vec' + n);
                const tname = it === 'int' ? 'int' : it === 'iint' ? 'int' : it === 'uint' ? 'uint' : it === 'nt' ? 'int' : it;
                h += `layout(location = ${L}) in ${tname} _la_${a.name};\n${a.type} ${a.name};\n`; pre += `${a.name} = ${a.type}(_la_${a.name}); `; }
            for (const o of V.outs) h += `layout(location = ${vary.get(o.name)}) ${o.flat ? 'flat ' : ''}out ${o.type} ${o.name}${o.arr ? '[' + o.arr + ']' : ''};\n`;
            h += 'float _lps;\n';
            return h + body(V, `\nvoid main() { ${pre}_lmain(); gl_Position.y = -gl_Position.y; gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5; }\n`);
        }
        return { V, F, attrs, leafs, uboSize, samplers, blocks, fouts, fs, vertex, structs, members };
    }
    return { preprocess, parse, link, PRIM, SAMPLERS, layoutOf };
})();
