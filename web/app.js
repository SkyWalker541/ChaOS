/* ChaOS's site: the boot, the button tabs, and the device screens. */
(function () {
  "use strict";

  /* ---- the boot: tools/genchaosmotion.py's boot_frame, on a canvas ---- */
  var boot = document.getElementById("boot");
  var canvas = document.getElementById("boot-canvas");
  var B = window.CHAOS_BOOT;
  var SLOW = 1.5;            /* the Brick's 0.6 s, a little slower to read on a page */
  var HOLD = 650;            /* ms the whole logo holds before the page */

  function remember() { try { sessionStorage.setItem("chaos-booted", "1"); } catch (e) {} }

  function draw(ctx, t) {
    var g = B.ground, cell = B.cell, cs = B.cells, i, c, k, off, a;
    ctx.fillStyle = g;
    ctx.fillRect(0, 0, B.w, B.h);
    for (i = 0; i < cs.length; i++) {
      c = cs[i];
      k = (t - c[6] * B.delayMax) / B.flight;
      if (k <= 0) continue;
      if (k > 1) k = 1;
      off = c[5] * Math.pow(1 - k, 3);
      a = Math.min(1, k / 0.35);
      ctx.globalAlpha = a;
      ctx.fillStyle = B.colors[c[2]];
      ctx.fillRect(Math.round(c[0] + c[3] * off), Math.round(c[1] + c[4] * off), cell, cell);
    }
    ctx.globalAlpha = 1;
  }

  function finish() {
    if (!boot || boot.classList.contains("done")) return;
    boot.classList.add("done");
    boot.classList.remove("playing");
    remember();
  }

  function play() {
    if (!boot || !canvas || !B) { finish(); return; }
    var ctx = canvas.getContext("2d");
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = B.w * dpr; canvas.height = B.h * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.imageSmoothingEnabled = false;
    document.documentElement.classList.remove("booted");
    boot.classList.remove("done");
    boot.classList.add("playing");
    var end = (B.delayMax + B.flight) * 1000 * SLOW, start = null;
    function frame(now) {
      if (boot.classList.contains("done")) return;
      if (start === null) start = now;
      var ms = now - start;
      draw(ctx, Math.min(ms, end) / 1000 / SLOW);
      if (ms < end + HOLD) requestAnimationFrame(frame); else finish();
    }
    requestAnimationFrame(frame);
  }

  var reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  var seen = document.documentElement.classList.contains("booted");
  if (boot) {
    boot.addEventListener("click", finish);
    window.addEventListener("keydown", function (e) { if (boot.classList.contains("playing")) { finish(); e.preventDefault(); } });
    if (reduced || seen) { boot.classList.add("done"); remember(); } else play();
  }
  var lockup = document.getElementById("lockup");
  if (lockup) lockup.addEventListener("click", function () { if (!reduced) play(); });

  /* ---- button tabs ---------------------------------------------------- */
  var tabs = document.querySelectorAll('.tabs [role="tab"]');
  Array.prototype.forEach.call(tabs, function (tab, n) {
    tab.addEventListener("click", function () { select(tab); });
    tab.addEventListener("keydown", function (e) {
      var d = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
      if (!d) return;
      var next = tabs[(n + d + tabs.length) % tabs.length];
      next.focus(); select(next);
    });
  });
  function select(tab) {
    Array.prototype.forEach.call(tabs, function (t) {
      var on = t === tab;
      t.setAttribute("aria-selected", on ? "true" : "false");
      t.tabIndex = on ? 0 : -1;
      document.getElementById(t.dataset.tab).hidden = !on;
    });
  }

  /* ---- device screens: chips under each to switch ---------------------- */
  Array.prototype.forEach.call(document.querySelectorAll(".device[data-screens]"), function (fig) {
    var list, img = fig.querySelector("img"), chips = fig.querySelector(".chips");
    try { list = JSON.parse(fig.dataset.screens); } catch (e) { return; }
    function show(i) {
      img.style.opacity = "0";
      setTimeout(function () {
        img.src = "img/" + list[i][0] + ".webp";
        img.alt = list[i][1] + ", on the Brick";
        img.onload = function () { img.style.opacity = "1"; };
      }, 140);
      Array.prototype.forEach.call(chips.children, function (b, j) { b.setAttribute("aria-pressed", j === i ? "true" : "false"); });
    }
    img.alt = list[0][1] + ", on the Brick";
    if (list.length < 2) return;
    list.forEach(function (s, i) {
      var b = document.createElement("button");
      b.type = "button"; b.textContent = s[1];
      b.setAttribute("aria-pressed", i === 0 ? "true" : "false");
      b.addEventListener("click", function () { show(i); });
      chips.appendChild(b);
    });
    list.forEach(function (s) { var p = new Image(); p.src = "img/" + s[0] + ".webp"; });
  });

  /* ---- the hero's screen, cycling through the themes ------------------- */
  var hero = document.querySelector(".hero-device img");
  if (hero && !reduced) {
    var cycle = ["home-pixelnames", "home-pixelcards", "home-fancy", "home-plainjane", "chaos-shader", "pm-list"], at = 0;
    cycle.forEach(function (s) { var p = new Image(); p.src = "img/" + s + ".webp"; });
    setInterval(function () {
      if (document.hidden) return;
      at = (at + 1) % cycle.length;
      hero.style.opacity = "0";
      setTimeout(function () { hero.src = "img/" + cycle[at] + ".webp"; hero.onload = function () { hero.style.opacity = "1"; }; }, 180);
    }, 3200);
  }
})();
