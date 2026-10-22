#pragma once
#include <Arduino.h>

// The composer, served at /compose. One file with no external requests, like
// the control page. Songs save to the unit through /api/user-songs; opened
// away from a unit it runs as a demo that saves nothing.
static const char COMPOSER_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Compose · Open Jingle Bells</title>
<style>
/* One-screen phone app: top bar, the bell grid (a music-box strip of strikes), thumb-reach controls. */
:root {
  --desk: #dde3e8; --strip: #fbfcfd; --bar: #eaeff3; --panel: #ffffff;
  --ink: #17222b; --muted: #586774; --line: rgba(23, 34, 43, .065); --line-strong: rgba(23, 34, 43, .2);
  --accent: #946012; --accent-soft: rgba(148, 96, 18, .14); --accent-ink: #ffffff; --danger: #b8352e;
  --lane: .065; --tail: .3;
  --f-display: ui-rounded, "SF Pro Rounded", system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  --f-body: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  --f-mono: ui-monospace, "SF Mono", Menlo, Consolas, monospace;
}
@media (prefers-color-scheme: dark) {
  :root {
    --desk: #0b1216; --strip: #121b21; --bar: #19242b; --panel: #1a252c;
    --ink: #e3eaef; --muted: #8e9fab; --line: rgba(227, 234, 239, .055); --line-strong: rgba(227, 234, 239, .17);
    --accent: #e2b05a; --accent-soft: rgba(226, 176, 90, .16); --accent-ink: #1a1206; --danger: #f07b72;
    --lane: .085; --tail: .38; color-scheme: dark;
  }
}
*, *::before, *::after { box-sizing: border-box; }
[hidden] { display: none !important; }
html, body { height: 100%; margin: 0; }
body { background: var(--desk); color: var(--ink); font: 14px/1.4 var(--f-body); overflow: hidden; -webkit-tap-highlight-color: transparent; }
button { font-family: inherit; color: inherit; cursor: pointer; }
a { color: inherit; }
button:focus-visible, input:focus-visible, a:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }

.app {
  height: 100%; max-width: 760px; margin: 0 auto; display: flex; flex-direction: column; gap: 8px;
  padding: max(8px, env(safe-area-inset-top, 0px)) max(16px, env(safe-area-inset-right, 0px))
           max(8px, env(safe-area-inset-bottom, 0px)) max(16px, env(safe-area-inset-left, 0px));
}
.top { display: flex; align-items: center; gap: 8px; }
.title-wrap { flex: 1; min-width: 0; display: flex; flex-direction: column; }
.title { border: 0; background: none; padding: 2px; text-align: left; min-width: 0; font: 700 20px/1.15 var(--f-display); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.save { font-size: 11.5px; color: var(--muted); padding-left: 2px; font-variant-numeric: tabular-nums; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.save.err { color: var(--danger); }
.icon-btn { flex: none; width: 42px; height: 42px; border-radius: 12px; border: 0; background: var(--bar); display: grid; place-items: center; }
.icon-btn:disabled { opacity: .35; cursor: default; }
.seg { display: inline-flex; flex: none; background: var(--bar); border-radius: 999px; padding: 3px; gap: 2px; }
.seg button { border: 0; background: transparent; color: var(--muted); font: 600 13px var(--f-body); padding: 8px 12px; border-radius: 999px; }
.seg button[aria-pressed="true"] { background: var(--panel); color: var(--ink); box-shadow: 0 1px 2px rgba(0, 0, 0, .14); }

.hint { display: flex; align-items: center; gap: 10px; padding: 8px 10px 8px 12px; background: var(--accent-soft); border-radius: 12px; font-size: 13px; line-height: 1.35; }
.hint p { margin: 0; flex: 1; min-width: 0; }
.hint button { flex: none; border: 0; background: var(--panel); border-radius: 999px; padding: 6px 12px; font-weight: 600; font-size: 13px; }

.stage { position: relative; flex: 1; min-height: 0; border-radius: 14px; overflow: hidden; background: var(--strip); box-shadow: inset 0 0 0 1px var(--line-strong); }
.stage canvas { position: absolute; inset: 0; width: 100%; height: 100%; pointer-events: none; }
.scroller { position: absolute; inset: 0; overflow: auto; overscroll-behavior: contain; scrollbar-width: thin; }
.scroller > div { pointer-events: none; }

.selbar { display: flex; align-items: center; gap: 6px; flex-wrap: wrap; background: var(--panel); border-radius: 14px; padding: 6px 6px 6px 12px; box-shadow: inset 0 0 0 1.5px var(--accent); }
.selbar span { flex: 1; min-width: 7em; font-size: 13px; font-weight: 600; font-variant-numeric: tabular-nums; }
.selbar button { border: 0; background: var(--bar); border-radius: 10px; padding: 9px 11px; font-weight: 600; font-size: 13px; }
.selbar button.danger { color: var(--danger); }
.selbar button.done { background: var(--accent); color: var(--accent-ink); }

.bottom { display: flex; flex-direction: column; gap: 8px; }
.row { display: flex; align-items: center; gap: 8px; }
.spacer { flex: 1; }
.play { flex: 1; min-width: 0; height: 54px; border-radius: 16px; border: 0; background: var(--accent); color: var(--accent-ink); font: 700 17px var(--f-display); display: flex; align-items: center; justify-content: center; gap: 8px; white-space: nowrap; }
.play:active { transform: scale(.98); }
.tempo { flex: none; height: 54px; border-radius: 16px; border: 0; background: var(--bar); padding: 0 14px; display: flex; flex-direction: column; justify-content: center; align-items: flex-start; gap: 1px; }
.tempo small { font-size: 10.5px; letter-spacing: .06em; text-transform: uppercase; color: var(--muted); font-weight: 600; }
.tempo b { font: 500 14px var(--f-mono); font-variant-numeric: tabular-nums; }
.out.seg button { padding: 8px 11px; }

.toast { position: fixed; left: 50%; bottom: calc(150px + env(safe-area-inset-bottom, 0px)); z-index: 60; transform: translate(-50%, 8px); opacity: 0; pointer-events: none; max-width: calc(100% - 40px); width: max-content; background: var(--ink); color: var(--strip); padding: 10px 14px; border-radius: 12px; font-size: 13.5px; line-height: 1.35; text-align: center; transition: opacity .18s, transform .18s; }
.toast.show { opacity: 1; transform: translate(-50%, 0); }

.sheet-wrap { position: fixed; inset: 0; z-index: 50; }
.backdrop { position: absolute; inset: 0; background: rgba(5, 10, 14, .42); }
.sheet { position: absolute; left: 0; right: 0; bottom: 0; margin: 0 auto; max-width: 560px; max-height: 82%; overflow: auto; background: var(--panel); border-radius: 22px 22px 0 0;
  padding: 10px max(18px, env(safe-area-inset-right, 0px)) calc(20px + env(safe-area-inset-bottom, 0px)) max(18px, env(safe-area-inset-left, 0px));
  display: flex; flex-direction: column; gap: 10px; animation: up .22s cubic-bezier(.2, .8, .3, 1); }
.sheet::before { content: ""; align-self: center; width: 38px; height: 4px; border-radius: 4px; background: var(--line-strong); margin-bottom: 4px; }
@keyframes up { from { transform: translateY(40px); opacity: .4; } }
@media (prefers-reduced-motion: reduce) { .sheet { animation: none; } .toast { transition: none; } }
.sheet h2 { margin: 0; font: 700 21px/1.2 var(--f-display); }
.sheet .sub { margin: -4px 0 4px; color: var(--muted); font-size: 13.5px; line-height: 1.45; }
.mono { font-family: var(--f-mono); font-size: .92em; }
.btn { display: flex; align-items: center; width: 100%; min-height: 48px; border-radius: 13px; border: 0; background: var(--bar); font: 600 15px var(--f-body); padding: 0 16px; text-align: left; text-decoration: none; }
.btn:disabled { opacity: .4; cursor: default; }
.btn.primary { background: var(--accent); color: var(--accent-ink); justify-content: center; }
.btn.danger { color: var(--danger); }
.songs { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 6px; }
.songs li { display: flex; align-items: center; gap: 6px; background: var(--bar); border-radius: 13px; padding: 4px 6px 4px 4px; }
.songs li.current { box-shadow: inset 0 0 0 1.5px var(--accent); }
.song-open { flex: 1; min-width: 0; border: 0; background: none; text-align: left; padding: 8px 10px; display: flex; flex-direction: column; gap: 2px; }
.song-name { font-weight: 600; font-size: 15px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.song-meta { font-size: 12px; color: var(--muted); font-variant-numeric: tabular-nums; }
.mini { flex: none; border: 0; background: var(--panel); border-radius: 9px; padding: 8px 10px; font-size: 12.5px; font-weight: 600; }
.mini.danger { color: var(--danger); }
.note { margin: 4px 0 0; font-size: 12.5px; color: var(--muted); line-height: 1.45; }
.field { width: 100%; height: 48px; border-radius: 12px; border: 1.5px solid var(--line-strong); background: var(--strip); color: var(--ink); font: 500 16px var(--f-body); padding: 0 14px; }
.range-row { display: flex; justify-content: space-between; font-size: 12.5px; color: var(--muted); font-weight: 600; }
input[type="range"] { width: 100%; accent-color: var(--accent); height: 32px; }
.big-num { font: 500 30px var(--f-mono); font-variant-numeric: tabular-nums; }
.big-num small { font: 500 14px var(--f-body); color: var(--muted); margin-left: 6px; }
</style>
</head>
<body>
<div class="app" id="app">
  <header class="top">
    <button class="icon-btn" id="songsBtn" aria-label="My songs" title="My songs">
      <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M9 6h11M9 12h11M9 18h11M4.5 6h.01M4.5 12h.01M4.5 18h.01"/></svg>
    </button>
    <div class="title-wrap">
      <button class="title" id="titleBtn" title="Rename song"></button>
      <span class="save" id="saveState"></span>
    </div>
    <div class="seg" id="layoutSeg" role="group" aria-label="Grid layout">
      <button data-v="strip" title="Time runs down the screen">Strip</button>
      <button data-v="roll" title="Time runs across the screen">Roll</button>
    </div>
  </header>

  <div class="hint" id="hint" hidden>
    <p>Tap a square to add a bell. The faded trail shows it still ringing, so strike that bell again after the trail ends.</p>
    <button id="hintOk">Got it</button>
  </div>

  <main class="stage" id="stage">
    <canvas id="cv"></canvas>
    <div class="scroller" id="scroller"><div id="sizer"></div></div>
  </main>

  <div class="selbar" id="selbar" hidden>
    <span id="selText"></span>
    <button id="selDup">Duplicate</button>
    <button id="selClear">Clear</button>
    <button id="selDel" class="danger">Remove</button>
    <button id="selDone" class="done">Done</button>
  </div>

  <footer class="bottom">
    <div class="row">
      <div class="seg" id="modeSeg" role="group" aria-label="Touch mode">
        <button data-v="tap" title="Tap squares, drag to scroll">Tap</button>
        <button data-v="paint" title="Drag to paint, two fingers to scroll">Paint</button>
        <button data-v="select" title="Drag across steps to select them">Select</button>
      </div>
      <div class="spacer"></div>
      <button class="icon-btn" id="undoBtn" aria-label="Undo" title="Undo">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 14 4 9l5-5"/><path d="M4 9h10.5a5.5 5.5 0 0 1 0 11H11"/></svg>
      </button>
      <button class="icon-btn" id="redoBtn" aria-label="Redo" title="Redo">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m15 14 5-5-5-5"/><path d="M20 9H9.5a5.5 5.5 0 0 0 0 11H13"/></svg>
      </button>
    </div>
    <div class="row">
      <div class="seg out" id="outSeg" role="group" aria-label="Play on">
        <button data-v="phone">Phone</button>
        <button data-v="bells">Bells</button>
      </div>
      <button class="play" id="playBtn"></button>
      <button class="tempo" id="tempoBtn" aria-label="Speed"><small>Speed</small><b id="tempoVal"></b></button>
    </div>
  </footer>
</div>

<div class="toast" id="toast" role="status" aria-live="polite"></div>
<div class="sheet-wrap" id="sheetWrap" hidden>
  <div class="backdrop" id="backdrop"></div>
  <section class="sheet" id="sheet" role="dialog" aria-modal="true"></section>
</div>

<script>
(() => {
  // Bells in pitch order. hz is the measured pitch from calibration; mask is the ROM bit.
  // The grid keeps words in pitch order (bit i = BELLS[i]); the unit stores ROM words.
  const BELLS = [
    { n: "C6",  hz: 1066, mask: 0x004, c: "#e03a3a" },
    { n: "D6",  hz: 1196, mask: 0x002, c: "#ef8217" },
    { n: "E6",  hz: 1359, mask: 0x001, c: "#f2c00c", dark: true },
    { n: "F6",  hz: 1425, mask: 0x800, c: "#88bd2c", dark: true },
    { n: "F♯6", hz: 1507, mask: 0x400, c: "#22a55a" },
    { n: "G6",  hz: 1573, mask: 0x200, c: "#119a97" },
    { n: "A6",  hz: 1775, mask: 0x100, c: "#4c66dc" },
    { n: "A♯6", hz: 1930, mask: 0x010, c: "#8950d2" },
    { n: "B6",  hz: 1994, mask: 0x020, c: "#cc46a0" },
    { n: "C7",  hz: 2116, mask: 0x040, c: "#e03a3a" },
    { n: "D7",  hz: 2410, mask: 0x080, c: "#ef8217" },
    { n: "E7",  hz: 2670, mask: 0x008, c: "#f2c00c", dark: true },
  ];
  const MISSING_ABOVE = new Set([0, 1, 5, 9, 10]); // no C#, D#, G# bells
  const MIN_GAP_MS = 400, MAX_BELLS = 3, STEPS_PER_BEAT = 2, STEPS_PER_BAR = 8, MAX_STEPS = 4000;
  const PREFS = "ojb-composer-prefs-v1";

  const $ = id => document.getElementById(id);
  const stage = $("stage"), scroller = $("scroller"), sizer = $("sizer"), cv = $("cv"), ctx = cv.getContext("2d");

  const toRom = w => { let m = 0; BELLS.forEach((b, i) => { if (w >> i & 1) m |= b.mask; }); return m; };
  const fromRom = m => { let w = 0; BELLS.forEach((b, i) => { if (m & b.mask) w |= 1 << i; }); return w; };
  const hex3 = m => "0x" + m.toString(16).toUpperCase().padStart(3, "0");

  // ---------- prefs (this phone only) ----------
  const prefs = Object.assign({ layout: innerHeight >= innerWidth ? "strip" : "roll", mode: "tap", output: "phone", hint: true, lastUid: null },
    (() => { try { return JSON.parse(localStorage.getItem(PREFS)) || {}; } catch (e) { return {}; } })());
  function savePrefs() { try { localStorage.setItem(PREFS, JSON.stringify(prefs)); } catch (e) {} }

  // ---------- the unit ----------
  let online = false, unitName = "this unit", userSongs = [];
  async function api(path, opts) {
    const r = await fetch(path, Object.assign({ cache: "no-store" }, opts));
    let j = null; try { j = await r.json(); } catch (e) {}
    if (!r.ok || (j && j.ok === false)) throw new Error((j && j.error) || "the unit answered " + r.status);
    return j;
  }
  const post = data => ({ method: "POST", body: new URLSearchParams(data) });
  async function loadList() { userSongs = (await api("/api/songs")).filter(s => s.user); }

  // ---------- the open song ----------
  function demoSong() {
    const f = new Array(64).fill(0), idx = {};
    BELLS.forEach((b, i) => idx[b.n.replace("♯", "#")] = i);
    const put = (i, ...ns) => ns.forEach(n => f[i] |= 1 << idx[n]);
    [[0,"E6"],[2,"E6"],[4,"E6"],[8,"E6"],[10,"E6"],[12,"E6"],[16,"E6"],[18,"G6"],[20,"C6"],[23,"D6"],[24,"E6"],
     [32,"F6"],[34,"F6"],[36,"F6"],[39,"F6"],[40,"F6"],[42,"E6"],[44,"E6"],[46,"E6"],[47,"E6"],
     [48,"E6"],[50,"D6"],[52,"D6"],[54,"E6"],[56,"D6"],[60,"G6"]].forEach(([i, n]) => put(i, n));
    [[0,"G6","C7"],[8,"G6","C7"],[16,"G6","C7"],[24,"G6","C7"],[32,"A6","C7"],[40,"G6","C7"],[48,"G6","B6"],[56,"G6","B6"]]
      .forEach(([i, ...ns]) => put(i, ...ns));
    return { uid: null, index: -1, name: "Jingle Bells (example)", stepMs: 250, frames: f };
  }
  const blankSong = () => ({ uid: null, index: -1, name: "Untitled song " + (userSongs.length + 1), stepMs: 250, frames: [] });
  let song = demoSong();
  let undoStack = [], redoStack = [];

  const bits = w => { let n = 0; while (w) { n += w & 1; w >>= 1; } return n; };
  const word = f => song.frames[f] | 0;
  function setWord(f, w) { while (song.frames.length <= f) song.frames.push(0); song.frames[f] = w; }
  function lastStrike() { for (let i = song.frames.length - 1; i >= 0; i--) if (song.frames[i]) return i; return -1; }
  const fmtTime = ms => { const s = ms / 1000; return Math.floor(s / 60) + ":" + String(Math.floor(s % 60)).padStart(2, "0"); };
  const meta = (n, stepMs) => n ? `${n} steps · ${fmtTime(n * stepMs)}` : "empty";

  function snapshot() { return song.frames.slice(); }
  function commit(before) {
    undoStack.push(before); if (undoStack.length > 200) undoStack.shift();
    redoStack = []; afterEdit();
  }
  function afterEdit() { markDirty(); updateRows(); updateButtons(); requestDraw(); }
  function undo() { if (!undoStack.length) return; redoStack.push(snapshot()); song.frames = undoStack.pop(); afterEdit(); }
  function redo() { if (!redoStack.length) return; undoStack.push(snapshot()); song.frames = redoStack.pop(); afterEdit(); }

  // ---------- saving: a short pause after each edit, one request at a time ----------
  let dirty = false, saveTimer = 0, saving = null;
  function setSaveState(text, err) { const el = $("saveState"); el.textContent = text; el.classList.toggle("err", !!err); }
  function showSaved() {
    const n = lastStrike() + 1;
    if (!online) setSaveState("Demo: not connected to a bell unit, nothing is saved");
    else if (!song.uid) setSaveState("Not saved yet · tap a square to start");
    else setSaveState(`Saved on ${unitName} · ${meta(n, song.stepMs)}`);
  }
  function markDirty() {
    if (!online) return showSaved();
    dirty = true; setSaveState("Saving…");
    clearTimeout(saveTimer); saveTimer = setTimeout(flushSave, 700);
  }
  async function flushSave() {
    clearTimeout(saveTimer);
    while (saving) await saving;
    if (!dirty || !online) return;
    dirty = false;
    const target = song, f = target.frames.slice(0, MAX_STEPS);
    while (f.length && !f[f.length - 1]) f.pop();
    if (!f.length) f.push(0);
    const body = { name: target.name, step_ms: target.stepMs, frames: f.map(w => toRom(w).toString(16)).join(",") };
    saving = api(target.uid ? "/api/user-songs/" + target.uid : "/api/user-songs", post(body))
      .then(j => {
        target.uid = j.uid; target.index = j.song;
        if (target === song) { prefs.lastUid = j.uid; savePrefs(); if (!dirty) showSaved(); }
      })
      .catch(e => {
        if (target === song) { dirty = true; setSaveState("Not saved: " + e.message, true); }
        toast("Couldn't save: " + e.message, 3600);
      })
      .finally(() => { saving = null; });
    await saving;
  }

  // ---------- audio (phone preview) ----------
  let actx = null, master = null;
  const rings = BELLS.map(() => []);  // glow times, in performance.now() seconds
  const pnow = () => performance.now() / 1000;
  function ensureAudio() {
    if (!actx) {
      const AC = window.AudioContext || window.webkitAudioContext; if (!AC) return;
      actx = new AC();
      const comp = actx.createDynamicsCompressor();
      master = actx.createGain(); master.gain.value = .22;
      master.connect(comp); comp.connect(actx.destination);
    }
    if (actx.state !== "running") actx.resume().catch(() => {});
  }
  // Phones unlock sound only inside a tap (touchend/click, not pointerdown). iOS also mutes
  // Web Audio on the silent switch unless the page plays as media.
  let primed = false, silentEl = null;
  function silentWavUrl() {
    const n = 8000, buf = new ArrayBuffer(44 + n), v = new DataView(buf), s = (o, t) => [...t].forEach((c, i) => v.setUint8(o + i, c.charCodeAt(0)));
    s(0, "RIFF"); v.setUint32(4, 36 + n, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
    v.setUint32(24, 8000, true); v.setUint32(28, 8000, true); v.setUint16(32, 1, true); v.setUint16(34, 8, true); s(36, "data"); v.setUint32(40, n, true);
    for (let i = 0; i < n; i++) v.setUint8(44 + i, 128);
    return URL.createObjectURL(new Blob([buf], { type: "audio/wav" }));
  }
  function unlockAudio() {
    try { if (navigator.audioSession) navigator.audioSession.type = "playback"; } catch (e) {}
    ensureAudio(); if (!actx) return;
    if (!primed) {
      primed = true;
      try { const src = actx.createBufferSource(); src.buffer = actx.createBuffer(1, 1, 22050); src.connect(actx.destination); src.start(0); } catch (e) {}
      try { silentEl = new Audio(silentWavUrl()); silentEl.loop = true; silentEl.setAttribute("playsinline", ""); } catch (e) { silentEl = null; }
    }
    if (silentEl && silentEl.paused) silentEl.play().catch(() => {});
  }
  ["touchend", "click", "keydown"].forEach(ev => document.addEventListener(ev, unlockAudio, { capture: true, passive: true }));

  function glow(b, at) { rings[b].push(at ?? pnow()); requestDraw(); }
  function ring(b, when) {
    ensureAudio(); if (!actx) return;
    const t = when ?? actx.currentTime, hz = BELLS[b].hz;
    // fundamental plus two inharmonic partials, like a small struck bell
    for (const [mul, amp, dec] of [[1, 1, 1.5], [2.76, .26, .45], [5.4, .09, .2]]) {
      const o = actx.createOscillator(), g = actx.createGain();
      o.frequency.value = hz * mul;
      g.gain.setValueAtTime(.0001, t);
      g.gain.exponentialRampToValueAtTime(amp, t + .004);
      g.gain.exponentialRampToValueAtTime(.0001, t + dec);
      o.connect(g).connect(master); o.start(t); o.stop(t + dec + .05);
    }
    glow(b, pnow() + (t - actx.currentTime));
  }
  // Hearing one bell: the phone, or the unit's real bell when playing on bells.
  function audition(b) {
    if (prefs.output === "bells" && online) { glow(b); api("/api/strike", post({ word: BELLS[b].mask })).catch(() => {}); }
    else ring(b);
  }
  function ringLevel(b, now) {
    const a = rings[b]; while (a.length && a[0] < now - .6) a.shift();
    let k = 0; for (const t of a) if (t <= now) k = Math.max(k, 1 - (now - t) / .45);
    return k;
  }

  // ---------- playback ----------
  // play is null, a phone preview { t0, next }, or bells { bells, index, from, F, T, seen, lastF }.
  let play = null, playPos = null, playTimer = 0, startFrame = 0, userScrollUntil = 0;
  const stepS = () => song.stepMs / 1000;
  const endFrame = () => { const l = lastStrike(); return l < 0 ? 0 : l + 1 + Math.ceil(900 / song.stepMs); };
  function playFrom(f) {
    if (endFrame() === 0) { toast("Add some bells first."); return; }
    if (f >= endFrame() - 1) f = 0;
    startFrame = f;
    if (prefs.output === "bells" && online) return playOnBells(f);
    unlockAudio(); if (!actx) { toast("This browser can't play sound."); return; }
    stopPlay();
    if (actx.state !== "running") {
      actx.resume().then(() => playFrom(f)).catch(() => {});
      setTimeout(() => { if (actx.state !== "running") toast("The phone blocked sound. Tap Play again, and check that the phone isn't on silent.", 3600); }, 600);
      return;
    }
    play = { t0: actx.currentTime + .06 - f * stepS(), next: f };
    playTimer = setInterval(schedule, 25); schedule();
    updateButtons(); requestDraw();
  }
  function schedule() {
    if (!play || play.bells) return;
    const s = stepS(), end = endFrame();
    while (play.next < end && play.t0 + play.next * s < actx.currentTime + .15) {
      const w = word(play.next);
      for (let b = 0; b < 12; b++) if (w >> b & 1) ring(b, play.t0 + play.next * s);
      play.next++;
    }
    if (actx.currentTime > play.t0 + end * s) stopPlay();
  }
  async function playOnBells(f) {
    stopPlay();
    try {
      dirty = dirty || !song.uid;
      await flushSave();
      if (!song.uid) return;
      // single: the composer plays just this song, not the rest of the library
      await api("/api/play", post({ song: song.index, mode: "single", from: f }));
    } catch (e) { toast("Couldn't play: " + e.message, 3600); return; }
    play = { bells: true, index: song.index, from: f, started: performance.now(), seen: false, lastF: f - 1 };
    pollBells(); updateButtons(); requestDraw();
  }
  // The unit reports the next frame it will strike; the playhead runs on from there.
  async function pollBells() {
    const p0 = play;
    if (!p0 || !p0.bells) return;
    try {
      const pb = (await api("/api/status")).playback;
      if (play !== p0) return;
      if (!pb.playing || pb.song !== p0.index) { if (performance.now() - p0.started > 1500) return stopPlay(); }
      else if (pb.frame >= 0) { p0.F = pb.frame; p0.T = performance.now(); p0.seen = true; }
    } catch (e) {}
    if (play === p0) p0.timer = setTimeout(pollBells, 250);
  }
  function bellsPos(p) {
    if (!p.seen) return p.from;
    const est = p.F - 1 + (performance.now() - p.T) / song.stepMs;
    return Math.max(p.F - 1, Math.min(p.F + 1.5, est));
  }
  function stopPlay(tellUnit) {
    if (play && play.bells) { clearTimeout(play.timer); if (tellUnit) api("/api/stop", post({})).catch(() => {}); }
    if (playTimer) clearInterval(playTimer);
    playTimer = 0; play = null; playPos = null; updateButtons(); requestDraw();
  }
  function setTempo(ms) {
    if (play && !play.bells) { const pos = (actx.currentTime - play.t0) / stepS(); song.stepMs = ms; play.t0 = actx.currentTime - pos * stepS(); }
    else song.stepMs = ms;
    $("tempoVal").textContent = ms + " ms";
    markDirty(); requestDraw();
  }

  // ---------- geometry ----------
  // t = the time axis (steps), b = the bell axis. Strip: t runs down. Roll: t runs right.
  let G = null, nRows = 40, dpr = 1;
  function computeGeo() {
    const W = stage.clientWidth, H = stage.clientHeight;
    if (prefs.layout === "strip") {
      G = { strip: true, W, H, tStart: 46, tSize: 30, bStart: 30, bSize: (W - 30) / 12 };
    } else {
      const bStart = 22;
      G = { strip: false, W, H, tStart: 52, tSize: 34, bStart, bSize: Math.max(20, Math.min(46, (H - bStart) / 12)) };
    }
  }
  function updateRows() {
    nRows = Math.min(MAX_STEPS, Math.max(lastStrike() + 1 + 24, 40));
    const len = G.tStart + nRows * G.tSize + 40;
    if (G.strip) { sizer.style.width = G.W + "px"; sizer.style.height = len + "px"; scroller.style.overflowX = "hidden"; scroller.style.overflowY = "auto"; }
    else { sizer.style.width = len + "px"; sizer.style.height = Math.min(G.H, G.bStart + 12 * G.bSize) + "px"; scroller.style.overflowX = "auto"; scroller.style.overflowY = "hidden"; }
  }
  const scrollT = () => G.strip ? scroller.scrollTop : scroller.scrollLeft;
  function setScrollT(v) { if (G.strip) scroller.scrollTop = v; else scroller.scrollLeft = v; }
  function resize() {
    const t = G ? scrollT() / G.tSize : 0;
    computeGeo(); updateRows();
    dpr = Math.min(3, window.devicePixelRatio || 1);
    cv.width = Math.round(G.W * dpr); cv.height = Math.round(G.H * dpr);
    setScrollT(t * G.tSize);
    applyTouchAction(); requestDraw();
  }
  function hit(x, y) {
    const tp = G.strip ? y : x, bp = G.strip ? x : y;
    if (tp < G.tStart && bp < G.bStart) return null;
    let b = null;
    if (bp >= G.bStart) { const i = Math.floor((bp - G.bStart) / G.bSize); if (i >= 0 && i < 12) b = G.strip ? i : 11 - i; }
    if (tp < G.tStart) return b == null ? null : { zone: "bell", b };
    const f = Math.floor((tp - G.tStart + scrollT()) / G.tSize);
    if (f >= MAX_STEPS) return null;
    if (bp < G.bStart) return { zone: "time", f };
    return b == null ? null : { zone: "cell", f, b };
  }

  // ---------- drawing ----------
  let T = {};
  function readTheme() {
    const cs = getComputedStyle(document.documentElement), v = n => cs.getPropertyValue(n).trim();
    T = { strip: v("--strip"), bar: v("--bar"), ink: v("--ink"), muted: v("--muted"), line: v("--line"), lineStrong: v("--line-strong"),
          accent: v("--accent"), accentSoft: v("--accent-soft"), danger: v("--danger"), lane: parseFloat(v("--lane")) || .07, tail: parseFloat(v("--tail")) || .3 };
  }
  function rr(x, y, w, h, r) {
    r = Math.min(r, w / 2, h / 2);
    ctx.beginPath(); ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r); ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r); ctx.arcTo(x, y, x + w, y, r); ctx.closePath();
  }
  let raf = 0, shake = null, sel = null;
  function requestDraw() { if (!raf) raf = requestAnimationFrame(tick); }
  function tick() {
    raf = 0;
    const now = pnow();
    if (play) {
      if (play.bells) {
        playPos = bellsPos(play);
        // flash the chips as the unit strikes
        const f = Math.floor(playPos);
        if (f > play.lastF) { const w = word(f); for (let b = 0; b < 12; b++) if (w >> b & 1) glow(b); play.lastF = f; }
      } else playPos = (actx.currentTime - play.t0) / stepS();
      if (Date.now() > userScrollUntil && G) {
        const view = (G.strip ? G.H : G.W) - G.tStart;
        const target = Math.max(0, playPos * G.tSize - view * .3);
        if (Math.abs(target - scrollT()) > .5) setScrollT(target);
      }
    }
    draw(now);
    const glowing = rings.some(a => a.some(t => t > now - .5));
    if (play || glowing || (shake && performance.now() - shake.t0 < 420)) raf = requestAnimationFrame(tick);
  }

  function draw(now) {
    if (!G) return;
    const { strip, W, H, tStart, tSize, bStart, bSize } = G;
    const sT = scrollT(), viewEnd = strip ? H : W, crossEnd = strip ? W : H;
    const bEnd = bStart + 12 * bSize;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = T.strip; ctx.fillRect(0, 0, W, H);

    const tp = f => tStart + f * tSize - sT;
    const laneLo = b => strip ? bStart + b * bSize : bStart + (11 - b) * bSize;
    const laneMid = b => laneLo(b) + bSize / 2;
    const XY = (t, b) => strip ? [b, t] : [t, b];
    const fill = (t0, t1, b0, b1, c) => { ctx.fillStyle = c; strip ? ctx.fillRect(b0, t0, b1 - b0, t1 - t0) : ctx.fillRect(t0, b0, t1 - t0, b1 - b0); };
    const lineT = (t, b0, b1, c, w) => { ctx.strokeStyle = c; ctx.lineWidth = w; ctx.beginPath(); ctx.moveTo(...XY(t, b0)); ctx.lineTo(...XY(t, b1)); ctx.stroke(); };
    const lineB = (b, t0, t1, c, w) => { ctx.strokeStyle = c; ctx.lineWidth = w; ctx.beginPath(); ctx.moveTo(...XY(t0, b)); ctx.lineTo(...XY(t1, b)); ctx.stroke(); };

    const f0 = Math.max(0, Math.floor(sT / tSize) - 1);
    const f1 = Math.min(nRows, f0 + Math.ceil((viewEnd - tStart) / tSize) + 3);
    const songEnd = endFrame();

    // lanes tinted with their bell's colour
    ctx.globalAlpha = T.lane;
    for (let b = 0; b < 12; b++) fill(tStart, viewEnd, laneLo(b), laneLo(b) + bSize, BELLS[b].c);
    ctx.globalAlpha = 1;
    // beat shading and step lines
    for (let f = f0; f < f1; f++) if (Math.floor(f / STEPS_PER_BEAT) % 2) fill(tp(f), tp(f + 1), bStart, bEnd, T.line);
    for (let f = f0; f <= f1; f++) {
      if (f % STEPS_PER_BAR === 0) lineT(tp(f), bStart, bEnd, T.lineStrong, 1.25);
      else if (f % STEPS_PER_BEAT === 0) lineT(tp(f), bStart, bEnd, T.line, 1);
    }
    // lane edges; a heavier line where a semitone has no bell
    for (let b = 0; b < 11; b++) {
      const edge = strip ? laneLo(b) + bSize : laneLo(b);
      lineB(edge, tStart, viewEnd, MISSING_ABOVE.has(b) ? T.lineStrong : T.line, 1);
    }
    // song end
    if (songEnd > 0 && songEnd >= f0 && songEnd <= f1) {
      ctx.setLineDash([4, 4]); lineT(tp(songEnd), bStart, bEnd, T.muted, 1); ctx.setLineDash([]);
    }
    // selection
    if (sel) {
      const a = Math.min(sel.a, sel.b), z = Math.max(sel.a, sel.b);
      fill(tp(a), tp(z + 1), bStart, bEnd, T.accentSoft);
      lineT(tp(a), bStart, bEnd, T.accent, 2); lineT(tp(z + 1), bStart, bEnd, T.accent, 2);
    }

    // tails, then strikes on top
    const ringF = Math.max(1, Math.ceil(MIN_GAP_MS / song.stepMs));
    const r = Math.min(tSize, bSize) * .34;
    const strikes = [];
    const shk = shake && performance.now() - shake.t0 < 420 ? shake : null;
    const shakeOff = shk ? Math.sin((performance.now() - shk.t0) * .07) * 4 * (1 - (performance.now() - shk.t0) / 420) : 0;
    for (let b = 0; b < 12; b++) {
      let prev = -1e9;
      for (let f = Math.max(0, f0 - ringF - 1); f < f1 + ringF; f++) {
        if (!(word(f) >> b & 1)) continue;
        const soon = (f - prev) * song.stepMs < MIN_GAP_MS;
        prev = f;
        if (f + ringF < f0 || f > f1) continue;
        let next = f + 1; while (next <= f + ringF && !(word(next) >> b & 1)) next++;
        const len = Math.min(next - f, ringF);
        ctx.globalAlpha = T.tail; ctx.lineCap = "round";
        lineB(laneMid(b), tp(f + .5), tp(f + .5 + len) - (next - f <= ringF ? r : 0), BELLS[b].c, r * 1.05);
        ctx.globalAlpha = 1; ctx.lineCap = "butt";
        strikes.push([f, b, soon]);
      }
    }
    const curStep = playPos != null ? Math.floor(playPos) : -1;
    for (const [f, b, soon] of strikes) {
      const [x, y] = XY(tp(f + .5), laneMid(b) + (shk && shk.f === f ? shakeOff : 0));
      const active = f === curStep && playPos - f < 1.2;
      const rad = active ? r * 1.2 : r;
      ctx.beginPath(); ctx.arc(x, y, rad, 0, Math.PI * 2);
      ctx.fillStyle = BELLS[b].c;
      if (soon) {
        ctx.globalAlpha = .38; ctx.fill(); ctx.globalAlpha = 1;
        ctx.setLineDash([2.5, 2.5]); ctx.strokeStyle = BELLS[b].c; ctx.lineWidth = 1.6; ctx.stroke(); ctx.setLineDash([]);
      } else {
        ctx.fill();
        ctx.strokeStyle = "rgba(0,0,0,.18)"; ctx.lineWidth = 1; ctx.stroke();
      }
      if (active) { ctx.beginPath(); ctx.arc(x, y, rad + 3, 0, Math.PI * 2); ctx.strokeStyle = T.accent; ctx.lineWidth = 2; ctx.stroke(); }
    }
    if (shk) { ctx.globalAlpha = .12 * (1 - (performance.now() - shk.t0) / 420); fill(tp(shk.f), tp(shk.f + 1), bStart, bEnd, T.danger); ctx.globalAlpha = 1; }

    // playhead
    if (playPos != null) lineT(tp(playPos), 0, bEnd, T.accent, 2.5);

    // bell header
    fill(0, tStart, 0, crossEnd, T.bar);
    lineT(tStart, 0, crossEnd, T.lineStrong, 1);
    for (let b = 0; b < 12; b++) {
      const k = ringLevel(b, now);
      const pad = strip ? 2.5 : 3;
      const [x, y] = XY(6 - k * 3, laneLo(b) + pad);
      const [w, h] = XY(tStart - 12, bSize - pad * 2);
      if (k > 0) { ctx.shadowColor = BELLS[b].c; ctx.shadowBlur = 14 * k; }
      rr(x, y, w, h, 7); ctx.fillStyle = BELLS[b].c; ctx.fill();
      ctx.shadowBlur = 0;
      if (k > 0) { ctx.globalAlpha = .4 * k; ctx.fillStyle = "#fff"; ctx.fill(); ctx.globalAlpha = 1; }
      ctx.fillStyle = BELLS[b].dark ? "#1d1a0c" : "#fff";
      ctx.font = `700 ${strip && bSize < 30 ? 10.5 : 12}px system-ui, -apple-system, sans-serif`;
      ctx.textAlign = "center"; ctx.textBaseline = "middle";
      ctx.fillText(BELLS[b].n, x + w / 2, y + h / 2 + .5);
    }

    // step gutter with bar numbers
    fill(tStart, viewEnd, 0, bStart, T.bar);
    lineB(bStart, tStart, viewEnd, T.lineStrong, 1);
    ctx.font = "500 10.5px ui-monospace, Menlo, monospace";
    for (let f = f0; f < f1; f++) {
      if (f % STEPS_PER_BAR === 0) {
        ctx.fillStyle = T.muted;
        if (strip) { ctx.textAlign = "center"; ctx.textBaseline = "top"; ctx.fillText(String(f / STEPS_PER_BAR + 1), bStart / 2, tp(f) + 4); }
        else { ctx.textAlign = "left"; ctx.textBaseline = "middle"; ctx.fillText(String(f / STEPS_PER_BAR + 1), tp(f) + 4, bStart / 2); }
      } else if (f % STEPS_PER_BEAT === 0) {
        const [x, y] = XY(tp(f), bStart / 2);
        ctx.beginPath(); ctx.arc(x, y, 1.6, 0, Math.PI * 2); ctx.fillStyle = T.lineStrong; ctx.fill();
      }
    }
    const marker = playPos != null ? playPos : startFrame;
    if (playPos != null || startFrame > 0) {
      const t = tp(marker);
      if (t >= tStart - 6) {
        ctx.beginPath();
        if (strip) { ctx.moveTo(bStart - 1, t); ctx.lineTo(bStart - 9, t - 6); ctx.lineTo(bStart - 9, t + 6); }
        else { ctx.moveTo(t, bStart - 1); ctx.lineTo(t - 6, bStart - 9); ctx.lineTo(t + 6, bStart - 9); }
        ctx.closePath(); ctx.fillStyle = T.accent; ctx.fill();
      }
    }
    fill(0, tStart, 0, bStart, T.bar);
  }

  // ---------- editing ----------
  let lastLimitToast = 0;
  function refuse(f) {
    shake = { f, t0: performance.now() }; requestDraw();
    if (navigator.vibrate) try { navigator.vibrate(30); } catch (e) {}
    if (Date.now() - lastLimitToast > 1800) { lastLimitToast = Date.now(); toast("A unit strikes at most 3 bells at once."); }
  }
  function toggleCell(f, b) {
    const before = snapshot(), w = word(f);
    if (w >> b & 1) setWord(f, w & ~(1 << b));
    else if (bits(w) >= MAX_BELLS) { refuse(f); return; }
    else { setWord(f, w | 1 << b); audition(b); }
    commit(before);
  }
  function paintAt(h) {
    const k = h.f + ":" + h.b;
    if (gesture.last === k) return;
    gesture.last = k;
    const w = word(h.f), on = !!(w >> h.b & 1);
    if (gesture.on && !on) {
      if (bits(w) >= MAX_BELLS) { if (gesture.refused !== h.f) { gesture.refused = h.f; refuse(h.f); } return; }
      setWord(h.f, w | 1 << h.b); audition(h.b); gesture.changed = true;
    } else if (!gesture.on && on) { setWord(h.f, w & ~(1 << h.b)); gesture.changed = true; }
    updateRows(); requestDraw();
  }
  function insertSteps(at, arr) {
    while (song.frames.length < at) song.frames.push(0);
    song.frames.splice(at, 0, ...arr);
    if (song.frames.length > MAX_STEPS) { song.frames.length = MAX_STEPS; toast("A song can be up to 4000 steps."); }
  }

  // ---------- pointer input ----------
  const ptrs = new Map();
  let gesture = null;
  const local = e => { const r = stage.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
  const mid = () => { let x = 0, y = 0; ptrs.forEach(p => { x += p.x; y += p.y; }); return { x: x / ptrs.size, y: y / ptrs.size }; };
  function applyTouchAction() {
    scroller.style.touchAction = prefs.mode === "tap" ? (prefs.layout === "strip" ? "pan-y" : "pan-x") : "none";
  }
  function endGesture() {
    if (!gesture) return;
    if (gesture.timer) clearTimeout(gesture.timer);
    if (gesture.type === "paint" && gesture.changed) commit(gesture.before);
    if (gesture.type === "select") showSelbar();
    gesture = null;
  }
  scroller.addEventListener("pointerdown", e => {
    ptrs.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (ptrs.size === 2 && prefs.mode !== "tap") { endGesture(); gesture = { type: "pan", mid: mid() }; return; }
    if (ptrs.size > 1) return;
    const p = local(e), h = hit(p.x, p.y);
    if (!h) return;
    if (h.zone === "bell") { audition(h.b); return; }
    const base = { x0: e.clientX, y0: e.clientY };
    if (h.zone === "time") {
      gesture = { ...base, type: "time", f: h.f };
      gesture.timer = setTimeout(() => { if (gesture && gesture.type === "time") { gesture.long = true; openStepMenu(h.f); } }, 480);
      return;
    }
    if (prefs.mode === "tap") {
      gesture = { ...base, type: "tap", h };
      gesture.timer = setTimeout(() => { if (gesture && gesture.type === "tap") { gesture.long = true; openStepMenu(h.f); } }, 520);
    } else if (prefs.mode === "paint") {
      scroller.setPointerCapture(e.pointerId);
      gesture = { type: "paint", on: !(word(h.f) >> h.b & 1), before: snapshot(), changed: false, last: null };
      paintAt(h);
    } else {
      scroller.setPointerCapture(e.pointerId);
      $("selbar").hidden = true;
      gesture = { type: "select" }; sel = { a: h.f, b: h.f }; requestDraw();
    }
  });
  scroller.addEventListener("pointermove", e => {
    if (!ptrs.has(e.pointerId)) return;
    ptrs.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (!gesture) return;
    if (gesture.type === "pan") {
      const m = mid(); scroller.scrollLeft -= m.x - gesture.mid.x; scroller.scrollTop -= m.y - gesture.mid.y; gesture.mid = m;
      userScrollUntil = Date.now() + 2500; return;
    }
    if (gesture.type === "tap" || gesture.type === "time") {
      if (Math.hypot(e.clientX - gesture.x0, e.clientY - gesture.y0) > 9) { clearTimeout(gesture.timer); gesture = null; }
      return;
    }
    const p = local(e), h = hit(p.x, p.y);
    if (gesture.type === "paint" && h && h.zone === "cell") paintAt(h);
    if (gesture.type === "select" && h && h.f != null && h.f >= 0) { sel.b = Math.min(h.f, nRows - 1); requestDraw(); }
  });
  function pointerEnd(e, cancelled) {
    ptrs.delete(e.pointerId);
    if (!gesture) return;
    if (gesture.type === "pan") { if (!ptrs.size) gesture = null; return; }
    if (gesture.type === "tap" || gesture.type === "time") {
      clearTimeout(gesture.timer);
      if (!cancelled && !gesture.long) {
        if (gesture.type === "tap") toggleCell(gesture.h.f, gesture.h.b);
        else playFrom(gesture.f);
      }
      gesture = null; return;
    }
    endGesture();
  }
  scroller.addEventListener("pointerup", e => pointerEnd(e, false));
  scroller.addEventListener("pointercancel", e => pointerEnd(e, true));
  scroller.addEventListener("contextmenu", e => e.preventDefault());
  scroller.addEventListener("scroll", requestDraw, { passive: true });
  scroller.addEventListener("wheel", () => userScrollUntil = Date.now() + 2500, { passive: true });
  scroller.addEventListener("touchstart", () => userScrollUntil = Date.now() + 2500, { passive: true });

  // ---------- selection ----------
  function selRange() { return [Math.min(sel.a, sel.b), Math.max(sel.a, sel.b)]; }
  function showSelbar() {
    if (!sel) return;
    const [a, z] = selRange(), n = z - a + 1;
    $("selText").textContent = n === 1 ? `Step ${a + 1}` : `Steps ${a + 1}–${z + 1}`;
    $("selbar").hidden = false;
  }
  function clearSel() { sel = null; $("selbar").hidden = true; requestDraw(); }
  $("selDup").onclick = () => {
    const [a, z] = selRange(), before = snapshot(), part = [];
    for (let f = a; f <= z; f++) part.push(word(f));
    insertSteps(z + 1, part); sel = { a: z + 1, b: z + part.length }; commit(before); showSelbar();
    toast(`Duplicated ${part.length} step${part.length > 1 ? "s" : ""}.`);
  };
  $("selClear").onclick = () => { const [a, z] = selRange(), before = snapshot(); for (let f = a; f <= z; f++) if (word(f)) setWord(f, 0); commit(before); };
  $("selDel").onclick = () => { const [a, z] = selRange(), before = snapshot(); song.frames.splice(a, z - a + 1); commit(before); clearSel(); };
  $("selDone").onclick = clearSel;

  // ---------- sheets ----------
  function openSheet(html, mount) {
    const sh = $("sheet"); sh.innerHTML = html; sh.onclick = null; $("sheetWrap").hidden = false;
    mount && mount(sh);
    const first = sh.querySelector("input, button"); first && first.focus({ preventScroll: true });
  }
  function closeSheet() { $("sheetWrap").hidden = true; $("sheet").innerHTML = ""; }
  $("backdrop").onclick = closeSheet;
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

  function openStepMenu(f) {
    if (navigator.vibrate) try { navigator.vibrate(12); } catch (e) {}
    const w = word(f), bar = Math.floor(f / STEPS_PER_BAR) + 1, beat = Math.floor((f % STEPS_PER_BAR) / STEPS_PER_BEAT) + 1;
    const names = BELLS.filter((_, i) => w >> i & 1).map(b => b.n).join(" + ") || "No bells";
    openSheet(`
      <h2>Step ${f + 1}</h2>
      <p class="sub">Bar ${bar}, beat ${beat} · at ${(f * song.stepMs / 1000).toFixed(2)} s<br>${names} · <span class="mono">ROM word ${hex3(toRom(w))}</span></p>
      <button class="btn" data-a="play">Play from here</button>
      <button class="btn" data-a="ins">Insert an empty step before this one</button>
      <button class="btn" data-a="clr" ${w ? "" : "disabled"}>Clear this step</button>
      <button class="btn danger" data-a="del">Remove this step</button>`, sh => {
      sh.onclick = e => {
        const a = e.target.closest("[data-a]")?.dataset.a; if (!a) return;
        closeSheet();
        if (a === "play") return playFrom(f);
        const before = snapshot();
        if (a === "ins") insertSteps(f, [0]);
        if (a === "clr") setWord(f, 0);
        if (a === "del") song.frames.splice(f, 1);
        commit(before);
      };
    });
  }
  function openRename() {
    openSheet(`
      <h2>Rename song</h2>
      <form id="renameForm" style="display:flex;flex-direction:column;gap:10px">
        <input class="field" id="renameInput" maxlength="39" value="${esc(song.name)}" aria-label="Song name">
        <button class="btn primary" type="submit">Save name</button>
      </form>`, sh => {
      const inp = sh.querySelector("#renameInput"); setTimeout(() => inp.select(), 30);
      sh.querySelector("#renameForm").onsubmit = e => {
        e.preventDefault(); const v = inp.value.trim(); if (v) { song.name = v; renderTitle(); markDirty(); } closeSheet();
      };
    });
  }
  function openTempo() {
    const label = ms => `${Math.round(60000 / (ms * STEPS_PER_BEAT))}<small>BPM · ${ms} ms per step</small>`;
    openSheet(`
      <h2>Speed</h2>
      <div class="big-num" id="tempoBig">${label(song.stepMs)}</div>
      <input type="range" id="tempoRange" min="150" max="400" step="10" value="${550 - song.stepMs}" aria-label="Speed">
      <div class="range-row"><span>Slower</span><span>Faster</span></div>
      <p class="note">Each square is one step. Two steps make a beat. The original cartridge plays a step every 200 ms.</p>
      <button class="btn primary" id="tempoDone">Done</button>`, sh => {
      const rg = sh.querySelector("#tempoRange");
      rg.oninput = () => { const ms = 550 - +rg.value; setTempo(ms); sh.querySelector("#tempoBig").innerHTML = label(ms); };
      sh.querySelector("#tempoDone").onclick = closeSheet;
    });
  }
  async function openSongs() {
    if (online) { try { await loadList(); } catch (e) {} }
    const list = userSongs.slice();
    if (!song.uid) list.unshift({ uid: null, name: song.name, frames: lastStrike() + 1, step_ms: song.stepMs });
    const rows = list.map(s => `
      <li class="${s.uid === song.uid ? "current" : ""}">
        <button class="song-open" ${s.uid ? `data-open="${s.uid}"` : ""}><span class="song-name">${esc(s.name)}</span><span class="song-meta">${s.uid ? meta(s.frames, s.step_ms) : online ? "not saved yet" : "demo, not saved"}</span></button>
        ${s.uid ? `<button class="mini" data-dup="${s.uid}">Copy</button><button class="mini danger" data-del="${s.uid}">Delete</button>` : ""}
      </li>`).join("");
    openSheet(`
      <h2>My songs</h2>
      <button class="btn primary" id="newSong" ${online ? "" : "disabled"}>New song</button>
      <ul class="songs">${rows}</ul>
      <p class="note">${online ? `Songs save to ${esc(unitName)}. Play one on the bells and the other units in its group fetch a copy and play along.` : "Open this page from a bell unit to save songs."}</p>
      <a class="btn" href="/">Back to the player</a>`, sh => {
      sh.onclick = async e => {
        const t = e.target.closest("button"); if (!t) return;
        try {
          if (t.id === "newSong") { await switchTo(blankSong()); closeSheet(); toast("New song. Tap a square to start."); return; }
          if (t.dataset.open) { closeSheet(); await openSong(t.dataset.open); return; }
          if (t.dataset.dup) {
            const src = await api("/api/user-songs/" + t.dataset.dup);
            await api("/api/user-songs", post({ name: (src.name.slice(0, 34) + " copy"), step_ms: src.step_ms, frames: src.frames.map(m => m.toString(16)).join(",") }));
            return openSongs();
          }
          if (t.dataset.del) {
            if (t.dataset.armed !== "1") { t.dataset.armed = "1"; t.textContent = "Confirm"; return; }
            const uid = t.dataset.del;
            if (uid === song.uid) dirty = false;
            await api("/api/user-songs/" + uid, { method: "DELETE" });
            if (uid === song.uid) { await loadList(); await switchTo(userSongs.length ? null : blankSong(), userSongs[0]?.uid); }
            return openSongs();
          }
        } catch (err) { toast(err.message, 3600); }
      };
    });
  }
  async function openSong(uid) {
    const j = await api("/api/user-songs/" + uid);
    await switchTo({ uid: j.uid, index: j.song, name: j.name, stepMs: j.step_ms, frames: j.frames.map(fromRom) });
  }
  // Open s, or the song with uid when s is null.
  async function switchTo(s, uid) {
    await flushSave();
    stopPlay(true); clearSel();
    if (!s) return openSong(uid);
    song = s; undoStack = []; redoStack = []; startFrame = 0;
    if (s.uid) { prefs.lastUid = s.uid; savePrefs(); }
    renderTitle(); $("tempoVal").textContent = song.stepMs + " ms";
    showSaved(); updateRows(); setScrollT(0); updateButtons(); requestDraw();
  }

  // ---------- chrome ----------
  let toastTimer = 0;
  function toast(msg, ms = 2200) {
    const t = $("toast"); t.textContent = msg; t.classList.add("show");
    clearTimeout(toastTimer); toastTimer = setTimeout(() => t.classList.remove("show"), ms);
  }
  function renderTitle() { $("titleBtn").textContent = song.name; }
  function updateButtons() {
    $("undoBtn").disabled = !undoStack.length; $("redoBtn").disabled = !redoStack.length;
    const target = prefs.output === "bells" ? "on bells" : "";
    $("playBtn").innerHTML = play
      ? `<svg width="18" height="18" viewBox="0 0 24 24"><rect x="5" y="5" width="14" height="14" rx="2.5" fill="currentColor"/></svg>Stop`
      : `<svg width="18" height="18" viewBox="0 0 24 24"><path d="M7 4.5v15l13-7.5z" fill="currentColor"/></svg>Play ${target}`;
    for (const [seg, val] of [["layoutSeg", prefs.layout], ["modeSeg", prefs.mode], ["outSeg", prefs.output]])
      $(seg).querySelectorAll("button").forEach(b => b.setAttribute("aria-pressed", String(b.dataset.v === val)));
  }
  function bindSeg(id, keyName, after) {
    $(id).onclick = e => {
      const v = e.target.closest("button")?.dataset.v; if (!v || prefs[keyName] === v) return;
      prefs[keyName] = v; savePrefs(); updateButtons(); after && after(v);
    };
  }
  bindSeg("layoutSeg", "layout", () => resize());
  bindSeg("modeSeg", "mode", v => {
    applyTouchAction(); clearSel();
    toast({ tap: "Tap squares to add or remove bells. Drag to scroll.", paint: "Drag to paint bells. Use two fingers to scroll.", select: "Drag across steps to select them." }[v]);
  });
  bindSeg("outSeg", "output", v => {
    stopPlay(true);
    if (v === "bells") toast(online ? "Play now plays on the bells, with every unit in the group." : "Open this page from a bell unit to play on the bells.");
  });
  $("playBtn").onclick = () => play ? stopPlay(true) : playFrom(startFrame);
  $("undoBtn").onclick = undo; $("redoBtn").onclick = redo;
  $("tempoBtn").onclick = openTempo; $("titleBtn").onclick = openRename; $("songsBtn").onclick = openSongs;
  $("hintOk").onclick = () => { prefs.hint = false; $("hint").hidden = true; savePrefs(); };
  document.addEventListener("keydown", e => {
    if (e.key === "Escape" && !$("sheetWrap").hidden) return closeSheet();
    if (e.target.closest("input, textarea") || !$("sheetWrap").hidden) return;
    if (e.key === " ") { e.preventDefault(); play ? stopPlay(true) : playFrom(startFrame); }
    else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") { e.preventDefault(); e.shiftKey ? redo() : undo(); }
  });
  // Leaving or hiding the page: send any pending edit now.
  document.addEventListener("visibilitychange", () => { if (document.hidden) flushSave(); });

  readTheme();
  matchMedia("(prefers-color-scheme: dark)").addEventListener?.("change", () => { readTheme(); requestDraw(); });

  $("hint").hidden = !prefs.hint;
  renderTitle(); $("tempoVal").textContent = song.stepMs + " ms";
  updateButtons();
  new ResizeObserver(resize).observe(stage);
  resize();

  (async () => {
    try {
      const st = await api("/api/status");
      unitName = st.device.name; online = true;
      await loadList();
      const pick = userSongs.find(s => s.uid === prefs.lastUid) || userSongs[userSongs.length - 1];
      await switchTo(pick ? null : blankSong(), pick && pick.uid);
    } catch (e) {
      online = false; showSaved();
    }
  })();
})();
</script>
</body>
</html>
)HTML";
