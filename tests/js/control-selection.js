/* Public DOM API regression; also runs unchanged in a WebKit reference page. */
(function () {
    'use strict';
    var result = {checks: 0, failures: [], finished: false};
    globalThis.__controlResult = result;
    function check(name, fn) {
        result.checks++;
        try { if (fn()) return; } catch (error) { result.failures.push(name + ': ' + error.name); return; }
        result.failures.push(name);
    }
    function field(tag, type, value) {
        var element = document.createElement(tag);
        if (type) element.type = type;
        document.body.appendChild(element);
        if (value !== undefined) element.value = value;
        return element;
    }
    function selected(element, start, end, direction) {
        return element.selectionStart === start && element.selectionEnd === end && (direction === undefined || element.selectionDirection === direction);
    }
    function throws(name, fn) { try { fn(); return false; } catch (error) { return error.name === name; } }
    ['textarea', 'text', 'search', 'url', 'tel', 'password'].forEach(function (type) {
        var element = field(type === 'textarea' ? 'textarea' : 'input', type === 'textarea' ? null : type, 'abcdef');
        check(type + ': new value puts caret at end', function () { return selected(element, 6, 6); });
        check(type + ': setSelectionRange preserves backward direction', function () { element.setSelectionRange(1, 4, 'backward'); return selected(element, 1, 4, 'backward'); });
        check(type + ': selectionStart above end collapses forward', function () { element.selectionStart = 5; return selected(element, 5, 5); });
        check(type + ': selectionEnd below start collapses backward', function () { element.selectionEnd = 2; return selected(element, 2, 2); });
        check(type + ': selection direction setter', function () { element.selectionDirection = 'forward'; return selected(element, 2, 2, 'forward'); });
        check(type + ': invalid direction normalizes to none', function () { element.selectionDirection = 'sideways'; return element.selectionDirection === 'none'; });
        check(type + ': end before start collapses at end', function () { element.setSelectionRange(5, 2); return selected(element, 2, 2, 'none'); });
        check(type + ': bounds clamp to UTF16 length', function () { element.setSelectionRange(90, 99); return selected(element, 6, 6); });
        check(type + ': assigning same value preserves range', function () { element.setSelectionRange(1, 4, 'backward'); element.value = 'abcdef'; return selected(element, 1, 4, 'backward'); });
        check(type + ': assigning changed value resets caret', function () { element.value = 'xy'; return selected(element, 2, 2, 'none'); });
        check(type + ': select selects the whole value', function () { element.select(); return selected(element, 0, 2); });
    });
    ['email', 'number', 'date', 'checkbox', 'hidden'].forEach(function (type) {
        var element = field('input', type);
        check(type + ': selection getters are null', function () { return element.selectionStart === null && element.selectionEnd === null && element.selectionDirection === null; });
        check(type + ': selection setter throws InvalidStateError', function () { return throws('InvalidStateError', function () { element.selectionStart = 1; }); });
        check(type + ': range method throws InvalidStateError', function () { return throws('InvalidStateError', function () { element.setSelectionRange(0, 1); }); });
        check(type + ': replacement throws InvalidStateError', function () { return throws('InvalidStateError', function () { element.setRangeText('x'); }); });
    });
    var input = field('input', 'text', 'abcd'), textarea = field('textarea', null, 'abcdef');
    check('negative unsigned selection offset clamps to end', function () { input.setSelectionRange(-1, -1); return selected(input, 4, 4); });
    check('fractional selection offsets truncate', function () { input.setSelectionRange(1.9, 3.9); return selected(input, 1, 3); });
    check('nonfinite selection offsets convert to zero', function () { input.setSelectionRange(NaN, Infinity); return selected(input, 0, 0); });
    check('null selection setter converts to zero', function () { input.setSelectionRange(2, 3); input.selectionStart = null; return selected(input, 0, 3); });
    check('setSelectionRange requires both offsets', function () { return throws('TypeError', function () { input.setSelectionRange(1); }); });
    check('input type transition disables selection API', function () { input.type = 'email'; var ok = input.selectionStart === null; input.type = 'text'; return ok; });
    check('invalid input type follows text selection semantics', function () { input.type = 'not-a-type'; input.setSelectionRange(1, 2); return selected(input, 1, 2); });
    check('newly applicable input selection starts at zero', function () { input.type = 'text'; input.value = 'abcd'; input.setSelectionRange(1, 3, 'backward'); input.type = 'email'; input.type = 'text'; return selected(input, 0, 0, 'none'); });
    check('changing between selectable input types preserves selection', function () { input.value = 'abcd'; input.setSelectionRange(1, 3, 'backward'); input.type = 'password'; var ok = selected(input, 1, 3, 'backward'); input.type = 'text'; return ok; });
    check('input type change sanitizes the existing value', function () { input.value = '  a@example.test  '; input.type = 'email'; var ok = input.value === 'a@example.test'; input.type = 'text'; return ok; });
    check('selection offset conversion can change the value reentrantly', function () { input.value = 'ab'; input.selectionStart = {valueOf: function () { input.value = 'abcdef'; return 2; }}; return input.value === 'abcdef' && selected(input, 2, 6); });
    ['checkbox', 'radio', 'hidden', 'button', 'submit', 'reset', 'image'].forEach(function (type) {
        check(type + ': default-mode value reflects its attribute and survives text transition', function () {
            var element = field('input', type, 'choice');
            var reflects = element.getAttribute('value') === 'choice' && element.defaultValue === 'choice';
            element.type = 'text';
            return reflects && element.value === 'choice' && selected(element, 0, 0, 'none');
        });
    });
    check('default-on value falls back to on only when attribute is absent', function () { var element = field('input', 'checkbox'); var absent = element.value === 'on'; element.value = ''; var empty = element.value === '' && element.hasAttribute('value'); element.removeAttribute('value'); return absent && empty && element.value === 'on'; });
    check('default-mode value follows later content attribute changes', function () { var element = field('input', 'button', 'first'); element.setAttribute('value', 'second'); var updated = element.value === 'second'; element.removeAttribute('value'); return updated && element.value === ''; });
    check('text to default-mode transition reflects the dirty value', function () { var element = field('input', 'text', 'typed'); element.type = 'checkbox'; return element.value === 'typed' && element.getAttribute('value') === 'typed'; });
    check('text value stays independent of its default attribute', function () { var element = field('input', 'text'); element.defaultValue = 'default'; element.value = 'typed'; return element.value === 'typed' && element.getAttribute('value') === 'default'; });
    check('input value rejects Symbol conversion', function () { return throws('TypeError', function () { input.value = Symbol('value'); }); });
    check('textarea value rejects Symbol conversion', function () { return throws('TypeError', function () { textarea.value = Symbol('value'); }); });
    check('input value maps null to empty but stringifies undefined', function () { input.value = null; var empty = input.value === ''; input.value = undefined; return empty && input.value === 'undefined'; });
    check('textarea value maps null to empty but stringifies undefined', function () { textarea.value = null; var empty = textarea.value === ''; textarea.value = undefined; return empty && textarea.value === 'undefined'; });

    function range(name, mode, expectedValue, start, end) {
        check(name, function () { textarea.value = 'abcdef'; textarea.setSelectionRange(1, 5, 'backward'); textarea.setRangeText('XY', 2, 4, mode); return textarea.value === expectedValue && selected(textarea, start, end); });
    }
    range('setRangeText select mode', 'select', 'abXYef', 2, 4);
    range('setRangeText start mode', 'start', 'abXYef', 2, 2);
    range('setRangeText end mode', 'end', 'abXYef', 4, 4);
    range('setRangeText preserve outside endpoints', 'preserve', 'abXYef', 1, 5);
    check('setRangeText default replaces active selection', function () { textarea.value = 'abcdef'; textarea.setSelectionRange(1, 4); textarea.setRangeText('X'); return textarea.value === 'aXef' && selected(textarea, 1, 2); });
    check('preserve shifts selection after replacement', function () { textarea.value = 'abcdef'; textarea.setSelectionRange(4, 6, 'backward'); textarea.setRangeText('XYZ', 0, 2); return textarea.value === 'XYZcdef' && selected(textarea, 5, 7); });
    check('preserve clamps endpoints inside replaced range', function () { textarea.value = 'abcdef'; textarea.setSelectionRange(2, 3); textarea.setRangeText('X', 1, 5); return textarea.value === 'aXf' && selected(textarea, 1, 2); });
    check('setRangeText range clamps after ordering check', function () { textarea.value = 'abc'; textarea.setRangeText('X', 10, 20, 'end'); return textarea.value === 'abcX' && selected(textarea, 4, 4); });
    check('setRangeText rejects reversed range without mutation', function () { textarea.value = 'abc'; return throws('IndexSizeError', function () { textarea.setRangeText('X', 10, 5); }) && textarea.value === 'abc'; });
    check('setRangeText invalid mode throws TypeError without mutation', function () { textarea.value = 'abc'; return throws('TypeError', function () { textarea.setRangeText('X', 0, 1, 'middle'); }) && textarea.value === 'abc'; });
    check('setRangeText two-argument call throws TypeError', function () { return throws('TypeError', function () { textarea.setRangeText('X', 1); }); });
    check('setRangeText zero-argument call throws TypeError', function () { return throws('TypeError', function () { textarea.setRangeText(); }); });
    check('replacement conversion observes the updated live selection', function () { textarea.value = 'ab'; textarea.setSelectionRange(1, 1); textarea.setRangeText({toString: function () { textarea.value = 'abcd'; textarea.setSelectionRange(2, 3); return 'X'; }}); return textarea.value === 'abXd' && selected(textarea, 2, 3); });
    check('reversed replacement sets dirty-value flag before IndexSizeError', function () { var t = field('textarea'); t.defaultValue = 'abc'; var ok = throws('IndexSizeError', function () { t.setRangeText('X', 3, 1); }); t.defaultValue = 'changed'; return ok && t.value === 'abc'; });
    check('replacement selection uses raw CRLF length before normalization', function () { textarea.value = 'abc'; textarea.setRangeText('\r\n', 0, 1, 'end'); return textarea.value === '\nbc' && selected(textarea, 2, 2, 'none'); });
    check('textarea value normalizes CRLF and CR', function () { textarea.value = 'a\r\nb\rc'; return textarea.value === 'a\nb\nc' && selected(textarea, 5, 5); });
    check('input value strips CR and LF', function () { input.type = 'text'; input.value = 'a\r\nb\nc'; return input.value === 'abc' && selected(input, 3, 3); });

    check('surrogate pair offsets count as two UTF16 units', function () { textarea.value = 'A\uD83D\uDE00B'; textarea.setSelectionRange(1, 3); return textarea.value.length === 4 && selected(textarea, 1, 3); });
    check('emoji replacement preserves adjacent UTF8 content', function () { textarea.setRangeText('\u00E9', 1, 3, 'end'); return textarea.value === 'A\u00E9B' && selected(textarea, 2, 2); });
    check('selection may address the middle of a surrogate pair', function () { textarea.value = 'A\uD83D\uDE00B'; textarea.setSelectionRange(2, 2); return selected(textarea, 2, 2); });
    check('setRangeText preserves unpaired UTF16 code units', function () { textarea.setRangeText('-', 2, 2, 'end'); return textarea.value === 'A\uD83D-\uDE00B' && selected(textarea, 3, 3); });
    check('value preserves NUL and unpaired surrogates', function () { textarea.value = 'a\u0000\uD800b'; return textarea.value.length === 4 && textarea.value.charCodeAt(1) === 0 && textarea.value.charCodeAt(2) === 0xD800 && selected(textarea, 4, 4); });
    check('readonly does not reject programmatic selection or replacement', function () { textarea.readOnly = true; textarea.value = 'abc'; textarea.setSelectionRange(1, 2); textarea.setRangeText('X', 1, 2, 'end'); textarea.readOnly = false; return textarea.value === 'aXc' && selected(textarea, 2, 2); });
    check('disabled does not reject programmatic selection or replacement', function () { textarea.disabled = true; textarea.value = 'abc'; textarea.setRangeText('X', 1, 2, 'select'); textarea.disabled = false; return textarea.value === 'aXc' && selected(textarea, 1, 2); });
    check('default textarea value changes before dirty value', function () { var t = field('textarea'); t.defaultValue = 'abcd'; t.setSelectionRange(1, 3); t.defaultValue = 'xy'; return t.value === 'xy' && t.selectionEnd <= 2; });
    check('default textarea changes do not overwrite dirty value', function () { var t = field('textarea', null, 'user'); t.defaultValue = 'default'; return t.value === 'user'; });
    check('cloned dirty textarea retains its independent value', function () { textarea.value = 'A\uD800B'; var clone = textarea.cloneNode(true); textarea.value = 'other'; return clone.value === 'A\uD800B'; });
    check('cloned dirty input retains its independent value', function () { input.value = 'edited'; var clone = input.cloneNode(); input.value = 'other'; return clone.value === 'edited'; });

    var events = {select: 0, input: 0, change: 0};
    var eventField = field('textarea', null, 'abcdef');
    eventField.addEventListener('select', function (event) { if (event.bubbles && !event.cancelable) events.select++; });
    eventField.addEventListener('input', function () { events.input++; });
    eventField.addEventListener('change', function () { events.change++; });
    check('selection and range mutation happen synchronously', function () { eventField.setSelectionRange(1, 3); eventField.setRangeText('X', 1, 3, 'select'); return eventField.value === 'aXdef' && selected(eventField, 1, 2); });
    setTimeout(function () {
        check('selection updates emit select event', function () { return events.select > 0; });
        check('programmatic replacements do not emit input or change', function () { return events.input === 0 && events.change === 0; });
        result.finished = true;
        result.failures.forEach(function (name) { console.log('FAIL control-selection ' + name); });
        console.log('RESULTS control-selection ' + (result.failures.length ? 'FAIL' : 'PASS') + ' (' + (result.checks - result.failures.length) + '/' + result.checks + ' checks)');
    }, 20);
}());
