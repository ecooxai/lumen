/* Read-only companion to tools/macos_input_probe.m, loaded with LUMEN_PRE_FILE.
 * It does not focus, click, edit, submit, or send network requests. */
(() => {
    const prefix = 'LUMEN_NATIVE_INPUT';
    const start = performance.now();
    let previous = '', composer = null, events = 0;
    const value = el => el && /^(INPUT|TEXTAREA)$/.test(el.tagName) ? el.value : el ? el.textContent : null;
    const summary = el => el ? { tag: el.tagName, id: el.id || '', editable: el.contentEditable } : null;
    const emit = (kind, data) => console.log(prefix, kind, JSON.stringify(data));
    function find() {
        const candidates = [document.querySelector('#mobile-composer-prompt'), document.querySelector('#prompt-textarea'),
            ...document.querySelectorAll('textarea'), ...document.querySelectorAll('[contenteditable="true"]')];
        return candidates.find(el => {
            if (!el || !el.isConnected) return false;
            const r = el.getBoundingClientRect(), s = getComputedStyle(el);
            return r.width > 20 && r.height > 10 && s.display !== 'none' && s.visibility !== 'hidden';
        }) || null;
    }
    function snapshot() {
        composer = find();
        const r = composer && composer.getBoundingClientRect();
        const state = {
            composer: summary(composer), active: summary(document.activeElement), value: value(composer),
            focused: !!composer && (document.activeElement === composer || composer.contains(document.activeElement)),
            selection: composer ? [composer.selectionStart, composer.selectionEnd, composer.selectionDirection] : null,
            rect: r ? [r.x, r.y, r.width, r.height] : null,
            viewport: [innerWidth, innerHeight], outer: [outerWidth, outerHeight], dpr: devicePixelRatio
        };
        const serialized = JSON.stringify(state);
        if (serialized !== previous) { previous = serialized; emit('STATE', state); }
        if (performance.now() - start > 90000) { clearInterval(timer); emit('DONE', { events }); }
    }
    for (const type of ['pointerdown','mousedown','click','focusin','focusout','keydown','keyup','beforeinput','input','change','submit']) {
        document.addEventListener(type, event => {
            if (events >= 160) return;
            const candidate = composer || find();
            if (!candidate || !(event.target === candidate || candidate.contains(event.target) ||
                    (type === 'submit' && event.target.contains(candidate)))) return;
            events++;
            emit('EVENT', { type, key: event.key || '', inputType: event.inputType || '', data: event.data ?? null,
                trusted: event.isTrusted, canceled: event.defaultPrevented, value: value(candidate),
                selection: [candidate.selectionStart, candidate.selectionEnd] });
            setTimeout(snapshot, 0);
        }, true);
    }
    const timer = setInterval(snapshot, 200);
    snapshot();
})();
