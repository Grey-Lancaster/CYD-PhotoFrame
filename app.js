// ---- Config ----
const API = "https://api.particle.io/v1";
const TOKEN_KEY = "particle_token";

// Minimal PLMN -> name map
const PLMN = {
  "310260": "T-Mobile USA",
  "310410": "AT&T",
  "313100": "FirstNet (AT&T)",
  "311480": "Verizon",
  "310120": "Sprint (legacy/T-Mobile)"
};

// ---- DOM helpers ----
const $ = (id) => document.getElementById(id);
const setText = (id, t) => { const el = $(id); if (el) el.innerText = t; };
const appendLog = (msg) => {
  const el = $("log"); if (!el) return;
  el.innerText += msg + "\n"; el.scrollTop = el.scrollHeight;
};

// ---- Token handling ----
function loadTokenFromHash() {
  const m = (location.hash || "").match(/[#&]token=([^&]+)/);
  if (m) {
    const tok = decodeURIComponent(m[1]);
    localStorage.setItem(TOKEN_KEY, tok);
    $("token").value = tok;
    return tok;
  }
  return null;
}
function loadToken() {
  const tok = localStorage.getItem(TOKEN_KEY) || "";
  $("token").value = tok;
  return tok || null;
}
function saveToken() {
  const tok = $("token").value.trim();
  if (!tok) { setText("authMsg", "Paste a token first."); return; }
  localStorage.setItem(TOKEN_KEY, tok);
  setText("authMsg", "Token saved."); setTimeout(() => setText("authMsg",""), 1200);
  bootstrap(tok);
}
function clearToken() {
  localStorage.removeItem(TOKEN_KEY);
  $("token").value = "";
  setText("authMsg", "Token cleared.");
}

// ---- API helpers ----
function authHeaders(token) { return { "Authorization": `Bearer ${token}` }; }

async function apiGetJson(url, token) {
  const res = await fetch(url, { headers: authHeaders(token) });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}
async function apiPostForm(url, token, data) {
  const res = await fetch(url, {
    method: "POST",
    headers: { ...authHeaders(token), "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(data).toString()
  });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}

// ---- Particle endpoints ----
async function listDevices(token) {
  return apiGetJson(`${API}/devices`, token); // array
}
async function getVariable(deviceId, name, token) {
  return apiGetJson(`${API}/devices/${deviceId}/${name}`, token); // {result,...}
}
async function callFunction(deviceId, name, arg, token) {
  return apiPostForm(`${API}/devices/${deviceId}/${name}`, token, { arg }); // Particle expects 'arg'
}

// ---- State ----
let auth = null;
let currentDeviceId = null;
let autoTimer = null;
let evtAbort = null;
let lastEventTs = 0;
let latestNextWake = 0; // epoch seconds from device event/vars
let backoffMs = 1500;
const backoffMax = 30000;

// ---- Devices / reads ----
async function refreshDevices() {
  if (!auth) { setText("authMsg", "No token. Paste and Save first."); return; }
  try {
    const list = await listDevices(auth);
    const sel = $("deviceSelect");
    sel.innerHTML = "";
    if (!list.length) { setText("authMsg", "No devices for this token."); return; }

    for (const d of list) {
      const opt = document.createElement("option");
      opt.value = d.id;
      opt.textContent = `${d.name || d.id} ${d.online ? "🟢" : "⚪"}`;
      sel.appendChild(opt);
    }
    const preferred = list.find(d => (d.name || "").toLowerCase() === "grey_fox_1");
    const chosen = preferred || list[0];
    sel.value = chosen.id;
    setDevice(chosen.id, chosen.online);
    setText("authMsg", "");
  } catch (e) {
    console.error(e);
    setText("authMsg", `Device list failed: ${e.message || e}`);
  }
}
function setDevice(id, online) {
  currentDeviceId = id;
  setText("selectedId", id);
  $("onlineBadge").innerText = online ? "🟢 online" : "⚪ offline";
  startEventStream();
  startAutoReads();
}

// ---- Read variables (fallback path when events are quiet)
async function readAll() {
  if (!auth || !currentDeviceId) return;
  try {
    const [bp, bv, bs, ob, lw, nw, c, s] = await Promise.all([
      getVariable(currentDeviceId, "battPct",      auth),
      getVariable(currentDeviceId, "battV",        auth),
      getVariable(currentDeviceId, "battStateStr", auth),
      getVariable(currentDeviceId, "onBatterySec", auth),
      getVariable(currentDeviceId, "lastWake",     auth),
      getVariable(currentDeviceId, "nextWake",     auth),
      getVariable(currentDeviceId, "carrier",      auth),
      getVariable(currentDeviceId, "sigPct",       auth),
    ]);

    // Battery
    const pct = Number(bp.result);
    setText("battPctOut", isFinite(pct) ? `${pct.toFixed(1)} %` : String(bp.result));

    const v = Number(bv.result);
    setText("battVOut", isFinite(v) ? `${v.toFixed(3)} V` : String(bv.result));

    setText("battStateOut", prettify(String(bs.result || "unknown")));

    // Time & runtime
    const obSec = Number(ob.result) || 0;
    setText("onBattOut", toHMS(obSec));

    const lastW = Number(lw.result) || 0;
    const nextW = Number(nw.result) || 0;
    latestNextWake = nextW;
    setText("lastWakeOut", lastW ? new Date(lastW*1000).toLocaleString() : "—");
    setText("nextWakeOut", nextW ? new Date(nextW*1000).toLocaleString() : "—");

    // Carrier & signal
    const raw = String(c.result || "");
    const cleaned = raw.replace(/[^\x20-\x7E]/g, "");
    setText("carrierOut", PLMN[cleaned] || cleaned || "unknown");

    const sig = Number(s.result);
    setText("sigOut", isFinite(sig) ? `${sig}%` : String(s.result));

    setAwakeBadge();
    setText("varMsg", "");
  } catch (e) {
    console.error(e);
    setText("varMsg", `Read failed: ${e.message || e}`);
  }
}
function startAutoReads() {
  if (autoTimer) clearInterval(autoTimer);
  autoTimer = setInterval(() => {
    // Fallback polling if no events in 20s
    if (Date.now() - lastEventTs > 20000) readAll();
    setAwakeBadge();
  }, 60000); // check once per minute
  readAll(); // initial
}

// ---- Functions (LED + clrlog) ----
async function onLedClick(ev) {
  const arg = ev.currentTarget?.dataset?.arg;
  if (!auth || !currentDeviceId || !arg) { setText("funcMsg", "LED call missing info."); return; }
  try {
    const res = await callFunction(currentDeviceId, "led", arg, auth);
    setText("funcMsg", `Return: ${res.return_value}`);
    $("ledState").innerText = `LED: ${arg.toUpperCase()}`;
    setTimeout(() => setText("funcMsg",""), 1200);
  } catch (e) {
    console.error(e);
    setText("funcMsg", `Call failed (device sleeping?): ${e.message || e}`);
  }
}
async function onClearLog() {
  if (!auth || !currentDeviceId) return;
  try {
    const res = await callFunction(currentDeviceId, "clrlog", "", auth);
    setText("funcMsg", `clrlog ok (${res.return_value})`);
    setTimeout(() => setText("funcMsg",""), 1500);
  } catch (e) {
    setText("funcMsg", `clrlog failed (sleeping?): ${e.message || e}`);
  }
}

// ---- Events (SSE via fetch + Authorization header) ----
function stopEventStream() {
  if (evtAbort) evtAbort.abort();
  evtAbort = null;
}
async function startEventStream() {
  stopEventStream();
  if (!auth || !currentDeviceId) return;

  const url = `${API}/devices/${currentDeviceId}/events/status`;
  evtAbort = new AbortController();
  const { signal } = evtAbort;

  try {
    const res = await fetch(url, {
      method: "GET",
      headers: { ...authHeaders(auth), "Accept": "text/event-stream" },
      signal
    });
    if (!res.ok || !res.body) {
      setText("evtMsg", `Event stream open failed: HTTP ${res.status}`);
      scheduleEvtReconnect();
      return;
    }
    setText("evtMsg", "");
    appendLog(`(listening to status events for ${currentDeviceId})`);
    backoffMs = 1500;

    const reader = res.body.getReader();
    const decoder = new TextDecoder("utf-8");
    let buf = "";
    (function pump() {
      reader.read().then(({ done, value }) => {
        if (done) {
          setText("evtMsg", "Event stream ended (device asleep/offline or network change).");
          scheduleEvtReconnect();
          return;
        }
        buf += decoder.decode(value, { stream: true });
        const lines = buf.split("\n");
        buf = lines.pop() || "";
        for (const line of lines) {
          if (line.startsWith("data:")) {
            const data = line.slice(5).trim();
            if (data) handleEventData(data);
          }
        }
        pump();
      }).catch(err => {
        setText("evtMsg", `Event stream error: ${err.message || err}`);
        scheduleEvtReconnect();
      });
    })();
  } catch (e) {
    setText("evtMsg", `Event stream error: ${e.message || e}`);
    scheduleEvtReconnect();
  }
}
function handleEventData(data) {
  try {
    const j = JSON.parse(data);

    if (typeof j.battPct === "number") setText("battPctOut", `${j.battPct.toFixed(1)} %`);
    if (typeof j.battV   === "number") setText("battVOut",   `${j.battV.toFixed(3)} V`);
    if (j.battStateStr) setText("battStateOut", prettify(j.battStateStr));
    if (typeof j.onBatterySec === "number") setText("onBattOut", toHMS(j.onBatterySec));

    if (j.lastWake) setText("lastWakeOut", new Date(j.lastWake*1000).toLocaleString());
    if (j.nextWake) { setText("nextWakeOut", new Date(j.nextWake*1000).toLocaleString()); latestNextWake = j.nextWake; }

    if (typeof j.sigPct === "number") setText("sigOut", `${j.sigPct}%`);
    if (j.carrier) {
      const cleaned = String(j.carrier).replace(/[^\x20-\x7E]/g, "");
      setText("carrierOut", PLMN[cleaned] || cleaned || "unknown");
    }
    if (j.led) $("ledState").innerText = `LED: ${String(j.led).toUpperCase()}`;
  } catch {
    // Old/non-JSON event fallback: just log it
    appendLog(`[${new Date().toLocaleTimeString()}] ${data}`);
  }
  lastEventTs = Date.now();
  setAwakeBadge();
}
function scheduleEvtReconnect() {
  stopEventStream();
  setTimeout(startEventStream, backoffMs);
  backoffMs = Math.min(backoffMs * 2, backoffMax);
}

// Pause SSE when tab hidden to save network
document.addEventListener("visibilitychange", () => {
  if (document.hidden) stopEventStream();
  else startEventStream();
});

// ---- UI helpers ----
function prettify(s) {
  return String(s).replace(/_/g, " ").replace(/\b\w/g, c => c.toUpperCase());
}
function toHMS(sec) {
  sec = Math.max(0, Math.floor(Number(sec)||0));
  const h = Math.floor(sec/3600);
  const m = Math.floor((sec%3600)/60);
  const s = sec%60;
  if (h) return `${h}h ${m}m`;
  if (m) return `${m}m ${s}s`;
  return `${s}s`;
}
function setAwakeBadge() {
  const badge = $("awakeBadge");
  // If we received an event in the last 10s, likely awake.
  const recentEvent = (Date.now() - lastEventTs) < 100
