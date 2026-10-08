/* Onyx web site -- the little script. Everything it does is an enhancement: without it the pages are
   complete (dark, every section visible, the catalogue whole, the captures plain links).
   No dependency, no request. MIT licence (ours). */
(function () {
  'use strict';
  var root = document.documentElement;
  function all(sel, from) { return Array.prototype.slice.call((from || document).querySelectorAll(sel)); }

  /* ---- the mobile menu ---- */
  var header = document.querySelector('.site-header'), menu = document.querySelector('.menu-btn');
  if (header && menu) {
    var setMenu = function (open) {
      header.classList.toggle('is-open', open);
      menu.setAttribute('aria-expanded', open ? 'true' : 'false');
    };
    menu.addEventListener('click', function () { setMenu(!header.classList.contains('is-open')); });
    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape' && header.classList.contains('is-open')) { setMenu(false); menu.focus(); }
    });
    all('.nav a', header).forEach(function (a) { a.addEventListener('click', function () { setMenu(false); }); });
    document.addEventListener('click', function (e) { if (header.classList.contains('is-open') && !header.contains(e.target)) setMenu(false); });
  }

  /* ---- the theme switch: dark is the site, light is the visitor's choice, remembered ---- */
  var tbtn = document.querySelector('.theme-btn');
  if (tbtn) {
    var sync = function () { tbtn.setAttribute('aria-pressed', root.getAttribute('data-theme') === 'light' ? 'true' : 'false'); };
    tbtn.addEventListener('click', function () {
      var light = root.getAttribute('data-theme') !== 'light';
      if (light) root.setAttribute('data-theme', 'light'); else root.removeAttribute('data-theme');
      try { localStorage.setItem('onyx-theme', light ? 'light' : 'dark'); } catch (e) {}
      sync();
    });
    sync();
  }

  /* ---- the app band (Home): the icons are tabs, one panel shows ---- */
  all('[role="tablist"]').forEach(function (list) {
    var tabs = all('[role="tab"]', list);
    function select(tab, focus) {
      tabs.forEach(function (t) {
        var on = t === tab, panel = document.getElementById(t.getAttribute('aria-controls'));
        t.setAttribute('aria-selected', on ? 'true' : 'false');
        t.tabIndex = on ? 0 : -1;
        if (panel) panel.hidden = !on;
      });
      if (focus) {
        tab.focus();
        if (tab.scrollIntoView) tab.scrollIntoView({ block: 'nearest', inline: 'center' });
      }
    }
    tabs.forEach(function (t, i) {
      t.addEventListener('click', function () { select(t, false); });
      t.addEventListener('keydown', function (e) {
        var n = e.key === 'ArrowRight' ? i + 1 : e.key === 'ArrowLeft' ? i - 1 : e.key === 'Home' ? 0 : e.key === 'End' ? tabs.length - 1 : -1;
        if (n < 0 && e.key !== 'ArrowLeft') return;
        e.preventDefault();
        select(tabs[(n + tabs.length) % tabs.length], true);
      });
    });
    if (tabs.length) select(tabs[0], false);
  });

  /* ---- the catalogue's filter (Apps): the anchor links become buttons that show one group ---- */
  var chips = all('.chip[data-filter]');
  if (chips.length) {
    var groups = all('[data-group]');
    var filter = function (id) {
      chips.forEach(function (c) { c.setAttribute('aria-pressed', c.getAttribute('data-filter') === id ? 'true' : 'false'); });
      groups.forEach(function (g) { g.hidden = id !== 'all' && g.getAttribute('data-group') !== id; });
    };
    chips.forEach(function (c) {
      c.setAttribute('role', 'button');
      var go = function (e) {
        e.preventDefault();
        filter(c.getAttribute('data-filter'));
        var top = document.getElementById('catalogue');
        if (top && top.getBoundingClientRect().top < 0) window.scrollTo(0, window.pageYOffset + top.getBoundingClientRect().top - 120);
      };
      c.addEventListener('click', go);
      c.addEventListener('keydown', function (e) { if (e.key === ' ' || e.key === 'Spacebar') go(e); });
    });
    filter('all');
  }

  /* ---- a card's capture opens in the page's dialog ---- */
  var dialog = document.querySelector('.shot-dialog');
  if (dialog && typeof dialog.showModal === 'function') {
    var dimg = dialog.querySelector('img'), dcap = dialog.querySelector('figcaption');
    all('a[data-shot]').forEach(function (a) {
      a.addEventListener('click', function (e) {
        if (e.ctrlKey || e.metaKey || e.shiftKey) return;
        e.preventDefault();
        dimg.removeAttribute('src');
        dimg.width = +a.getAttribute('data-w'); dimg.height = +a.getAttribute('data-h');
        dimg.style.setProperty('--native', a.getAttribute('data-w'));
        dimg.alt = a.getAttribute('data-alt') || '';
        dimg.src = a.getAttribute('href');
        dcap.textContent = a.getAttribute('data-caption') || '';
        dialog.showModal();
      });
    });
    dialog.addEventListener('click', function (e) { if (e.target === dialog) dialog.close(); });
  }

  /* ---- the desktop's looks (Desktop): a swatch shows its capture ---- */
  var swatches = all('.swatch[data-look]');
  if (swatches.length) {
    var looks = all('.look[data-look]');
    var show = function (id) {
      swatches.forEach(function (s) { s.setAttribute('aria-pressed', s.getAttribute('data-look') === id ? 'true' : 'false'); });
      looks.forEach(function (l) { l.hidden = l.getAttribute('data-look') !== id; });
    };
    swatches.forEach(function (s) { s.addEventListener('click', function () { show(s.getAttribute('data-look')); }); });
    show(swatches[0].getAttribute('data-look'));
  }

  window.onyxReady = true;      /* the head's safety net: everything above ran */

  /* ---- the reveal: once, when a block enters the screen; ?static shows everything at once (captures, tests) ---- */
  var els = all('.reveal');
  function showAll() { els.forEach(function (e) { e.classList.add('is-in'); }); }
  if (/[?&]static/.test(location.search)) { els.forEach(function (e) { e.style.transition = 'none'; }); showAll(); return; }
  if (!('IntersectionObserver' in window)) { showAll(); return; }
  var io = new IntersectionObserver(function (entries) {
    entries.forEach(function (en) {
      /* 8 % of the block, or -- for a block taller than the screen -- a good part of the screen */
      if (en.isIntersecting && (en.intersectionRatio >= 0.08 || en.intersectionRect.height >= window.innerHeight * 0.3)) {
        en.target.classList.add('is-in'); io.unobserve(en.target);
      }
    });
  }, { rootMargin: '0px 0px -8% 0px', threshold: [0, 0.08, 0.2, 0.4] });
  els.forEach(function (e) { io.observe(e); });
  /* Printing, or a browser that never scrolls (a capture of the whole page): nothing stays hidden. */
  window.addEventListener('beforeprint', showAll);
})();
