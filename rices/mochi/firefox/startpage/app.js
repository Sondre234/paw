// mochi start page: the clock, the greeting and the date, the search box (DuckDuckGo, or an
// address straight away), number keys for the cards, and the cat: pat mochi and it squishes
// like the rice cake it is named after and says something; click the night and hearts float up.
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

function sparkle() {
  const svg = document.createElementNS(SVG, "svg");
  svg.setAttribute("aria-hidden", "true");
  const use = document.createElementNS(SVG, "use");
  use.setAttribute("href", "#sparkle");
  svg.append(use);
  return svg;
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

    $("date").replaceChildren(weekday.format(now), sparkle(), dayMonth.format(now));

    const week = document.createElement("b");
    week.textContent = "w" + isoWeek(now);
    $("almanac").replaceChildren(week,
      ` · day ${dayOfYear(now)} of ${daysInYear(now.getFullYear())} · ${now.getFullYear()}`);
  }
  setTimeout(tick, 1000 - (Date.now() % 1000) + 5);
}

tick();

// ------------------------------------------------------------------ the wallpaper

// The page shows the rice's wallpaper from ../../wallpaper/; where it is missing, Mocha's own
// night stands in, without the veil that dims a picture.
const wallpaper = new Image();
wallpaper.addEventListener("error", () => document.documentElement.classList.add("no-wallpaper"));
wallpaper.src = "../../wallpaper/mochi.jpg";

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

// ------------------------------------------------------------------ hearts

const hearts = $("hearts");
const COLOURS = ["#F5C2E7", "#CBA6F7", "#B4BEFE", "#89B4FA", "#F2CDCD"];  // pink, mauve, lavender, blue, flamingo
const reduced = matchMedia("(prefers-reduced-motion: reduce)");

function heart(x, y, delay) {
  const svg = document.createElementNS(SVG, "svg");
  svg.setAttribute("class", "heart");
  const use = document.createElementNS(SVG, "use");
  use.setAttribute("href", "#heart");
  svg.append(use);
  const size = 12 + Math.random() * 14;
  svg.style.left = `${x}px`;
  svg.style.top = `${y}px`;
  svg.style.setProperty("--size", `${size.toFixed(1)}px`);
  svg.style.setProperty("--colour", COLOURS[Math.floor(Math.random() * COLOURS.length)]);
  svg.style.setProperty("--drift", `${(Math.random() * 80 - 40).toFixed(0)}px`);
  svg.style.setProperty("--rise", `${(-90 - Math.random() * 110).toFixed(0)}px`);
  svg.style.setProperty("--tilt", `${(Math.random() * 30 - 15).toFixed(0)}deg`);
  svg.style.setProperty("--time", `${(1.6 + Math.random() * 1.2).toFixed(2)}s`);
  svg.style.animationDelay = `${delay}ms`;
  svg.addEventListener("animationend", () => svg.remove());
  hearts.append(svg);
}

function burst(x, y, count) {
  for (let i = 0; i < count; i++) {
    heart(x + Math.random() * 24 - 12, y + Math.random() * 12 - 6, reduced.matches ? 0 : i * 70);
  }
}

document.addEventListener("pointerdown", (event) => {
  if (event.button !== 0) return;
  if (event.target instanceof Element && event.target.closest("a, button, input, form, kbd")) return;
  burst(event.clientX, event.clientY, 3);
});

// ------------------------------------------------------------------ the cat

const mascot = $("mascot");
const picture = $("mascot-picture");
const purr = $("purr");
const SAYS = ["nya~", "mrrp?", "purr…", "mochi!", "nyaa~", "mrow"];

// No picture (the file is missing): no cat, and the search card sits where it would anyway.
function hideCat() { mascot.hidden = true; }
picture.addEventListener("error", hideCat);
if (picture.complete && picture.naturalWidth === 0) hideCat();

let said = -1;
mascot.addEventListener("click", () => {
  mascot.classList.remove("squish");
  purr.classList.remove("show");
  void mascot.offsetWidth;                    // restart both animations on a quick second pat
  mascot.classList.add("squish");
  let say;
  do { say = Math.floor(Math.random() * SAYS.length); } while (say === said && SAYS.length > 1);
  said = say;
  purr.textContent = SAYS[say];
  purr.classList.add("show");
  const box = mascot.getBoundingClientRect();
  burst(box.left + box.width / 2, box.top + box.height * 0.35, 5);
});
mascot.addEventListener("animationend", (event) => {
  if (event.animationName === "squish") mascot.classList.remove("squish");
});
