/* Served by the official WPT server at /resources/testharnessreport.js.
 * Register synchronously after testharness.js; late callbacks are not replayed.
 * No browser APIs, assertions, test contents, or testdriver actions are replaced.
 */
(function () {
  if (typeof window !== 'undefined' && window.top !== window) return;
  var log = console.log.bind(console), serial = 0, emitted = new WeakSet();
  var completion = null, completionText = null, completedTests = null, callbackCount = 0;
  function ascii(value) {
    return JSON.stringify(value).replace(/[\u007f-\uffff]/g, function (character) {
      return '\\u' + ('0000' + character.charCodeAt(0).toString(16)).slice(-4);
    });
  }
  function report(kind, value) {
    var payload = ascii(value);
    if (payload.length <= 1600) {
      log('LUME_WPT_' + kind + ' ' + payload);
      return;
    }
    // Lumen's native console caps each message at 2000 UTF-8 bytes. Keep
    // complete assertion names by chunking ASCII JSON below that bound.
    var id = ++serial, total = Math.ceil(payload.length / 600);
    for (var index = 0; index < total; index++) {
      log('LUME_WPT_CHUNK ' + ascii({id:id, kind:kind, index:index,
        total:total, data:payload.slice(index * 600, (index + 1) * 600)}));
    }
  }
  function describe(test) {
    return {name:String(test.name),
      status:['PASS','FAIL','TIMEOUT','NOTRUN','PRECONDITION_FAILED'][test.status] || 'UNKNOWN',
      message:test.message == null ? null : String(test.message).slice(0,4000)};
  }
  function emit(test) {
    if (emitted.has(test)) return;
    emitted.add(test);
    report('SUB', describe(test));
  }
  if (typeof add_result_callback !== 'function' || typeof add_completion_callback !== 'function') {
    report('READY', {status:'ERROR', message:'testharness callbacks unavailable at the official reporter boundary'});
    return;
  }
  add_result_callback(emit);
  add_completion_callback(function (tests, status) {
    var candidate = {status:['OK','ERROR','TIMEOUT','PRECONDITION_FAILED'][status.status] || 'UNKNOWN',
      message:status.message == null ? null : String(status.message).slice(0,4000),
      subtest_count:tests.length};
    callbackCount++;
    if (completion !== null) {
      // A browser may cause the harness to call completion twice after an
      // uncaught error/timeout. Preserve that anomaly without counting the
      // same finalized result twice. Changed assertions/status remain invalid.
      var seen = new WeakSet(), identical = ascii(candidate) === completionText;
      for (var index = 0; index < tests.length; index++) {
        var test = tests[index];
        if (seen.has(test) || completedTests.get(test) !== ascii(describe(test))) identical = false;
        seen.add(test);
      }
      report('REPEAT', {status:identical ? 'IDENTICAL' : 'CONFLICT',
        completion_callback_count:callbackCount, completion:candidate});
      return;
    }
    completedTests = new WeakMap();
    for (var index = 0; index < tests.length; index++) {
      emit(tests[index]);
      completedTests.set(tests[index], ascii(describe(tests[index])));
    }
    completion = candidate;
    completionText = ascii(candidate);
    report('DONE', candidate);
  });
  report('READY', {status:'ATTACHED'});
})();
