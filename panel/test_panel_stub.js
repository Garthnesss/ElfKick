#!/usr/bin/env node
/* Stub-DOM synthetic-mouse checks for the ElfKick panel inline script.
 * Run: node test_panel_stub.js   (exit 0 = all checks pass)
 * Recreates the Oct 30 harness (the original was a session artifact and
 * was lost) and extends it with the jogpush (EN_KNOB_ENTER) click-path
 * checks: plain click, sub-detent wiggle, and no-confirm-after-drag. */
const fs = require('fs'), vm = require('vm');
const html = fs.readFileSync(require('path').join(__dirname, 'template.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];

class ClassList {
  constructor() { this.s = new Set(); }
  add(c) { this.s.add(c); } remove(c) { this.s.delete(c); }
  toggle(c, f) { if (f === undefined) f = !this.s.has(c); f ? this.s.add(c) : this.s.delete(c); return f; }
  contains(c) { return this.s.has(c); }
}
class El {
  constructor(dataset = {}) {
    this.dataset = dataset; this.classList = new ClassList();
    this.style = { getPropertyValue: () => '', setProperty: () => {} };
    this.attrs = {}; this.listeners = {}; this._qs = new Map(); this.textContent = '';
  }
  setAttribute(k, v) { this.attrs[k] = String(v); }
  getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
  addEventListener(t, f) { (this.listeners[t] ||= []).push(f); }
  setPointerCapture() {}
  getContext() { return null; }
  getBoundingClientRect() { return { left: 0, top: 0, width: 100, height: 100, right: 100, bottom: 100 }; }
  querySelector(sel) { if (!this._qs.has(sel)) this._qs.set(sel, new El()); return this._qs.get(sel); }
  querySelectorAll() { return []; }
  closest(sel) {
    if (sel.includes('data-ctl')) return this.dataset.ctl ? this : null;
    if (sel === 'select') return null;
    return null;
  }
}
const docCache = new Map();
const svg = new El(); svg.dataset = {};
const documentStub = {
  querySelector(sel) {
    if (sel === '#panel svg') return svg;
    if (!docCache.has(sel)) docCache.set(sel, new El());
    return docCache.get(sel);
  },
  querySelectorAll() { return []; },
  addEventListener() {},
  elementFromPoint() { return null; },
};
function param() { return { value: 0, setTargetAtTime() {}, setValueAtTime() {}, exponentialRampToValueAtTime() {} }; }
function node() { return { gain: param(), frequency: param(), delayTime: param(), Q: param(), type: '', buffer: null, playbackRate: param(), connect() {}, start() {}, stop() {} }; }
class AudioContextStub {
  constructor() { this.currentTime = 0; this.sampleRate = 44100; this.destination = {}; this.state = 'running'; }
  resume() {}
  createGain() { return node(); } createBiquadFilter() { return node(); }
  createDelay() { return node(); } createOscillator() { return node(); }
  createBufferSource() { return node(); }
  createBuffer(ch, len) { return { getChannelData: () => new Float32Array(len) }; }
}
const winListeners = {};
const sandbox = {
  console, performance, setInterval, clearInterval, setTimeout, clearTimeout,
  requestAnimationFrame: () => {}, localStorage: { getItem: () => null, setItem: () => {} },
  navigator: {}, AudioContext: AudioContextStub,
  document: documentStub,
  addEventListener: (t, f) => { (winListeners[t] ||= []).push(f); },
};
sandbox.window = sandbox; sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(script, sandbox);

/* ---- driver (runs inside the context so it can see MPC/svg/consts) ---- */
const driver = `
MPC.sound = false;
const events = [];
MPC.on('*', (type, d) => events.push([type, JSON.parse(JSON.stringify(d || {}))]));
function mkEl(dataset) {
  const el = document.querySelector('#panel svg'); // placeholder, replaced below
  return null;
}
function fire(target, type, ev) {
  ev.target = target; ev.preventDefault = ev.preventDefault || (() => {});
  (svg.listeners[type] || []).forEach(f => f(ev));
}
function ctlEl(ctl, id) {
  const el = document.querySelector('#unused-' + ctl + '-' + (id || ''));
  el.dataset = { ctl, id };
  el.closest = sel => sel.includes('data-ctl') ? el : null;
  return el;
}
const pad = ctlEl('pad', '1'), btn = ctlEl('button', 'BROWSE'),
      knob = ctlEl('knob', 'P1'), strip = ctlEl('strip', ''),
      jog = ctlEl('jog', ''), curR = ctlEl('button', 'CUR_RIGHT'),
      curL = ctlEl('button', 'CUR_LEFT'), curU = ctlEl('button', 'CUR_UP'),
      curD = ctlEl('button', 'CUR_DOWN');
const pt = (el, x, y, pid = 1) => ({ target: el, clientX: x, clientY: y, pointerId: pid, preventDefault() {} });
const has = (type, pred) => events.some(([t, d]) => t === type && pred(d));
const count = (type, pred) => events.filter(([t, d]) => t === type && pred(d)).length;
const checks = [];
function check(name, ok) { checks.push([name, !!ok]); }

// pad down/up
fire(pad, 'pointerdown', pt(pad, 50, 10)); check('pad down emits pad down', has('pad', d => d.pad === 1 && d.down));
fire(pad, 'pointerup', pt(pad, 50, 10));   check('pad up emits pad up', has('pad', d => d.pad === 1 && !d.down));
// button down/up
fire(btn, 'pointerdown', pt(btn, 50, 50)); check('button down emits', has('button', d => d.id === 'BROWSE' && d.down));
fire(btn, 'pointerup', pt(btn, 50, 50));   check('button up emits', has('button', d => d.id === 'BROWSE' && !d.down));
// knob drag (y 100 -> 40 = value up)
fire(knob, 'pointerdown', pt(knob, 50, 100));
fire(knob, 'pointermove', pt(knob, 50, 40));
check('knob drag emits knob value > .5', has('knob', d => d.id === 'P1' && d.value > .5));
// knob wheel
const before = MPC.knobs.P1;
fire(knob, 'wheel', { target: knob, deltaY: -100, shiftKey: false, preventDefault() {} });
check('knob wheel steps value', MPC.knobs.P1 > before);
// strip down + drag
fire(strip, 'pointerdown', pt(strip, 50, 25)); check('strip down emits value .75', has('strip', d => Math.abs(d.value - .75) < .01));
fire(strip, 'pointermove', pt(strip, 50, 75)); check('strip drag emits value .25', has('strip', d => Math.abs(d.value - .25) < .01));
// jog drag: rotate 0deg -> 40deg in two moves (detents at 12deg steps)
events.length = 0;
fire(jog, 'pointerdown', pt(jog, 100, 50));
fire(jog, 'pointermove', pt(jog, 50 + 50 * Math.cos(.7), 50 + 50 * Math.sin(.7)));
check('jog drag emits jog detent', has('jog', d => d.delta === 1));
fire(jog, 'pointerup', pt(jog, 50 + 50 * Math.cos(.7), 50 + 50 * Math.sin(.7)));
check('jog drag does NOT emit jogpush on release', count('jogpush', () => true) === 0);
// jog plain click: down+up at same point
events.length = 0;
fire(jog, 'pointerdown', pt(jog, 100, 50));
fire(jog, 'pointerup', pt(jog, 100, 50));
check('jog click emits jogpush down', has('jogpush', d => d.down === true));
check('jog click emits jogpush up after down',
  count('jogpush', () => true) === 2 &&
  events.findIndex(([t, d]) => t === 'jogpush' && d.down) < events.findIndex(([t, d]) => t === 'jogpush' && !d.down));
check('jog click emits no jog detent', count('jog', () => true) === 0);
// jog sub-detent wiggle (5 degrees): still a click
events.length = 0;
fire(jog, 'pointerdown', pt(jog, 100, 50));
fire(jog, 'pointermove', pt(jog, 50 + 50 * Math.cos(.087), 50 + 50 * Math.sin(.087)));
fire(jog, 'pointerup', pt(jog, 50 + 50 * Math.cos(.087), 50 + 50 * Math.sin(.087)));
check('jog wiggle <12deg emits no detent', count('jog', () => true) === 0);
check('jog wiggle still emits jogpush (click)', count('jogpush', () => true) === 2);
// cursor arrows -> jog steps
for (const [el, want, name] of [[curR, 1, 'CUR_RIGHT -> jog +1'], [curL, -1, 'CUR_LEFT -> jog -1'], [curU, 1, 'CUR_UP -> jog +1'], [curD, -1, 'CUR_DOWN -> jog -1']]) {
  events.length = 0;
  fire(el, 'pointerdown', pt(el, 50, 50)); fire(el, 'pointerup', pt(el, 50, 50));
  check(name, has('jog', d => d.delta === want));
}
globalThis.__checks = checks;
`;
vm.runInContext(driver, sandbox);
let pass = 0;
for (const [name, ok] of sandbox.__checks) { console.log((ok ? 'PASS ' : 'FAIL ') + name); if (ok) pass++; }
console.log(`${pass}/${sandbox.__checks.length} checks passed`);
process.exit(pass === sandbox.__checks.length ? 0 : 1);
