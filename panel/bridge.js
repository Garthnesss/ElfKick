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

  function connect() {
    let sock;
    try { sock = new WebSocket(`ws://${host}:${WS_PORT}/`); }
    catch (e) { scheduleRetry(); return; }
    ws = sock;
    ws.onopen = () => {
      bridged = true; retryMs = 500;
      MPC.sound = false;                       // the runtime makes the sound now
      const sndBtn = document.querySelector('#sound');
      if (sndBtn) { sndBtn.classList.remove('on'); sndBtn.textContent = 'Sound: runtime'; }
      setBadge('bridge: connected — panel drives the ElfKick runtime', true);
      send({ t: 'hello', knobs: { ...MPC.knobs } });
      console.log('[bridge] connected; built-in kit silenced (MPC.sound=false)');
    };
    ws.onmessage = (ev) => {
      let msg; try { msg = JSON.parse(ev.data); } catch { return; }
      if (msg.t === 'led') MPC.setLed(msg.id, msg.on);
      else if (msg.t === 'padlight') MPC.setPad(msg.pad, msg.color, msg.level);
      else if (msg.t === 'log') console.log('[bridge]', msg.msg);
    };
    ws.onclose = ws.onerror = () => {
      if (bridged) console.log('[bridge] disconnected');
      bridged = false;
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

  window.MPC_BRIDGE = { get connected() { return bridged; }, send };
  connect();
})();
