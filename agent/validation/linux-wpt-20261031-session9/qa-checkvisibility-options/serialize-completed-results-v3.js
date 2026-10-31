// Transport only: observe the fixture's completed lexical result arrays; never rerun a check.
(() => {
  const completed = document.documentElement.getAttribute('data-checkvisibility-options-complete');
  if (!Array.isArray(results) || results.length !== 49 || !Array.isArray(failed) || (completed !== 'PASS' && completed !== 'FAIL')) throw new Error('Incomplete original fixture');
  const emit = (prefix, value) => {
    const line = (prefix + ' ' + JSON.stringify(value)).replace(/[^\x00-\x7f]/g, c => '\\u' + c.charCodeAt(0).toString(16).padStart(4, '0'));
    if (line.length >= 1900) throw new Error('Transport row exceeds native log budget');
    console.log(line);
  };
  for (let i = 0; i < results.length; ++i) emit('TRANSPORT_ROW checkvisibility-options', {index:i,result:results[i]});
  emit('TRANSPORT_SUMMARY checkvisibility-options', {total:results.length,passed:results.length-failed.length,failed:failed.length,completion:completed});
})();
