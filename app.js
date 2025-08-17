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

async function readAll() {
  if (!auth || !currentDeviceId) return;
  try {
    const [bp, bv, bs, c, s] = await Promise.all([
      getVariable(currentDeviceId, "battPct",  auth),
      getVariable(currentDeviceId, "battV",    auth),
      getVariable(currentDeviceId, "battState",auth),
      getVariable(currentDeviceId, "carrier",  auth),
      getVariable(currentDeviceId, "sigPct",   auth),
    ]);

    // Battery
    const pct = Number(bp.result);
    setText("battPctOut", isFinite(pct) ? `${pct.toFixed(1)} %` : String(bp.result));

    const v = Number(bv.result);
    setText("battVOut", isFinite(v) ? `${v.toFixed(3)} V` : String(bv.result));

    // Battery state (numeric fallback mapping)
    const stateNum = Number(bs.result);
    const stateStr = battStatePretty(stateNum);
    setText("battStateOut", stateStr);

    // Carrier & signal
    const raw = String(c.result || "");
    const cleaned = raw.replace(/[^\x20-\x7E]/g, "");
    setText("carrierOut", PLMN[cleaned] || cleaned || "unknown");

    const sig = Number(s.result);
    setText("sigOut", isFinite(sig) ? `${sig}%` : String(s.result));

    setText("varMsg", "");
  } catch (e) {
    console.error(e);
    setText("varMsg", `Read failed: ${e.message || e}`);
  }
}
function startAutoReads() {
  if (autoTimer) clearInterval(autoTimer);
  autoTimer = setInterval(() => {
    if (Date.now() - lastEventTs > 15000) readAll(); // fallback if no events in 15s
  }, 60000); // check once per minute
  readAll(); // initial
}

// ---- Functions (LED) ----
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
    setText("funcMsg", `Call failed: ${e.message || e}`);
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
          setText("evtMsg", "Event stream ended (device offline or network change).");
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
}

function scheduleEvtReconnect() {
  stopEventStream();
  setTimeout(startEventStream, backoffMs);
  backoffMs = Math.min(backoffMs * 2, backoffMax);
}

// Pause SSE when tab hidden to save data
document.addEventListener("visibilitychange", () => {
  if (document.hidden) stopEventStream();
  else startEventStream();
});

// ---- Helpers ----
function prettify(s) {
  return String(s).replace(/_/g, " ").replace(/\b\w/g, c => c.toUpperCase());
}
function battStatePretty(n) {
  // Fallback map if we only have the numeric variable
  const m = {
    0: "Unknown",
    1: "Not Charging",
    2: "Charging",
    3: "Charged",
    4: "Discharging",
    5: "Fault",
    6: "Disconnected"
  };
  return m[n] || "Unknown";
}

// ---- Bootstrap ----
function bootstrap(tok) {
  auth = tok;
  refreshDevices();
}

document.addEventListener("DOMContentLoaded", () => {
  $("saveToken").addEventListener("click", saveToken);
  $("clearToken").addEventListener("click", clearToken);
  $("refreshDevices").addEventListener("click", refreshDevices);
  $("refreshNow").addEventListener("click", readAll);
  document.querySelectorAll(".ledBtn").forEach(btn => {
    btn.addEventListener("click", onLedClick);
  });

  const tokHash = loadTokenFromHash();
  const tok = tokHash || loadToken();
  if (tok) bootstrap(tok);
});
