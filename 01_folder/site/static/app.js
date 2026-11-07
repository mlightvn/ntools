// nexplorer.vietkeys.com: the theme switcher over the screenshots (it also tints the page with that
// theme's accent) and the remembered page language.
(function () {
  'use strict';

  var show = document.querySelector('[data-showcase]');
  if (show) {
    var buttons = show.querySelectorAll('[data-theme]');
    var shots = show.querySelectorAll('[data-shot]');
    var still = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    var timer = 0, current = 0;

    var pick = function (i) {
      current = i;
      for (var b = 0; b < buttons.length; b++) buttons[b].setAttribute('aria-pressed', String(b === i));
      for (var s = 0; s < shots.length; s++) shots[s].classList.toggle('on', s === i);
      document.documentElement.style.setProperty('--accent', buttons[i].getAttribute('data-accent'));
    };
    // Cycle through the themes until the visitor picks one
    var cycle = function () {
      timer = setTimeout(function () { pick((current + 1) % buttons.length); cycle(); }, 4500);
    };
    for (var i = 0; i < buttons.length; i++) {
      buttons[i].addEventListener('click', (function (n) {
        return function () { clearTimeout(timer); pick(n); };
      })(i));
    }
    if (!still) cycle();
  }

  // A language picked by hand wins over the browser language (see the redirect on /)
  var links = document.querySelectorAll('[data-lang]');
  for (var j = 0; j < links.length; j++) {
    links[j].addEventListener('click', function () {
      try { localStorage.setItem('nx-lang', this.getAttribute('data-lang')); } catch (e) { /* private mode */ }
    });
  }
})();
