// Diagnostic only: observe the page's real workers before its scripts run.
// This file must never be injected into a scored conformance run.
(() => {
  const emit = (stage, details) => console.log('LUMEN_WORKER_DIAGNOSTIC ' +
    JSON.stringify({stage, ...details}));
  const text = value => String(value == null ? '' : value).slice(0, 600);
  const OriginalWorker = globalThis.Worker;
  emit('prefile', {url: text(location.href), worker: typeof OriginalWorker});
  if (typeof OriginalWorker !== 'function') return;
  let serial = 0;
  globalThis.Worker = new Proxy(OriginalWorker, {
    construct(target, args, newTarget) {
      const id = ++serial;
      emit('construct', {id, url: text(args[0])});
      let worker;
      try { worker = Reflect.construct(target, args, newTarget); }
      catch (error) {
        emit('construct-error', {id, name: text(error.name), message: text(error.message)});
        throw error;
      }
      let messages = 0;
      worker.addEventListener('message', event => {
        if (++messages <= 5 || (event.data && event.data.type === 'complete'))
          emit('message', {id, ordinal: messages, type: text(event.data && event.data.type)});
      });
      worker.addEventListener('messageerror', () => emit('messageerror', {id}));
      worker.addEventListener('error', event => emit('error', {
        id, eventType: text(event.type), message: text(event.message),
        filename: text(event.filename), line: event.lineno, column: event.colno
      }));
      return worker;
    }
  });
})();
