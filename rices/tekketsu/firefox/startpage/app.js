// tekketsu start page: the clock, the greeting, the date in English and in Japanese, the search
// box (DuckDuckGo, or an address straight away), number keys for the cards, the summoning
// circle's rim of marks, the ash (flakes of it falling past the wallpaper, turning as they go,
// a few cinders among them dying out), and the sigils: click the dark and a small circle is drawn
// there, a star turning in it, and gives off a few motes of blood-red light as it fades, while
// the big circle's marks flare. The ash and the sigils are two canvases drawn with
// requestAnimationFrame, the ash at 30 frames a second with at most 44 flakes and the sigils
// only while there are some; both stop while the tab is hidden, and with prefers-reduced-motion
// there is no ash and a sigil only glows where it is drawn.
// No network requests; nothing is stored.
// SPDX-License-Identifier: GPL-3.0-or-later
"use strict";

const NAME = "";                                    // "good evening, NAME." when set
const SEARCH_URL = "https://duckduckgo.com/?q=";    // the query is appended, encoded
const HOUR12 = false;                                // true for a 12-hour clock

const $ = (id) => document.getElementById(id);
const SVG = "http://www.w3.org/2000/svg";

// ------------------------------------------------------------------ clock, greeting, date

function greeting(hour) {
  if (hour < 5) return "good night";
  if (hour < 12) return "good morning";
  if (hour < 18) return "good afternoon";
  if (hour < 22) return "good evening";
  return "good night";
}

function isoWeek(date) {
  const d = new Date(Date.UTC(date.getFullYear(), date.getMonth(), date.getDate()));
  const day = d.getUTCDay() || 7;
  d.setUTCDate(d.getUTCDate() + 4 - day);
  const yearStart = new Date(Date.UTC(d.getUTCFullYear(), 0, 1));
  return Math.ceil(((d - yearStart) / 86400000 + 1) / 7);
}

function dayOfYear(date) {
  const start = new Date(date.getFullYear(), 0, 0);
  return Math.floor((date - start) / 86400000);
}

function daysInYear(year) {
  return new Date(year, 1, 29).getMonth() === 1 ? 366 : 365;
}

const weekday = new Intl.DateTimeFormat("en-GB", { weekday: "long" });
const dayMonth = new Intl.DateTimeFormat("en-GB", { day: "numeric", month: "long" });

function thorn() {
  const svg = document.createElementNS(SVG, "svg");
  svg.setAttribute("aria-hidden", "true");
  const use = document.createElementNS(SVG, "use");
  use.setAttribute("href", "#thorn");
  svg.append(use);
  return svg;
}

// The date in Japanese, written the way a calendar writes it (十月二十七日 火曜日), and the day's
// omen after it, in the style of a weather report: "ash, then blood", "ash, now and then blood",
// "iron and blood".
const DIGITS = "〇一二三四五六七八九";
const YOUBI = "日月火水木金土";
const OMENS = ["灰のち血", "灰ときどき血", "鉄と血"];

function kanji(n) {
  if (n < 10) return DIGITS[n];
  const tens = Math.floor(n / 10);
  const ones = n % 10;
  return (tens > 1 ? DIGITS[tens] : "") + "十" + (ones ? DIGITS[ones] : "");
}

function japanese(now) {
  const date = `${kanji(now.getMonth() + 1)}月${kanji(now.getDate())}日 ${YOUBI[now.getDay()]}曜日`;
  const omen = document.createElement("b");
  omen.textContent = OMENS[dayOfYear(now) % OMENS.length];
  $("wa").replaceChildren(date, thorn(), omen);
}

let shownMinute = -1;

function tick() {
  const now = new Date();
  const minute = now.getHours() * 60 + now.getMinutes();
  if (minute !== shownMinute) {
    shownMinute = minute;
    let hours = now.getHours();
    if (HOUR12) hours = hours % 12 || 12;
    $("hours").textContent = String(hours).padStart(2, "0");
    $("minutes").textContent = String(now.getMinutes()).padStart(2, "0");

    const hello = $("greeting");
    hello.textContent = greeting(now.getHours());
    if (NAME) {
      hello.append(", ");
      const name = document.createElement("b");
      name.textContent = NAME;
      hello.append(name);
    }
    hello.append(".");

    $("date").replaceChildren(weekday.format(now), thorn(), dayMonth.format(now));
    japanese(now);

    const week = document.createElement("b");
    week.textContent = "w" + isoWeek(now);
    $("almanac").replaceChildren(week,
      ` · day ${dayOfYear(now)} of ${daysInYear(now.getFullYear())} · ${now.getFullYear()}`);
  }
  setTimeout(tick, 1000 - (Date.now() % 1000) + 5);
}

tick();

// ------------------------------------------------------------------ the wallpaper

// The page shows the rice's wallpaper from ../../wallpaper/; where it is missing, tekketsu's own
// black iron stands in, without the veil that dims a picture.
const wallpaper = new Image();
wallpaper.addEventListener("error", () => document.documentElement.classList.add("no-wallpaper"));
wallpaper.src = "../../wallpaper/tekketsu.jpg";

// ------------------------------------------------------------------ the circle

// Its rim between the two outer rings carries RUNES marks, upright to the outside, the eight
// glyphs in a fixed order that never repeats a neighbour.
const RUNES = 36;
const circle = $("circle");
const rim = $("runes");
for (let i = 0; i < RUNES; i++) {
  const use = document.createElementNS(SVG, "use");
  use.setAttribute("href", `#rune${(i * 5 + 3) % 8}`);
  use.setAttribute("width", "5");
  use.setAttribute("height", "7");
  use.setAttribute("transform", `rotate(${(360 / RUNES) * i}) translate(-2.5 -89.5)`);
  rim.append(use);
}

let cooled = 0;
function stir() {
  circle.classList.remove("flare");
  void circle.getBoundingClientRect();        // restart the burn on a quick second stir
  circle.classList.add("flare");
  clearTimeout(cooled);
  cooled = setTimeout(() => circle.classList.remove("flare"), 1800);
}

// ------------------------------------------------------------------ search

const form = $("search");
const query = $("q");

// An address goes straight there; anything else is a search.
function asAddress(text) {
  if (/\s/.test(text)) return null;
  if (/^[a-z][a-z0-9+.-]*:\/\//i.test(text) || /^(about|file):/i.test(text)) return text;
  if (/^localhost(:\d+)?(\/.*)?$/i.test(text)) return "http://" + text;
  if (/^[\w-]+(\.[\w-]+)*\.[a-z]{2,}(:\d+)?(\/.*)?$/i.test(text)) return "https://" + text;
  return null;
}

form.addEventListener("submit", (event) => {
  event.preventDefault();
  const text = query.value.trim();
  if (!text) return;
  location.href = asAddress(text) ?? SEARCH_URL + encodeURIComponent(text);
});

// ------------------------------------------------------------------ keys

const cards = [...document.querySelectorAll(".card")];

document.addEventListener("keydown", (event) => {
  if (event.ctrlKey || event.altKey || event.metaKey) return;
  const typing = event.target === query;
  if (event.key === "Escape" && typing) {
    query.value = "";
    query.blur();
    return;
  }
  if (typing) return;
  if (event.key === "/") {
    event.preventDefault();
    query.focus();
  } else if (/^[1-9]$/.test(event.key) && cards[event.key - 1]) {
    cards[event.key - 1].click();
  } else if (event.key === "Shift") {
    document.body.classList.add("keys");      // hold Shift to see the card numbers
  }
});

document.addEventListener("keyup", (event) => {
  if (event.key === "Shift") document.body.classList.remove("keys");
});

// ------------------------------------------------------------------ canvases

const reduced = matchMedia("(prefers-reduced-motion: reduce)");

// Size a canvas to the window in device pixels (at most two per CSS pixel, which is sharp
// enough for flakes and thin lines) and draw on it in CSS pixels.
function fit(canvas) {
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  const width = Math.round(innerWidth * ratio);
  const height = Math.round(innerHeight * ratio);
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  const context = canvas.getContext("2d");
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  return context;
}

// A soft round glow, drawn once and stamped wherever a cinder or a mote is.
function glowSprite(stops) {
  const sprite = document.createElement("canvas");
  sprite.width = sprite.height = 32;
  const context = sprite.getContext("2d");
  const gradient = context.createRadialGradient(16, 16, 0, 16, 16, 16);
  for (const [at, colour] of stops) gradient.addColorStop(at, colour);
  context.fillStyle = gradient;
  context.fillRect(0, 0, 32, 32);
  return sprite;
}

const random = (low, high) => low + Math.random() * (high - low);

const CINDER = glowSprite([[0, "rgba(255, 214, 196, 1)"], [0.2, "rgba(255, 106, 92, 0.9)"],
                           [0.5, "rgba(196, 43, 35, 0.35)"], [1, "rgba(196, 43, 35, 0)"]]);

// ------------------------------------------------------------------ the ash

// Flakes of ash in three layers, smaller, fainter and slower the further off, falling and
// drifting on a slow wind, each an irregular little shard that turns as it falls; and a few
// cinders among them, red at the heart, that fall more slowly and go out before they land.
const LAYERS = [
  { count: 18, alpha: 0.22, size: [1, 1.8], fall: [14, 24] },
  { count: 14, alpha: 0.32, size: [1.6, 2.6], fall: [22, 36] },
  { count: 8, alpha: 0.45, size: [2.4, 3.8], fall: [34, 52] },
];
const CINDERS = 4;
const ASH_COLOURS = ["142, 130, 125", "171, 159, 144", "107, 96, 94", "203, 191, 168"];

const ash = { canvas: $("ash"), context: null, flakes: [], cinders: [], frame: 0, last: 0, time: 0 };

function shapeFlake(flake) {
  // four or five points round a centre, each at its own distance: a shard, not a dot
  const points = 4 + Math.floor(Math.random() * 2);
  flake.shape = [];
  for (let i = 0; i < points; i++) {
    const angle = (i / points) * Math.PI * 2 + random(-0.4, 0.4);
    flake.shape.push([Math.cos(angle) * random(0.5, 1.1), Math.sin(angle) * random(0.5, 1.1)]);
  }
}

function dropFlake(flake, anywhere) {
  const layer = LAYERS[flake.layer];
  flake.size = random(...layer.size);
  flake.fall = random(...layer.fall);
  flake.sway = random(4, 14);
  flake.phase = random(0, Math.PI * 2);
  flake.spin = random(-1.6, 1.6);
  flake.angle = random(0, Math.PI * 2);
  flake.colour = ASH_COLOURS[Math.floor(Math.random() * ASH_COLOURS.length)];
  flake.x = random(-20, innerWidth + 20);
  flake.y = anywhere ? random(0, innerHeight) : -10;
  shapeFlake(flake);
}

function lightCinder(cinder, anywhere) {
  cinder.x = random(0, innerWidth);
  cinder.y = anywhere ? random(0, innerHeight * 0.6) : -10;
  cinder.fall = random(10, 22);
  cinder.sway = random(6, 14);
  cinder.phase = random(0, Math.PI * 2);
  cinder.size = random(4, 8);
  cinder.life = random(6, 12);
  cinder.age = anywhere ? random(0, cinder.life * 0.5) : 0;
}

function populate() {
  ash.flakes = [];
  LAYERS.forEach((layer, index) => {
    for (let i = 0; i < layer.count; i++) {
      const flake = { layer: index };
      dropFlake(flake, true);
      ash.flakes.push(flake);
    }
  });
  ash.cinders = [];
  for (let i = 0; i < CINDERS; i++) {
    const cinder = {};
    lightCinder(cinder, true);
    ash.cinders.push(cinder);
  }
}

function stepAsh(dt) {
  ash.time += dt;
  // the wind: a slow breath to the right and back
  const wind = 8 + Math.sin(ash.time * 0.13) * 10;
  for (const flake of ash.flakes) {
    const depth = 0.5 + flake.layer * 0.35;
    flake.y += flake.fall * dt;
    flake.x += (wind * depth + Math.sin(ash.time * 0.9 + flake.phase) * flake.sway) * dt;
    flake.angle += flake.spin * dt;
    if (flake.y > innerHeight + 10 || flake.x > innerWidth + 30 || flake.x < -30) dropFlake(flake, false);
  }
  for (const cinder of ash.cinders) {
    cinder.age += dt;
    cinder.y += cinder.fall * dt;
    cinder.x += (wind * 0.6 + Math.sin(cinder.age * 0.8 + cinder.phase) * cinder.sway) * dt;
    if (cinder.age >= cinder.life || cinder.y > innerHeight + 10) lightCinder(cinder, false);
  }
}

function drawAsh() {
  const context = ash.context;
  context.clearRect(0, 0, innerWidth, innerHeight);
  for (const flake of ash.flakes) {
    const cos = Math.cos(flake.angle) * flake.size;
    const sin = Math.sin(flake.angle) * flake.size;
    context.beginPath();
    flake.shape.forEach(([px, py], index) => {
      const x = flake.x + px * cos - py * sin;
      const y = flake.y + px * sin + py * cos;
      if (index) context.lineTo(x, y); else context.moveTo(x, y);
    });
    context.closePath();
    context.fillStyle = `rgba(${flake.colour}, ${LAYERS[flake.layer].alpha})`;
    context.fill();
  }
  // cinders: kindle, glow with a slow flicker, and go grey as they die
  for (const cinder of ash.cinders) {
    const life = cinder.age / cinder.life;
    const heat = Math.min(1, cinder.age * 1.5) * (1 - life);
    const flicker = 0.75 + 0.25 * Math.sin(cinder.age * 7 + cinder.phase * 3);
    context.globalAlpha = heat * flicker;
    context.drawImage(CINDER, cinder.x - cinder.size, cinder.y - cinder.size, cinder.size * 2, cinder.size * 2);
  }
  context.globalAlpha = 1;
}

function ashFrame(now) {
  ash.frame = requestAnimationFrame(ashFrame);
  const elapsed = now - ash.last;
  if (elapsed < 1000 / 30 - 2) return;                    // 30 frames a second is plenty for ash
  ash.last = now;
  stepAsh(Math.min(elapsed, 100) / 1000);
  drawAsh();
}

function startAsh() {
  if (ash.frame || reduced.matches || document.hidden) return;
  ash.context = fit(ash.canvas);
  if (!ash.flakes.length) populate();
  ash.last = performance.now();
  ash.frame = requestAnimationFrame(ashFrame);
}

function stopAsh() {
  cancelAnimationFrame(ash.frame);
  ash.frame = 0;
}

// ------------------------------------------------------------------ sigils

// A sigil is a small circle drawn where the dark was clicked: its ring opens out, a five-pointed
// star turning in it, then it fades, giving off motes of red light that drift up and go out. At
// most MAX_SIGILS at a time and MAX_MOTES motes, the oldest going first; the loop runs only while
// there are any.
const MAX_SIGILS = 4;
const MAX_MOTES = 28;

const sigils = { canvas: $("sigils"), context: null, list: [], motes: [], frame: 0, last: 0 };

function startSigils() {
  if (!sigils.frame) {
    sigils.context = fit(sigils.canvas);
    sigils.last = performance.now();
    sigils.frame = requestAnimationFrame(sigilFrame);
  }
}

// Motes rising from a point: slow, wavering, fading from bright to blood as they go.
function motes(x, y, count, spread) {
  const still = reduced.matches;
  for (let i = 0; i < count; i++) {
    if (sigils.motes.length >= MAX_MOTES) sigils.motes.shift();
    sigils.motes.push({
      x: x + random(-spread, spread),
      y: y + random(-spread * 0.4, spread * 0.4),
      rise: still ? 0 : random(24, 70),
      sway: still ? 0 : random(6, 18),
      phase: random(0, Math.PI * 2),
      size: random(3, 7),
      age: 0,
      life: still ? 0.6 : random(0.9, 1.8),
    });
  }
  startSigils();
}

function draw(x, y, radius) {
  if (sigils.list.length >= MAX_SIGILS) sigils.list.shift();
  sigils.list.push({ x, y, radius, spin: random(0, Math.PI * 2), age: 0, life: reduced.matches ? 0.6 : 1.1 });
  motes(x, y, 5, radius * 0.5);
}

function sigilFrame(now) {
  const dt = Math.min(now - sigils.last, 50) / 1000;
  sigils.last = now;
  const context = sigils.context;
  context.clearRect(0, 0, innerWidth, innerHeight);
  const still = reduced.matches;

  sigils.list = sigils.list.filter((sigil) => (sigil.age += dt) < sigil.life);
  context.lineCap = "round";
  for (const sigil of sigils.list) {
    const t = sigil.age / sigil.life;
    const open = still ? 1 : 1 - Math.pow(1 - Math.min(1, t * 2.2), 3);   // eases out to full size
    const r = sigil.radius * open;
    const alpha = t < 0.15 ? t / 0.15 : 1 - (t - 0.15) / 0.85;
    const spin = sigil.spin + (still ? 0 : t * 1.4);
    context.save();
    context.translate(sigil.x, sigil.y);
    context.globalAlpha = alpha;
    context.shadowColor = "rgba(196, 43, 35, 0.8)";
    context.shadowBlur = 10;
    context.strokeStyle = "rgb(229, 72, 61)";
    context.lineWidth = 1.4;
    context.beginPath();
    context.arc(0, 0, r, 0, Math.PI * 2);
    context.stroke();
    context.lineWidth = 0.8;
    context.beginPath();
    context.arc(0, 0, r * 0.84, 0, Math.PI * 2);
    context.stroke();
    // the star: every second of five points round the inner ring
    context.beginPath();
    for (let k = 0; k <= 5; k++) {
      const angle = spin - Math.PI / 2 + (k * 4 * Math.PI) / 5;
      const px = Math.cos(angle) * r * 0.84;
      const py = Math.sin(angle) * r * 0.84;
      if (k) context.lineTo(px, py); else context.moveTo(px, py);
    }
    context.strokeStyle = "rgb(196, 43, 35)";
    context.lineWidth = 1.1;
    context.stroke();
    // marks between the rings
    context.strokeStyle = "rgba(228, 219, 196, 0.7)";
    context.lineWidth = 0.8;
    context.beginPath();
    for (let k = 0; k < 16; k++) {
      const angle = -spin * 0.5 + (k * Math.PI) / 8;
      context.moveTo(Math.cos(angle) * r * 0.88, Math.sin(angle) * r * 0.88);
      context.lineTo(Math.cos(angle) * r * 0.96, Math.sin(angle) * r * 0.96);
    }
    context.stroke();
    context.restore();
  }

  context.globalCompositeOperation = "lighter";
  sigils.motes = sigils.motes.filter((mote) => (mote.age += dt) < mote.life);
  for (const mote of sigils.motes) {
    mote.y -= mote.rise * dt;
    mote.x += Math.sin(mote.age * 3 + mote.phase) * mote.sway * dt;
    const t = mote.age / mote.life;
    context.globalAlpha = (t < 0.2 ? t / 0.2 : 1 - (t - 0.2) / 0.8) * 0.9;
    context.drawImage(CINDER, mote.x - mote.size, mote.y - mote.size, mote.size * 2, mote.size * 2);
  }
  context.globalAlpha = 1;
  context.globalCompositeOperation = "source-over";

  if (sigils.list.length || sigils.motes.length) {
    sigils.frame = requestAnimationFrame(sigilFrame);
  } else {
    context.clearRect(0, 0, innerWidth, innerHeight);
    sigils.frame = 0;
  }
}

function stopSigils() {
  cancelAnimationFrame(sigils.frame);
  sigils.frame = 0;
  sigils.list = [];
  sigils.motes = [];
  if (sigils.context) sigils.context.clearRect(0, 0, innerWidth, innerHeight);
}

// ------------------------------------------------------------------ running them

addEventListener("resize", () => {
  if (ash.frame) ash.context = fit(ash.canvas);
  if (sigils.frame) sigils.context = fit(sigils.canvas);
});

// A hidden tab draws nothing at all; the ash picks up where it was when it comes back.
document.addEventListener("visibilitychange", () => {
  if (document.hidden) {
    stopAsh();
    stopSigils();
  } else {
    startAsh();
  }
});

reduced.addEventListener("change", () => {
  if (reduced.matches) {
    stopAsh();
    ash.canvas.getContext("2d").clearRect(0, 0, ash.canvas.width, ash.canvas.height);
  } else {
    startAsh();
  }
});

startAsh();

// Click the dark (anywhere that is not a link, a button or the search card) and a small circle
// is drawn there, and the big one's marks flare.
document.addEventListener("pointerdown", (event) => {
  if (event.button !== 0) return;
  if (event.target instanceof Element && event.target.closest("a, button, input, form, kbd")) return;
  draw(event.clientX, event.clientY, 34);
  stir();
});
