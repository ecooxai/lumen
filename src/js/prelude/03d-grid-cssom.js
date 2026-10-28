/* CSS Grid specified-value grammar and CSSOM shorthand expansion.
 * https://drafts.csswg.org/css-grid-2/#track-sizing
 * https://drafts.csswg.org/css-grid-2/#placement-shorthands
 * Layout and resolved track sizing remain native; this parser never evaluates
 * author code, expands repeat() counts, or substitutes custom properties. */
const CSS_GRID_GROUPS = {
    'grid-row': ['grid-row-start', 'grid-row-end'],
    'grid-column': ['grid-column-start', 'grid-column-end'],
    'grid-area': ['grid-row-start', 'grid-column-start', 'grid-row-end', 'grid-column-end'],
    'grid-template': ['grid-template-rows', 'grid-template-columns', 'grid-template-areas'],
    grid: ['grid-template-rows', 'grid-template-columns', 'grid-template-areas', 'grid-auto-flow', 'grid-auto-rows', 'grid-auto-columns']
};
function cssGridRelevant(p) { return /^(grid|grid-area|grid-row(?:-start|-end)?|grid-column(?:-start|-end)?|grid-template(?:-rows|-columns|-areas)?|grid-auto-(?:rows|columns|flow))$/.test(p); }
function cssGridChildren(p) { return cssGridRelevant(p) ? CSS_GRID_GROUPS[p] || null : null; }
function cssGridWide(v) { return /^(inherit|initial|unset|revert|revert-layer)$/i.test(v); }
function cssGridEscapeEnd(s, i) {
    if (s[i] !== '\\' || i + 1 >= s.length || /[\n\r\f]/.test(s[i + 1])) return -1;
    let j = i + 1, n = 0; while (j < s.length && /[0-9a-f]/i.test(s[j]) && n++ < 6) j++;
    if (j > i + 1) { if (/\s/.test(s[j] || '')) { if (s[j] === '\r' && s[j + 1] === '\n') j++; j++; } return j; }
    return i + 2;
}
function cssGridDecode(s) {
    return s.replace(/\\([0-9a-f]{1,6})(?:\r\n|[ \t\r\n\f])?|\\([^\r\n\f])/gi, (_, hex, ch) => {
        if (ch !== undefined) return ch;
        const n = parseInt(hex, 16); return n && n <= 0x10ffff && !(n >= 0xd800 && n <= 0xdfff) ? String.fromCodePoint(n) : '\ufffd';
    });
}
function cssGridTokens(input) {
    const s = String(input), out = []; let i = 0;
    while (i < s.length) {
        if (/\s/.test(s[i])) { i++; continue; }
        if (s.slice(i, i + 2) === '/*') { const e = s.indexOf('*/', i + 2); if (e < 0) break; i = e + 2; continue; }
        if (s[i] === '/' || s[i] === ',') { out.push(s[i++]); continue; }
        let token = '', stack = [], quote = '', start = i;
        for (; i < s.length; i++) {
            const c = s[i];
            if (c === '\\') { const e = cssGridEscapeEnd(s, i); if (e < 0) return null; token += s.slice(i, e); i = e - 1; continue; }
            if (quote) { if (/[\n\r\f]/.test(c)) return null; token += c; if (c === quote) { quote = ''; if (!stack.length) { i++; break; } } continue; }
            if (c === '"' || c === "'") { if (!stack.length && token) break; quote = c; token += c; continue; }
            if (s.slice(i, i + 2) === '/*') {
                const e = s.indexOf('*/', i + 2); if (e < 0) { if (stack.length) return null; i = s.length; break; }
                if (!stack.length) { i = e + 2; break; } token += ' '; i = e + 1; continue;
            }
            if (c === '(' || c === '[') { if (c === '[' && !stack.length && token) break; stack.push(c); token += c; continue; }
            if (c === ')' || c === ']') { if (stack.pop() !== (c === ')' ? '(' : '[')) return null; token += c; if (!stack.length && c === ']') { i++; break; } continue; }
            if (!stack.length && (/\s/.test(c) || c === '/' || c === ',')) break;
            if (!stack.length && /[{};]/.test(c)) return null;
            token += c;
        }
        if (stack.length || quote || i === start || !token) return null;
        out.push(token);
    }
    return out;
}
function cssGridIdent(s) {
    const esc = '\\\\(?:[0-9a-fA-F]{1,6}[ \\t\\r\\n\\f]?|[^\\r\\n\\f0-9a-fA-F])';
    const ident = new RegExp('^(?:--|-?(?:[_a-zA-Z\\u0080-\\uffff]|' + esc + '))(?:[-_a-zA-Z0-9\\u0080-\\uffff]|' + esc + ')*$');
    return ident.test(s) && !/^(auto|span|initial|inherit|unset|revert|revert-layer|default)$/i.test(cssGridDecode(s));
}
function cssGridIdentText(token) {
    const text = cssGridDecode(token); let out = '';
    for (let i = 0; i < text.length; i++) {
        const c = text[i], n = text.charCodeAt(i);
        if (!n) out += '\ufffd';
        else if (n < 32 || n === 127 || /[0-9]/.test(c) && (i === 0 || i === 1 && text[0] === '-')) out += '\\' + n.toString(16) + ' ';
        else if (i === 0 && c === '-' && text.length === 1) out += '\\-';
        else out += n >= 128 || /[-_a-zA-Z0-9]/.test(c) ? c : '\\' + c;
    }
    return out;
}
function cssGridLineNames(token) {
    if (!token.startsWith('[') || !token.endsWith(']')) return null;
    const names = cssGridTokens(token.slice(1, -1));
    return names && names.every(cssGridIdent) ? names.map(cssGridIdentText).join(' ') : null;
}
function cssGridNumber(token, unitless = false, flexible = false) {
    const m = /^([+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)([a-z]*|%)$/i.exec(token);
    if (!m) return null;
    const n = Number(m[1]), u = m[2].toLowerCase();
    if (!Number.isFinite(n) || n < 0 || !(u === '%' || MATH_UNITS.get(u) === 'length' || flexible && u === 'fr' || !u && n === 0 || unitless && !u)) return null;
    return { text: String(Object.is(n, -0) ? 0 : n) + (u || (unitless ? '' : 'px')), fixed: u !== 'fr', flex: u === 'fr' };
}
function cssGridBreadth(token, flexible = true) {
    const low = token.toLowerCase();
    if (/^(auto|min-content|max-content)$/.test(low)) return { text: low, fixed: false, flex: false };
    const num = cssGridNumber(token, false, flexible); if (num) return num;
    const name = /^([a-z-]+)\(/i.exec(token);
    if (name && MATH_FNS.test(name[1].toLowerCase())) {
        const [type, end] = mathType(token);
        if (end !== token.length || !(type === 'length^1' || type === '%^1')) return null;
        return { text: token.replace(/\s+/g, ' ').replace(/\s*,\s*/g, ', ').trim(), fixed: true, flex: false };
    }
    return null;
}
function cssGridTrack(token) {
    const breadth = cssGridBreadth(token); if (breadth) return breadth;
    let m = /^(minmax|fit-content)\(([\s\S]*)\)$/i.exec(token); if (!m) return null;
    const t = cssGridTokens(m[2]); if (!t) return null;
    if (m[1].toLowerCase() === 'fit-content') {
        if (t.length !== 1) return null; const x = cssGridBreadth(t[0], false);
        return x?.fixed ? { text: 'fit-content(' + x.text + ')', fixed: false, flex: false } : null;
    }
    if (t.length !== 3 || t[1] !== ',') return null;
    const a = cssGridBreadth(t[0], false), b = cssGridBreadth(t[2]);
    return a && b ? { text: 'minmax(' + a.text + ', ' + b.text + ')', fixed: a.fixed || !a.flex && b.fixed, flex: false } : null;
}
function cssGridRepeatCount(token) {
    if (/^[+]?\d+$/.test(token) && Number(token) > 0 && Number.isFinite(Number(token))) return String(Number(token));
    return cssGridIntegerMath(token);
}
function cssGridTrackList(tokens, options = {}) {
    if (!tokens?.length) return null;
    const { implicit = false, noRepeat = false, repeated = false } = options;
    if (!implicit && !repeated && tokens.length === 1 && /^none$/i.test(tokens[0])) return { text: 'none', fixed: false };
    if (!implicit && !repeated && /^subgrid$/i.test(tokens[0])) {
        let auto = 0, out = ['subgrid'];
        for (const token of tokens.slice(1)) {
            const names = cssGridLineNames(token);
            if (names !== null) { out.push('[' + names + ']'); continue; }
            const m = /^repeat\(([\s\S]*)\)$/i.exec(token), t = m && cssGridTokens(m[1]);
            if (!t || t.length < 3 || t[1] !== ',' || !(cssGridRepeatCount(t[0]) !== null || /^auto-fill$/i.test(t[0]))) return null;
            const group = t.slice(2).map(cssGridLineNames); if (group.some(x => x === null)) return null;
            if (/^auto-fill$/i.test(t[0]) && ++auto > 1) return null;
            out.push('repeat(' + (/auto-fill/i.test(t[0]) ? 'auto-fill' : cssGridRepeatCount(t[0])) + ', ' + group.map(x => '[' + x + ']').join(' ') + ')');
        }
        return { text: out.join(' '), fixed: false };
    }
    let count = 0, lastNames = false, allFixed = true, auto = 0, out = [];
    for (const token of tokens) {
        const names = cssGridLineNames(token);
        if (names !== null) { if (implicit || lastNames) return null; lastNames = true; if (names) out.push('[' + names + ']'); continue; }
        lastNames = false;
        const m = /^repeat\(([\s\S]*)\)$/i.exec(token);
        if (m) {
            if (implicit || noRepeat || repeated) return null;
            const t = cssGridTokens(m[1]); if (!t || t.length < 3 || t[1] !== ',') return null;
            const isAuto = /^auto-(fill|fit)$/i.test(t[0]);
            const repeatCount = isAuto ? t[0].toLowerCase() : cssGridRepeatCount(t[0]); if (repeatCount === null) return null;
            if (isAuto && ++auto > 1) return null;
            const inner = cssGridTrackList(t.slice(2), { repeated: true }); if (!inner || isAuto && !inner.fixed) return null;
            allFixed = allFixed && inner.fixed; count++;
            out.push('repeat(' + repeatCount + ', ' + inner.text + ')');
        } else {
            const track = cssGridTrack(token); if (!track) return null;
            allFixed = allFixed && track.fixed; count++; out.push(track.text);
        }
    }
    return count && (!auto || allFixed) ? { text: out.join(' '), fixed: allFixed } : null;
}
function cssGridNumberEval(token) {
    let source = token;
    for (let i = 0; i < 32; i++) {
        const next = source.replace(/\b(min|max|clamp)\(([^()]*)\)/gi, (whole, name, args) => {
            const parts = args.split(',').map(x => mathEval('calc(' + x + ')'));
            if (parts.some(x => !x || x.u) || !parts.length || name.toLowerCase() === 'clamp' && parts.length !== 3) return whole;
            const n = parts.map(x => x.v), value = name.toLowerCase() === 'min' ? Math.min(...n) : name.toLowerCase() === 'max' ? Math.max(...n) : Math.max(n[0], Math.min(n[1], n[2]));
            return Number.isFinite(value) ? String(value) : whole;
        });
        if (next === source) break; source = next;
    }
    return mathEval(source);
}
/* Integer range checks happen after math evaluation, not at parse time.
   Preserve a calculated negative span so computed-value clamping can apply. */
function cssGridIntegerMath(token) {
    const fn = /^([a-z-]+)\(/i.exec(token); if (!fn || !MATH_FNS.test(fn[1].toLowerCase())) return null;
    let [type, end] = mathType(token);
    if (end !== token.length) return null;
    if (type === undefined) {
        const replaced = token.replace(/\bsibling-(?:index|count)\(\)/gi, '1');
        if (replaced === token || mathType(replaced)[0] !== '') return null;
    } else if (type !== '') return null;
    const evaluated = cssGridNumberEval(token); if (evaluated && !evaluated.u) return mathSer(evaluated);
    /* Canonicalize affine number sums, keeping relative-number functions as
       symbols. Other valid dynamic expressions retain their token stream. */
    const inner = /^calc\(([\s\S]*)\)$/i.exec(token), parts = inner && cssGridTokens(inner[1]);
    if (parts && parts.length % 2 === 1) {
        let constant = 0, terms = new SMap(), valid = true;
        for (let i = 0; i < parts.length; i += 2) {
            if (i && !/^[+-]$/.test(parts[i - 1])) { valid = false; break; }
            const sign = i && parts[i - 1] === '-' ? -1 : 1, value = cssGridNumberEval('calc(' + parts[i] + ')');
            if (value && !value.u && Number.isFinite(value.v)) constant += sign * value.v;
            else if (/^sibling-(index|count)\(\)$/i.test(parts[i])) { const name = parts[i].toLowerCase(); terms.set(name, (terms.get(name) || 0) + sign); }
            else { valid = false; break; }
        }
        if (valid) {
            let result = constant ? String(+constant.toPrecision(12)) : '';
            for (const [name, coeff] of terms) if (coeff) {
                const term = (Math.abs(coeff) === 1 ? '' : Math.abs(coeff) + ' * ') + name;
                result += result ? (coeff < 0 ? ' - ' : ' + ') + term : (coeff < 0 ? '-1 * ' : '') + term;
            }
            return 'calc(' + (result || '0') + ')';
        }
    }
    return token.replace(/\s+/g, ' ');
}
function cssGridLine(tokens) {
    if (!tokens?.length || tokens.length > 3) return null;
    if (tokens.length === 1 && /^auto$/i.test(tokens[0])) return 'auto';
    let span = false, integer = null, name = null;
    if (tokens.length === 3 && /^span$/i.test(tokens[1])) return null;
    for (const token of tokens) {
        if (/^span$/i.test(token)) { if (span) return null; span = true; }
        else if (/^[+-]?\d+$/.test(token)) { const n = Number(token); if (integer !== null || !Number.isFinite(n) || n === 0) return null; integer = n; }
        else if (integer === null && cssGridIntegerMath(token) !== null) integer = cssGridIntegerMath(token);
        else if (name === null && cssGridIdent(token)) name = cssGridIdentText(token);
        else return null;
    }
    if (span && (typeof integer === 'number' && integer < 1 || integer === null && name === null)) return null;
    return [span ? 'span' : '', integer === null || span && integer === 1 && name ? '' : String(integer), name || ''].filter(Boolean).join(' ');
}
function cssGridImplicitEnd(v) { return cssGridIdent(v) ? v : 'auto'; }
function cssGridStrings(tokens) {
    if (!tokens?.length) return null;
    let rows = [], width = null, named = new SMap();
    for (const token of tokens) {
        if (!/^("[\s\S]*"|'[\s\S]*')$/.test(token)) return null;
        const value = cssGridDecode(token.slice(1, -1)), cells = value.match(/[.]+|[^.\s]+/g) || [];
        if (!cells.length || width !== null && cells.length !== width) return null;
        width = cells.length;
        for (let x = 0; x < cells.length; x++) {
            let name = cells[x]; if (/^\.+$/.test(name)) { cells[x] = '.'; continue; }
            if (!/^[-_a-zA-Z0-9\u0080-\uffff]+$/.test(name)) return null;
            const box = named.get(name) || [x, rows.length, x, rows.length, 0]; box[0] = Math.min(box[0], x); box[2] = Math.max(box[2], x); box[3] = rows.length; box[4]++; named.set(name, box);
        }
        rows.push(cells);
    }
    for (const b of named.values()) if ((b[2] - b[0] + 1) * (b[3] - b[1] + 1) !== b[4]) return null;
    return rows.map(r => JSON.stringify(r.join(' ')));
}
function cssGridParts(tokens, separator = '/') {
    const parts = [[]]; for (const token of tokens) { if (token === separator) parts.push([]); else parts[parts.length - 1].push(token); } return parts;
}
function cssGridTemplate(tokens) {
    if (tokens.length === 1 && /^none$/i.test(tokens[0])) return ['none', 'none', 'none'];
    const sides = cssGridParts(tokens); if (sides.length > 2 || sides.some(x => !x.length)) return null;
    const hasAreas = sides[0].some(x => /^['"]/.test(x));
    if (!hasAreas) {
        if (sides.length !== 2) return null;
        const a = cssGridTrackList(sides[0]), b = cssGridTrackList(sides[1]); return a && b ? [a.text, b.text, 'none'] : null;
    }
    const columns = sides.length === 2 ? cssGridTrackList(sides[1], { noRepeat: true }) : { text: 'none' };
    if (!columns || columns.text === 'none' && sides.length === 2 || columns.text.startsWith('subgrid')) return null;
    const t = sides[0], rows = [], strings = []; let i = 0, atStart = true;
    while (i < t.length) {
        const before = cssGridLineNames(t[i]);
        if (before !== null) { if (atStart && rows.length) return null; if (before) { if (rows.length && rows[rows.length - 1].startsWith('[')) rows[rows.length - 1] = rows[rows.length - 1].slice(0, -1) + ' ' + before + ']'; else rows.push('[' + before + ']'); } i++; if (i >= t.length) return null; }
        if (!/^['"]/.test(t[i])) return null;
        strings.push(t[i++]); let size = 'auto';
        if (i < t.length && !/^['"\[]/.test(t[i])) { const track = cssGridTrack(t[i++]); if (!track) return null; size = track.text; }
        rows.push(size); atStart = false;
        const after = i < t.length ? cssGridLineNames(t[i]) : null;
        if (after !== null) { if (after) rows.push('[' + after + ']'); i++; }
    }
    const areas = cssGridStrings(strings); return areas ? [rows.join(' '), columns.text, areas.join(' ')] : null;
}
/* Specified-value grammar depends only on the property and input text. Keep
   this separate from declaration merging, var/env substitution and native
   computed/layout values, all of which still run for every relevant mutation.
   Results are internal read-only data. Bound both entry count and retained text
   so a page generating unique declarations cannot grow this cache unbounded. */
const CSS_GRID_PARSE_CACHE = Object.create(null), CSS_GRID_PARSE_KEYS = Object.create(null);
const cssGridFreeze = Object.freeze;
let cssGridParseCacheUnits = 0, cssGridParseCacheHead = 0, cssGridParseCacheCount = 0;
function cssGridParse(p, input) {
    if (!cssGridRelevant(p)) return undefined;
    const raw = String(input).trim(); if (!raw) return null;
    if (raw.length > 512) return cssGridParseUncached(p, raw);
    const key = p + '\0' + raw, cached = CSS_GRID_PARSE_CACHE[key];
    if (cached !== undefined) return cached.parsed;
    const parsed = cssGridParseUncached(p, raw), values = parsed && parsed.values;
    let units = key.length + (parsed ? parsed.value.length : 0);
    if (values) for (let i = 0; i < values.length; i++) units += values[i].length;
    if (units <= 16384) {
        while (cssGridParseCacheCount >= 128 || cssGridParseCacheUnits + units > 16384) {
            const oldest = CSS_GRID_PARSE_KEYS[cssGridParseCacheHead];
            cssGridParseCacheUnits -= CSS_GRID_PARSE_CACHE[oldest].units;
            delete CSS_GRID_PARSE_CACHE[oldest]; delete CSS_GRID_PARSE_KEYS[cssGridParseCacheHead];
            cssGridParseCacheHead = (cssGridParseCacheHead + 1) % 128; cssGridParseCacheCount--;
        }
        if (parsed) { if (values) cssGridFreeze(values); cssGridFreeze(parsed); }
        CSS_GRID_PARSE_CACHE[key] = { parsed, units };
        CSS_GRID_PARSE_KEYS[(cssGridParseCacheHead + cssGridParseCacheCount) % 128] = key;
        cssGridParseCacheCount++; cssGridParseCacheUnits += units;
    }
    return parsed;
}
function cssGridParseUncached(p, raw) {
    const tokens = cssGridTokens(raw); if (!tokens?.length) return null;
    const children = cssGridChildren(p);
    if (tokens.length === 1 && cssGridWide(tokens[0])) { const value = tokens[0].toLowerCase(); return { value, values: children ? children.map(() => value) : null }; }
    if (tokens.some(t => !/^['"]/.test(t) && /\b(?:var|env|attr)\(/i.test(t))) return { value: raw, values: null, pending: true };
    let value, values = null;
    if (/^grid-auto-(rows|columns)$/.test(p)) value = cssGridTrackList(tokens, { implicit: true })?.text;
    else if (/^grid-template-(rows|columns)$/.test(p)) value = cssGridTrackList(tokens)?.text;
    else if (p === 'grid-auto-flow') {
        const low = tokens.map(x => x.toLowerCase()); if (low.length > 2 || new SSet(low).size !== low.length || low.some(x => !/^(row|column|dense)$/.test(x)) || low.includes('row') && low.includes('column')) return null;
        value = low.includes('column') ? 'column' + (low.includes('dense') ? ' dense' : '') : low.includes('dense') ? 'dense' : 'row';
    } else if (/^grid-(row|column)-(start|end)$/.test(p)) value = cssGridLine(tokens);
    else if (p === 'grid-template-areas') value = tokens.length === 1 && /^none$/i.test(tokens[0]) ? 'none' : cssGridStrings(tokens)?.join(' ');
    else if (p === 'grid-row' || p === 'grid-column' || p === 'grid-area') {
        const parts = cssGridParts(tokens), lines = parts.map(cssGridLine); if (lines.some(x => !x) || lines.length > (p === 'grid-area' ? 4 : 2)) return null;
        if (p === 'grid-area') values = [lines[0], lines[1] ?? cssGridImplicitEnd(lines[0]), lines[2] ?? cssGridImplicitEnd(lines[0]), lines[3] ?? cssGridImplicitEnd(lines[1] ?? cssGridImplicitEnd(lines[0]))];
        else values = [lines[0], lines[1] ?? cssGridImplicitEnd(lines[0])];
    } else if (p === 'grid-template') values = cssGridTemplate(tokens);
    else if (p === 'grid') {
        const sides = cssGridParts(tokens), autoSide = sides.map(x => x.some(t => /^auto-flow$/i.test(t)));
        if (autoSide.some(Boolean)) {
            if (sides.length !== 2 || autoSide[0] === autoSide[1]) return null;
            const auto = autoSide[0] ? 0 : 1, t = [...sides[auto]]; let flow = false, dense = false;
            while (t.length && /^(auto-flow|dense)$/i.test(t[0])) { const x = t.shift().toLowerCase(); if (x === 'auto-flow') { if (flow) return null; flow = true; } else { if (dense) return null; dense = true; } }
            if (!flow) return null;
            const implicit = t.length ? cssGridTrackList(t, { implicit: true }) : { text: 'auto' }, explicit = cssGridTrackList(sides[1 - auto]);
            if (!implicit || !explicit) return null;
            values = auto === 0 ? ['none', explicit.text, 'none', dense ? 'dense' : 'row', implicit.text, 'auto'] : [explicit.text, 'none', 'none', dense ? 'column dense' : 'column', 'auto', implicit.text];
        } else { const t = cssGridTemplate(tokens); if (t) values = t.concat('row', 'auto', 'auto'); }
    }
    if (children) {
        if (!values) return null;
        const map = new SMap(children.map((name, i) => [name, [values[i], '']])); value = cssGridValue(map, p);
    }
    return value === undefined || value === null || value === '' ? null : { value, values };
}
function cssGridTemplateText(rows, columns, areas) {
    if (areas === 'none') return rows === 'none' && columns === 'none' ? 'none' : rows + ' / ' + columns;
    const strings = cssGridStrings(cssGridTokens(areas)), tracks = cssGridTokens(rows); if (!strings || !tracks || tracks.some(x => /^repeat\(|^subgrid$|^none$/i.test(x))) return '';
    let index = 0, out = [];
    for (const token of tracks) {
        const names = cssGridLineNames(token); if (names !== null) { if (names) out.push('[' + names + ']'); continue; }
        if (index >= strings.length || !cssGridTrack(token)) return '';
        out.push(strings[index++] + (token === 'auto' ? '' : ' ' + token));
    }
    if (index !== strings.length) return '';
    if (columns !== 'none') { const parsed = cssGridTrackList(cssGridTokens(columns), { noRepeat: true }); if (!parsed || parsed.text.startsWith('subgrid')) return ''; out.push('/ ' + columns); }
    return out.join(' ');
}
function cssGridValue(map, p, priority = false) {
    const direct = map.get(p); if (direct) return direct[priority ? 1 : 0];
    const names = cssGridChildren(p); if (!names) return '';
    const entries = names.map(x => map.get(x)); if (entries.some(x => !x)) return '';
    if (entries.some(x => x[1] !== entries[0][1])) return '';
    if (priority) return entries[0][1];
    const v = entries.map(x => x[0]); if (v.some(cssGridWide)) return v.every(x => x === v[0]) ? v[0] : '';
    if (v.some(x => /\b(?:var|env|attr)\(/i.test(x))) return '';
    if (p === 'grid-row' || p === 'grid-column') return v[1] === cssGridImplicitEnd(v[0]) ? v[0] : v.join(' / ');
    if (p === 'grid-area') { let n = 4; if (v[3] === cssGridImplicitEnd(v[1])) { n--; if (v[2] === cssGridImplicitEnd(v[0])) { n--; if (v[1] === cssGridImplicitEnd(v[0])) n--; } } return v.slice(0, n).join(' / '); }
    if (p === 'grid-template') return cssGridTemplateText(...v);
    if (v[3] === 'row' && v[4] === 'auto' && v[5] === 'auto') return cssGridTemplateText(v[0], v[1], v[2]);
    if (v[2] !== 'none') return '';
    const column = v[3].startsWith('column'), dense = v[3].includes('dense'), flow = 'auto-flow' + (dense ? ' dense' : '');
    if (column && v[1] === 'none' && v[4] === 'auto') return v[0] + ' / ' + flow + (v[5] === 'auto' ? '' : ' ' + v[5]);
    if (!column && v[0] === 'none' && v[5] === 'auto') return flow + (v[4] === 'auto' ? '' : ' ' + v[4]) + ' / ' + v[1];
    return '';
}
function cssGridSet(map, p, value, priority, cascading = false) {
    const parsed = cssGridParse(p, value); if (!parsed) return false;
    const group = cssGridChildren(p);
    if (group) {
        /* A pending shorthand keeps its original token stream until substitution.
           Replacing it must discard that stream; assigning a new pending value
           must discard the concrete components it replaces. Preserve important
           declarations only while parsing a declaration block, not setProperty. */
        const previous = map.get(p);
        if (cascading && previous?.[1] && !priority) return true;
        map.delete(p);
        if (!parsed.values) for (const name of group) {
            if (!cascading || !map.get(name)?.[1] || priority) map.delete(name);
        }
    }
    const names = parsed.values ? group : [p], values = parsed.values || [parsed.value];
    for (let i = 0; i < names.length; i++) { const old = map.get(names[i]); if (cascading && old?.[1] && !priority) continue; map.set(names[i], [values[i], priority]); }
    return true;
}
function cssGridRemove(map, p) { const old = cssGridValue(map, p); map.delete(p); for (const name of cssGridChildren(p) || []) map.delete(name); return old; }
