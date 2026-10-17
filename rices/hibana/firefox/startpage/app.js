// hibana start page: the clock, the greeting, the date in English and in Japanese, the search
// box (DuckDuckGo, or an address straight away), number keys for the cards, the weather (rain
// slanting down over the wallpaper and a few embers drifting up from the fires), and the cat:
// pat it and it presses down, its eyes flare and it throws a burst of sparks; click the night and
// a few sparks fly from there. The weather and the sparks are two canvases drawn with
// requestAnimationFrame, the weather at 30 frames a second with at most 40 particles and the
// sparks only while there are some; both stop while the tab is hidden, and with
// prefers-reduced-motion there is no weather and the sparks only glow where they are.
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

function spark() {
  const svg = document.createElementNS(SVG, "svg");
  svg.setAttribute("aria-hidden", "true");
  const use = document.createElementNS(SVG, "use");
  use.setAttribute("href", "#spark");
  svg.append(use);
  return svg;
}

// The date in Japanese, written the way a calendar writes it (十月十七日 金曜日), and the day's
// forecast after it, in the style of a weather report: "rain, then sparks", "rain, now and
// then sparks", "sparks on ash".
const DIGITS = "〇一二三四五六七八九";
const YOUBI = "日月火水木金土";
const FORECASTS = ["雨のち火花", "雨ときどき火花", "灰に火花"];

function kanji(n) {
  if (n < 10) return DIGITS[n];
  const tens = Math.floor(n / 10);
  const ones = n % 10;
  return (tens > 1 ? DIGITS[tens] : "") + "十" + (ones ? DIGITS[ones] : "");
}

function japanese(now) {
  const date = `${kanji(now.getMonth() + 1)}月${kanji(now.getDate())}日 ${YOUBI[now.getDay()]}曜日`;
  const forecast = document.createElement("b");
  forecast.textContent = FORECASTS[dayOfYear(now) % FORECASTS.length];
  $("wa").replaceChildren(date, spark(), forecast);
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

    $("date").replaceChildren(weekday.format(now), spark(), dayMonth.format(now));
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

// The page shows the rice's wallpaper from ../../wallpaper/; where it is missing, hibana's own
// rainy night stands in, without the veil that dims a picture.
const wallpaper = new Image();
wallpaper.addEventListener("error", () => document.documentElement.classList.add("no-wallpaper"));
wallpaper.src = "../../wallpaper/hibana.jpg";

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
// enough for streaks of light) and draw on it in CSS pixels.
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

// A soft round glow, drawn once and stamped wherever an ember is.
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

// ------------------------------------------------------------------ the weather

// Rain falls 15 degrees off the vertical, down and to the left (the -75deg of the rice's rain
// texture): for every pixel down, SLANT pixels left. Three layers of it, fainter, shorter and
// slower the further off, and a few embers rising from the fires below, wavering, cooling from
// flare to blood red as they go, and gone before they reach the clouds.
const SLANT = 0.27;
const RAIN = [
  { count: 14, alpha: 0.10, width: 1, length: [10, 18], speed: [520, 660] },
  { count: 12, alpha: 0.15, width: 1, length: [16, 26], speed: [700, 860] },
  { count: 8, alpha: 0.22, width: 1.3, length: [26, 40], speed: [940, 1150] },
];
const EMBERS = 6;
const HOT = glowSprite([[0, "rgba(255, 240, 220, 1)"], [0.22, "rgba(255, 176, 112, 0.9)"],
                        [0.5, "rgba(255, 122, 51, 0.3)"], [1, "rgba(255, 122, 51, 0)"]]);
const COOL = glowSprite([[0, "rgba(229, 89, 77, 0.9)"], [0.3, "rgba(200, 68, 60, 0.5)"],
                         [1, "rgba(200, 68, 60, 0)"]]);

const weather = { canvas: $("weather"), context: null, drops: [], embers: [], frame: 0, last: 0 };

function dropRain(drop, anywhere) {
  const layer = RAIN[drop.layer];
  drop.length = random(...layer.length);
  drop.speed = random(...layer.speed);
  drop.x = random(0, innerWidth + innerHeight * SLANT);
  drop.y = anywhere ? random(0, innerHeight) : -drop.length - random(0, innerHeight * 0.4);
}

function lightEmber(ember, anywhere) {
  ember.x = random(0, innerWidth);
  ember.y = anywhere ? random(innerHeight * 0.45, innerHeight) : innerHeight + 12;
  ember.rise = random(16, 38);
  ember.sway = random(6, 16);
  ember.phase = random(0, Math.PI * 2);
  ember.size = random(5, 11);
  ember.life = random(5, 10);
  ember.age = anywhere ? random(0, ember.life * 0.5) : 0;
}

function populate() {
  weather.drops = [];
  RAIN.forEach((layer, index) => {
    for (let i = 0; i < layer.count; i++) {
      const drop = { layer: index };
      dropRain(drop, true);
      weather.drops.push(drop);
    }
  });
  weather.embers = [];
  for (let i = 0; i < EMBERS; i++) {
    const ember = {};
    lightEmber(ember, true);
    weather.embers.push(ember);
  }
}

function stepWeather(dt) {
  for (const drop of weather.drops) {
    drop.y += drop.speed * dt;
    drop.x -= drop.speed * SLANT * dt;
    if (drop.y - drop.length > innerHeight || drop.x < -drop.length) dropRain(drop, false);
  }
  for (const ember of weather.embers) {
    ember.age += dt;
    ember.y -= ember.rise * dt;
    ember.x += (Math.sin(ember.age * 1.3 + ember.phase) * ember.sway - 6) * dt;
    if (ember.age >= ember.life || ember.y < -20) lightEmber(ember, false);
  }
}

function drawWeather() {
  const context = weather.context;
  context.clearRect(0, 0, innerWidth, innerHeight);
  // each layer of rain in one stroke: a streak from the drop up and to the right, where it fell from
  RAIN.forEach((layer, index) => {
    context.beginPath();
    for (const drop of weather.drops) {
      if (drop.layer !== index) continue;
      context.moveTo(drop.x, drop.y);
      context.lineTo(drop.x + drop.length * SLANT, drop.y - drop.length);
    }
    context.strokeStyle = `rgba(159, 179, 204, ${layer.alpha})`;
    context.lineWidth = layer.width;
    context.stroke();
  });
  // embers: a red glow fading out under a white-hot one that cools first
  for (const ember of weather.embers) {
    const heat = 1 - ember.age / ember.life;
    const flicker = 0.75 + 0.25 * Math.sin(ember.age * 9 + ember.phase * 3);
    const fade = Math.min(1, ember.age * 2);                  // kindle rather than pop in
    const size = ember.size;
    context.globalAlpha = Math.min(1, heat * 2) * flicker * fade * 0.8;
    context.drawImage(COOL, ember.x - size, ember.y - size, size * 2, size * 2);
    context.globalAlpha = heat * heat * flicker * fade;
    context.drawImage(HOT, ember.x - size / 2, ember.y - size / 2, size, size);
  }
  context.globalAlpha = 1;
}

function weatherFrame(now) {
  weather.frame = requestAnimationFrame(weatherFrame);
  const elapsed = now - weather.last;
  if (elapsed < 1000 / 30 - 2) return;                    // 30 frames a second is plenty for rain
  weather.last = now;
  stepWeather(Math.min(elapsed, 100) / 1000);
  drawWeather();
}

function startWeather() {
  if (weather.frame || reduced.matches || document.hidden) return;
  weather.context = fit(weather.canvas);
  if (!weather.drops.length) populate();
  weather.last = performance.now();
  weather.frame = requestAnimationFrame(weatherFrame);
}

function stopWeather() {
  cancelAnimationFrame(weather.frame);
  weather.frame = 0;
}

// ------------------------------------------------------------------ sparks

// A burst flung up from a point: each spark a short streak along its path, white-hot at first,
// then flare, ember and blood red as it falls and fades. No more than MAX_SPARKS at a time, the
// oldest going first; the loop runs only while there are any.
const MAX_SPARKS = 24;
const GRAVITY = 1100;
const HEAT = [[1, [255, 244, 224]], [0.7, [255, 176, 112]], [0.4, [255, 122, 51]], [0.15, [200, 68, 60]], [0, [200, 68, 60]]];

const sparks = { canvas: $("sparks"), context: null, list: [], frame: 0, last: 0 };

function colourAt(heat) {
  for (let i = 1; i < HEAT.length; i++) {
    const [at, colour] = HEAT[i];
    if (heat >= at) {
      const [above, hot] = HEAT[i - 1];
      const t = (heat - at) / (above - at);
      return colour.map((channel, c) => Math.round(channel + (hot[c] - channel) * t));
    }
  }
  return HEAT[HEAT.length - 1][1];
}

function fling(x, y, count, power) {
  // with reduced motion they barely move and do not fall: a crackle where they start
  const still = reduced.matches;
  for (let i = 0; i < count; i++) {
    if (sparks.list.length >= MAX_SPARKS) sparks.list.shift();
    const angle = -Math.PI / 2 + random(-1.15, 1.15);        // upwards, 65 degrees either side
    const speed = power * random(0.45, 1) * (still ? 0.1 : 1);
    sparks.list.push({
      x: x + random(-4, 4),
      y: y + random(-3, 3),
      vx: Math.cos(angle) * speed,
      vy: Math.sin(angle) * speed,
      age: 0,
      life: still ? 0.45 : random(0.55, 1.15),
      width: random(1, 1.9),
      gravity: still ? 0 : GRAVITY,
    });
  }
  if (!sparks.frame) {
    sparks.context = fit(sparks.canvas);
    sparks.last = performance.now();
    sparks.frame = requestAnimationFrame(sparkFrame);
  }
}

function streak(context, from, to, width) {
  context.lineWidth = width;
  context.beginPath();
  context.moveTo(from[0], from[1]);
  context.lineTo(to[0], to[1]);
  context.stroke();
}

function sparkFrame(now) {
  const dt = Math.min(now - sparks.last, 50) / 1000;
  sparks.last = now;
  const context = sparks.context;
  context.clearRect(0, 0, innerWidth, innerHeight);
  context.globalCompositeOperation = "lighter";
  context.lineCap = "round";
  const drag = Math.exp(-1.6 * dt);
  sparks.list = sparks.list.filter((spark) => (spark.age += dt) < spark.life);
  for (const spark of sparks.list) {
    spark.vx *= drag;
    spark.vy = spark.vy * drag + spark.gravity * dt;
    spark.x += spark.vx * dt;
    spark.y += spark.vy * dt;
    const heat = 1 - spark.age / spark.life;
    const alpha = Math.min(1, heat * 1.8);
    const [r, g, b] = colourAt(heat);
    const head = [spark.x, spark.y];
    const tail = [spark.x - spark.vx * 0.028, spark.y - spark.vy * 0.028];   // where it was a moment ago
    context.strokeStyle = `rgba(255, 122, 51, ${(alpha * 0.22).toFixed(3)})`;  // its glow
    streak(context, tail, head, spark.width * 4);
    context.strokeStyle = `rgba(${r}, ${g}, ${b}, ${alpha.toFixed(3)})`;
    streak(context, tail, head, spark.width * (0.5 + heat));
  }
  if (sparks.list.length) {
    sparks.frame = requestAnimationFrame(sparkFrame);
  } else {
    context.clearRect(0, 0, innerWidth, innerHeight);
    sparks.frame = 0;
  }
}

function stopSparks() {
  cancelAnimationFrame(sparks.frame);
  sparks.frame = 0;
  sparks.list = [];
  if (sparks.context) sparks.context.clearRect(0, 0, innerWidth, innerHeight);
}

// ------------------------------------------------------------------ running them

addEventListener("resize", () => {
  if (weather.frame) weather.context = fit(weather.canvas);
  if (sparks.frame) sparks.context = fit(sparks.canvas);
});

// A hidden tab draws nothing at all; the weather picks up where it was when it comes back.
document.addEventListener("visibilitychange", () => {
  if (document.hidden) {
    stopWeather();
    stopSparks();
  } else {
    startWeather();
  }
});

reduced.addEventListener("change", () => {
  if (reduced.matches) {
    stopWeather();
    weather.canvas.getContext("2d").clearRect(0, 0, weather.canvas.width, weather.canvas.height);
  } else {
    startWeather();
  }
});

startWeather();

// Click the night (anywhere that is not a link, a button or the search card) and a few sparks
// fly from there, as from a struck flint.
document.addEventListener("pointerdown", (event) => {
  if (event.button !== 0) return;
  if (event.target instanceof Element && event.target.closest("a, button, input, form, kbd")) return;
  fling(event.clientX, event.clientY, 6, 380);
});

// ------------------------------------------------------------------ the cat

const mascot = $("mascot");
const picture = $("mascot-picture");
const purr = $("purr");
const SAYS = ["nya.", "mrrp?", "purr…", "mrow.", "hmph.", "nyaa~"];

// No picture (the file is missing): no cat, and the search card sits where it would anyway.
function hideCat() { mascot.hidden = true; }
picture.addEventListener("error", hideCat);
if (picture.complete && picture.naturalWidth === 0) hideCat();

let said = -1;
let unlit = 0;
mascot.addEventListener("click", (event) => {
  mascot.classList.remove("squish", "lit");
  purr.classList.remove("show");
  void mascot.offsetWidth;                    // restart the animations on a quick second pat
  mascot.classList.add("squish", "lit");
  clearTimeout(unlit);
  unlit = setTimeout(() => mascot.classList.remove("lit"), 1300);
  let say;
  do { say = Math.floor(Math.random() * SAYS.length); } while (say === said && SAYS.length > 1);
  said = say;
  purr.textContent = SAYS[say];
  purr.classList.add("show");
  // the sparks fly from where it was patted; from the keyboard, from the top of its head
  const box = mascot.getBoundingClientRect();
  const x = event.detail ? event.clientX : box.left + box.width * 0.45;
  const y = event.detail ? event.clientY : box.top + box.height * 0.25;
  fling(x, y, 16, 560);
});
mascot.addEventListener("animationend", (event) => {
  if (event.animationName === "squish") mascot.classList.remove("squish");
});
