// washi start page: the clock, the greeting and the date, the search box (DuckDuckGo, or an
// address straight away), number keys for the cards, and the easter egg: click the paper and a
// cat walks to where you clicked, its prints soaking in and fading like ink.
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

    const date = $("date");
    date.replaceChildren(weekday.format(now));
    const dot = document.createElement("span");
    dot.className = "dot";
    date.append(dot, dayMonth.format(now));

    const almanac = $("almanac");
    const week = document.createElement("b");
    week.textContent = "w" + isoWeek(now);
    almanac.replaceChildren(week,
      ` · day ${dayOfYear(now)} of ${daysInYear(now.getFullYear())} · ${now.getFullYear()}`);
  }
  setTimeout(tick, 1000 - (Date.now() % 1000) + 5);
}

tick();

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

// ------------------------------------------------------------------ the cat

const trail = $("trail");
const STRIDE = 30;          // px between prints
const GAIT = 9;             // px between the left and right paws
const reduced = matchMedia("(prefers-reduced-motion: reduce)");
let last = null;

function stamp(x, y, heading, foot) {
  const print = document.createElementNS(SVG, "svg");
  print.setAttribute("class", "print");
  const use = document.createElementNS(SVG, "use");
  use.setAttribute("href", "#print");
  print.append(use);
  // the print's toes point up; turn them along the walk, a little unsteady
  const turn = heading * 180 / Math.PI + 90 + (Math.random() * 12 - 6);
  print.style.left = `${x}px`;
  print.style.top = `${y}px`;
  print.style.setProperty("--turn", `${turn}deg`);
  print.style.setProperty("--foot", foot);
  print.addEventListener("animationend", () => print.remove());
  trail.append(print);
}

// Walk from the last click when it was recent and near, else wander in from somewhere.
function walkTo(x, y) {
  const now = performance.now();
  let fromX, fromY;
  if (last && now - last.time < 5000 && Math.hypot(x - last.x, y - last.y) > STRIDE
      && Math.hypot(x - last.x, y - last.y) < 700) {
    fromX = last.x;
    fromY = last.y;
  } else {
    const angle = Math.random() * Math.PI * 2;
    fromX = x - Math.cos(angle) * STRIDE * 3.5;
    fromY = y - Math.sin(angle) * STRIDE * 3.5;
  }
  last = { x, y, time: now };

  const heading = Math.atan2(y - fromY, x - fromX);
  const steps = Math.max(3, Math.min(18, Math.round(Math.hypot(x - fromX, y - fromY) / STRIDE)));
  const pace = reduced.matches ? 0 : 110;
  for (let i = 1; i <= steps; i++) {
    const t = i / steps;
    const foot = i % 2 ? 1 : -1;
    const px = fromX + (x - fromX) * t - Math.sin(heading) * GAIT * foot;
    const py = fromY + (y - fromY) * t + Math.cos(heading) * GAIT * foot;
    setTimeout(() => stamp(px, py, heading, foot), (i - 1) * pace);
  }
}

document.addEventListener("pointerdown", (event) => {
  if (event.button !== 0) return;
  if (event.target instanceof Element && event.target.closest("a, button, input, form, kbd")) return;
  walkTo(event.clientX, event.clientY);
});
