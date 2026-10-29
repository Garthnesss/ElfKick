/* ElfKick web-panel bridge glue (panel/BRIDGE.md).
 *
 * Connects window.MPC to the Python bridge (bridge.py) over WebSocket:
 *   MPC events  -> JSON -> bridge -> PktMgr frames -> KickDeviceControl
 *   bridge JSON -> MPC.setLed / MPC.setPad  (runtime LED/pad feedback)
 *
 * While connected, MPC.sound is set false — the sound you hear is the
 * real runtime's (the panel's built-in kit is only for standalone use).
 * If the bridge is unreachable the panel keeps working standalone.
 */
(() => {
  const WS_PORT = 8794;
  const host = location.hostname || '127.0.0.1';
  let ws = null, bridged = false, retryMs = 500;

  const badge = document.createElement('span');
  badge.id = 'bridge-status';
  badge.textContent = 'bridge: connecting…';
  badge.style.cssText = 'color:#8a8a92;font-size:12px';
  document.querySelector('header')?.appendChild(badge);

  function setBadge(txt, ok) {
    badge.textContent = txt;
    badge.style.color = ok ? '#5dff9a' : '#8a8a92';
  }

  function send(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
  }

  /* ---- screen streaming: the real GUI, drawn into MPC.screen ----
   * The bridge captures the KickUserInterface window (1024x600) and
   * sends JPEG frames as BINARY WebSocket messages. We ask for the
   * stream on connect; when the bridge confirms it, take the screen
   * over (MPC.takeScreen stops the built-in display) and paint frames
   * letterboxed — the source is 1024x600, the canvas 1184x680.
   * When the stream stops (or the bridge goes away) we hand the
   * screen back: MPC.ownScreen = false restores the built-in display,
   * which redraws on the next panel interaction. */
  let screenCtx = null, screenTaken = false, frameBusy = false;

  function startScreen() {
    if (screenTaken) return;
    screenTaken = true;
    MPC.takeScreen();
    screenCtx = MPC.screen.getContext('2d');
    screenCtx.fillStyle = '#000';
    screenCtx.fillRect(0, 0, MPC.screen.width, MPC.screen.height);
    console.log('[bridge] screen stream on — showing the real Kick GUI');
  }
  function stopScreen() {
    if (!screenTaken) return;
    screenTaken = false; screenCtx = null;
    MPC.ownScreen = false;                     // hand the display back
    console.log('[bridge] screen stream off — built-in display restored');
  }
  function paintFrame(img) {
    if (!screenCtx) return;
    const W = MPC.screen.width, H = MPC.screen.height;
    const s = Math.min(W / img.width, H / img.height);
    const w = Math.round(img.width * s), h = Math.round(img.height * s);
    screenCtx.fillStyle = '#000';
    screenCtx.fillRect(0, 0, W, H);
    screenCtx.drawImage(img, (W - w) / 2, (H - h) / 2, w, h);
  }
  function drawFrame(data) {
    if (!screenTaken || frameBusy) return;     // drop while decoding — latest wins
    frameBusy = true;
    const done = () => { frameBusy = false; };
    const blob = data instanceof Blob ? data : new Blob([data], { type: 'image/jpeg' });
    if (window.createImageBitmap) {
      createImageBitmap(blob).then(bmp => { paintFrame(bmp); bmp.close(); done(); }).catch(done);
    } else {
      const url = URL.createObjectURL(blob);
      const img = new Image();
      img.onload = () => { paintFrame(img); URL.revokeObjectURL(url); done(); };
      img.onerror = () => { URL.revokeObjectURL(url); done(); };
      img.src = url;
    }
  }

  function connect() {
    let sock;
    try { sock = new WebSocket(`ws://${host}:${WS_PORT}/`); }
    catch (e) { scheduleRetry(); return; }
    ws = sock;
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => {
      bridged = true; retryMs = 500;
      MPC.sound = false;                       // the runtime makes the sound now
      const sndBtn = document.querySelector('#sound');
      if (sndBtn) { sndBtn.classList.remove('on'); sndBtn.textContent = 'Sound: runtime'; }
      setBadge('bridge: connected — panel drives the ElfKick runtime', true);
      send({ t: 'hello', knobs: { ...MPC.knobs } });
      send({ t: 'stream', on: true });         // ask for the GUI screen stream
      console.log('[bridge] connected; built-in kit silenced (MPC.sound=false)');
    };
    ws.onmessage = (ev) => {
      if (ev.data instanceof ArrayBuffer || ev.data instanceof Blob) { drawFrame(ev.data); return; }
      let msg; try { msg = JSON.parse(ev.data); } catch { return; }
      if (msg.t === 'led') MPC.setLed(msg.id, msg.on);
      else if (msg.t === 'padlight') MPC.setPad(msg.pad, msg.color, msg.level);
      else if (msg.t === 'stream') { if (msg.on) startScreen(); else stopScreen(); }
      else if (msg.t === 'log') console.log('[bridge]', msg.msg);
    };
    ws.onclose = ws.onerror = () => {
      if (bridged) console.log('[bridge] disconnected');
      bridged = false;
      stopScreen();
      setBadge('bridge: disconnected (panel standalone)', false);
      scheduleRetry();
    };
  }
  function scheduleRetry() {
    setTimeout(connect, retryMs);
    retryMs = Math.min(retryMs * 2, 5000);
  }

  MPC.on('pad',    d => send({ t: 'pad',    pad: d.pad, vel: d.velocity, down: d.down }));
  MPC.on('button', d => send({ t: 'button', id: d.id, down: d.down }));
  MPC.on('knob',   d => send({ t: 'knob',   id: d.id, value: d.value }));
  MPC.on('jog',    d => send({ t: 'jog',    delta: d.delta }));
  MPC.on('strip',  d => send({ t: 'strip',  value: d.value }));
  /* Anything else the panel emits (e.g. 'aftertouch', or a future
   * screen-touch event) has no bridge mapping yet — log it, don't
   * forward. Touch-back from the streamed screen is out of scope:
   * the stream is display-out only. */
  const FORWARDED = ['pad', 'button', 'knob', 'jog', 'strip'];
  MPC.on('*', (type) => {
    if (!FORWARDED.includes(type)) console.log('[bridge] unhandled panel event:', type);
  });

  window.MPC_BRIDGE = { get connected() { return bridged; }, send };
  connect();
})();
