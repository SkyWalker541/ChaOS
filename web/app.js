/* ChaOS's site: the little boot, the button tabs, and the device screens. */
(function () {
  "use strict";

  /* ---- the little boot: the Brick's boot (tools/genchaosmotion.py's
   * boot_frame), only its mark, played small on the header's logo a moment
   * after the page loads. The mark's cells are the boot's left of x 440; each
   * 12 px cell is 4 px of the 64 px mark, drawn at the header img's size. */
  var B = window.CHAOS_BOOT;
  var SLOW = 3.2, DELAY = 500;
  var reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  var brand = document.querySelector(".brand"), mark = brand && brand.querySelector("img");
  var running = false;

  function little() {
    if (running || reduced || !B || !mark) return;
    var r = mark.getBoundingClientRect();
    if (!r.width) return;
    var cells = B.cells.filter(function (c) { return c[0] < 440; });
    var u = r.width / 64;                 /* css px per mark px */
    var k = u * 4 / B.cell;               /* boot px -> css px */
    var pad = 140;                        /* room for the pixels to fly in from */
    var cv = document.createElement("canvas");
    var W = r.width + pad * 2, H = r.height + pad * 2;
    var dpr = Math.min(window.devicePixelRatio || 1, 3);
    cv.id = "mark-boot";
    cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr);
    cv.style.width = W + "px"; cv.style.height = H + "px";
    cv.style.left = (r.left - pad) + "px"; cv.style.top = (r.top - pad) + "px";
    document.body.appendChild(cv);
    var ctx = cv.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.imageSmoothingEnabled = false;
    brand.classList.add("booting");
    running = true;
    var end = (B.delayMax + B.flight) * 1000 * SLOW, start = null, sz = 4 * u;
    function frame(now) {
      if (start === null) start = now;
      var t = Math.min(now - start, end) / 1000 / SLOW, i, c, q, off;
      ctx.clearRect(0, 0, W, H);
      for (i = 0; i < cells.length; i++) {
        c = cells[i];
        q = (t - c[6] * B.delayMax) / B.flight;
        if (q <= 0) continue;
        if (q > 1) q = 1;
        off = c[5] * k * 1.4 * Math.pow(1 - q, 3);
        ctx.globalAlpha = Math.min(1, q / 0.35);
        ctx.fillStyle = B.colors[c[2]];
        ctx.fillRect(pad + 4 * u + (c[0] - 230) * k + c[3] * off,
                     pad + (c[1] - 288) * k + c[4] * off, sz, sz);
      }
      ctx.globalAlpha = 1;
      if (now - start < end) { requestAnimationFrame(frame); return; }
      brand.classList.remove("booting");
      setTimeout(function () { cv.remove(); running = false; }, 30);
    }
    requestAnimationFrame(frame);
  }
  if (window.scrollY < 40) {
    if (brand && !reduced && B) brand.classList.add("booting");
    window.addEventListener("load", function () { setTimeout(little, DELAY); });
  }
  var lockup = document.getElementById("lockup");
  if (lockup) lockup.addEventListener("click", function () { window.scrollTo({ top: 0 }); little(); });

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

  /* ---- the hero: what ChaOS adds, one screen and one line at a time ---- */
  var hero = document.querySelector(".hero-device img"), cap = document.querySelector(".hero-cap");
  if (hero && cap && !reduced) {
    var cycle = [
      ["home-pixelnames", "Pixel Names, ChaOS's own theme"],
      ["ps-shelf", "PlayStation, discs and all"],
      ["chaos-shader", "CRT, LCD and PT shaders you can tune"],
      ["pm-list", "PC games through PortMaster"],
      ["pico8-shelf", "PICO-8, with your own copy"],
      ["cheats", "Cheats, chosen from a list"],
      ["romm", "Your RomM library, on the Brick"],
      ["sync", "Saves in step with Syncthing"],
      ["shader-gb-speed", "A speed check for every look"],
      ["home-pixelcards", "Pixel Cards, ChaOS's other theme"]
    ], at = 0;
    cycle.forEach(function (s) { var p = new Image(); p.src = "img/" + s[0] + ".webp"; });
    /* A crossfade: the next screen fades in over the one showing, then takes
     * its place, so the picture never dips to black between screens. */
    var bez = hero.parentNode, top = hero.cloneNode(false);
    top.className = "hero-next"; top.alt = ""; top.setAttribute("aria-hidden", "true");
    bez.appendChild(top);
    setInterval(function () {
      if (document.hidden) return;
      at = (at + 1) % cycle.length;
      var s = cycle[at];
      cap.style.opacity = "0";
      top.onload = function () {
        top.classList.add("in");
        setTimeout(function () { cap.textContent = s[1]; cap.style.opacity = "1"; }, 450);
        setTimeout(function () {
          hero.src = top.src; hero.alt = s[1] + ", on the Brick";
          hero.onload = function () { top.classList.remove("in"); };
        }, 1000);
      };
      top.src = "img/" + s[0] + ".webp";
    }, 4800);
  }
})();
