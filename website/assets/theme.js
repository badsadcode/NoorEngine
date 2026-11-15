// Light/dark toggle. The choice is remembered per browser; without one, the OS setting wins.
(function () {
  var root = document.documentElement;

  function current() {
    var set = root.getAttribute('data-theme');
    if (set === 'light' || set === 'dark') return set;
    return window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
  }

  document.addEventListener('click', function (event) {
    var button = event.target.closest && event.target.closest('.theme-toggle');
    if (!button) return;
    var next = current() === 'dark' ? 'light' : 'dark';
    root.setAttribute('data-theme', next);
    try { localStorage.setItem('noor-theme', next); } catch (_) { /* private mode: not remembered */ }
  });
})();
