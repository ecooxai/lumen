order.push('ext');
window.extCurrent = document.currentScript && document.currentScript.getAttribute('src');
document.write('<script>order.push("nested")<\/script>');
