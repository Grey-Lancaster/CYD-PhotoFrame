// ---- Config ----
const API = "https://api.particle.io/v1";
const TOKEN_KEY = "particle_token";

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
const appendLog = (msg) => { const el = $("log"); if (!el) return; el.innerText += msg + "\n"; el.scrollTop = el.scrollHeight; };

// ---- Token handling ----
function loadTokenFromHash() {
  const m = (location.hash || "").match(/[#&]token=([^&]+)/);
  if (m) { const tok = decodeURIComponent(m[1]); localStorage.setItem(TOKEN_KEY, tok); $("token").value = tok; return tok; }
  return null;
}
function loadToken() { const tok = localStorage.getItem(TOKEN_KEY) || ""; $("token").value = tok; return tok || null; }
function saveToken() {
  const tok = $("token").value.trim();
  if (!tok) { setText("authMsg", "Paste a token first."); return; }
  localStorage.setItem(TOKEN_KEY, tok);
  setText("authMsg", "Token saved."); setTimeout(() => setText("authMsg",""), 1200);
  bootstrap(tok);
}
function clearToken() { localStorage.removeItem(TOKEN_KEY); $("token").value = ""; setText("authMsg", "Token cleared."); }

// ---- API helpers ----
function authHeaders(token) { return { "Authorization": `Bearer ${token}` }; }
async function apiGetJson(url, token) { const r = await fetch(url, { headers: authHeaders(token) }); if (!r.ok) throw new Error(`HTTP ${r.status}`); return r.json(); }
async function apiPostForm(url, token, data) {
  const r = await fetch(url, { method: "POST", headers: { ...authHeaders(token), "Content-Type": "application/x-www-form-urlencoded" }, body: new URLSearchParams(data).toString() });
  if (!r.ok) throw new Error(`HTTP ${r.status}`); return r.json();
}

// ---- Particle endpoints ----
async function listDevices(t) { return apiGetJson(`${API}/devices`, t); }
async function getVariable(id, name, t) { return apiGetJson(`${API}/devices/${id}/${name}`, t); }
async function callFunction(id, name, arg, t) { return apiPostForm(`${API}/devices/${id}/${name}`, t, { arg }); }

// ---- State ----
let auth=null, currentDeviceId=null, autoTimer=null, evtAbort=null, lastEventTs=0, backoffMs=1500;
const backoffMax=30000;

// ---- Devices ----
async function refreshDevices() {
  if (!auth) { setText("authMsg", "No token. Paste and Save first."); return; }
  try {
    const list = await listDevices(auth);
    const sel = $("deviceSelect");
    sel.innerHTML = "";
    if (!list.length) { setText("authMsg", "No devices for this token."); return; }

    for (const d of list) {
      const opt = document.createElement("option");
      opt.value = d.id; opt.textContent = `${d.name || d.id} ${d.online ? "🟢" : "⚪"}`;
      sel.appendChild(opt);
    }
    const preferred = list.find(d => (d.name || "").toLowerCase() === "grey_fox_1");
    const chosen = preferred || list[0];
    sel.value = chosen.id;
    setDevice(chosen.id, chosen.online);
    sel.onchange = () => setDevice(sel.value, true);
    setText("authMsg", "");
  } catch (e) { console.error(e); setText("authMsg", `Device list failed: ${e.message || e}`); }
}
function setDevice(id, online) {
  currentDeviceId = id;
  setText("selectedId", id);
  $("onlineBadge").innerText = online ? "🟢 online" : "⚪ offline";
  startEventStream();
  startAutoReads();
}

// ---- Reads (fallback polling) ----
async function readAll() {
  if (!auth || !currentDeviceId) return;
  try {
    const [bp,bv,bs,ob,c,s] = await Promise.all([
      getVariable(currentDeviceId,"battPct",auth),
      getVariable(currentDeviceId,"battV",auth),
      getVariable(currentDeviceId,"battStateStr",auth),
      getVariable(currentDeviceId,"onBatterySec",auth),
      getVariable(currentDeviceId,"carrier",auth),
      getVariable(currentDeviceId,"sigPct",auth),
    ]);
    const pct = Number(bp.result); setText("battPctOut", isFinite(pct)?`${pct.toFixed(1)} %`:String(bp.result));
    const v   = Number(bv.result); setText("battVOut", isFinite(v)?`${v.toFixed(3)} V`:String(bv.result));
    setText("battStateOut", prettify(String(bs.result||"unknown")));
    const obSec = Number(ob.result)||0; setText("onBattOut", toHMS(obSec));
    const raw = String(c.result||""); const cleaned = raw.replace(/[^\x20-\x7E]/g,""); setText("carrierOut", PLMN[cleaned] || cleaned || "unknown");
    const sig = Number(s.result); setText("sigOut", isFinite(sig)?`${sig}%`:String(s.result));
    setText("varMsg","");
  } catch(e){ console.error(e); setText("varMsg", `Read failed: ${e.message||e}`); }
}
function startAutoReads(){
  if(autoTimer) clearInterval(autoTimer);
  autoTimer = setInterval(() => { if(Date.now()-lastEventTs>20000) readAll(); }, 60000); // poll once/min if quiet
  readAll();
}

// ---- Functions ----
async function onLedClick(ev){
  const arg = ev.currentTarget?.dataset?.arg;
  if(!auth || !currentDeviceId || !arg){ setText("funcMsg","LED call missing info."); return; }
  try {
    const r = await callFunction(currentDeviceId, "led", arg, auth);
    setText("funcMsg", `Return: ${r.return_value}`);
    $("ledState").innerText = `LED: ${arg.toUpperCase()}`;
    setTimeout(()=>setText("funcMsg",""),1200);
  } catch(e){ setText("funcMsg", `Call failed: ${e.message||e}`); }
}
async function onClearLog(){
  if(!auth || !currentDeviceId) return;
  try { const r=await callFunction(currentDeviceId,"clrlog","",auth);
    setText("funcMsg",`clrlog ok (${r.return_value})`); setTimeout(()=>setText("funcMsg",""),1500);
  } catch(e){ setText("funcMsg", `clrlog failed: ${e.message||e}`); }
}

// ---- Events (SSE) ----
function stopEventStream(){ if(evtAbort) evtAbort.abort(); evtAbort=null; }
async function startEventStream(){
  stopEventStream(); if(!auth || !currentDeviceId) return;
  const url = `${API}/devices/${currentDeviceId}/events/status`;
  evtAbort = new AbortController(); const {signal} = evtAbort;
  try{
    const res = await fetch(url,{ method:"GET", headers:{...authHeaders(auth),"Accept":"text/event-stream"}, signal });
    if(!res.ok || !res.body){ setText("evtMsg",`Event stream open failed: HTTP ${res.status}`); scheduleEvtReconnect(); return; }
    setText("evtMsg",""); appendLog(`(listening to status events for ${currentDeviceId})`); backoffMs=1500;
    const reader=res.body.getReader(); const dec=new TextDecoder("utf-8"); let buf="";
    (function pump(){
      reader.read().then(({done,value})=>{
        if(done){ setText("evtMsg","Event stream ended."); scheduleEvtReconnect(); return; }
        buf+=dec.decode(value,{stream:true}); const lines=buf.split("\n"); buf=lines.pop()||"";
        for(const line of lines){ if(line.startsWith("data:")){ const data=line.slice(5).trim(); if(data) handleEventData(data); } }
        pump();
      }).catch(err=>{ setText("evtMsg",`Event stream error: ${err.message||err}`); scheduleEvtReconnect(); });
    })();
  }catch(e){ setText("evtMsg",`Event stream error: ${e.message||e}`); scheduleEvtReconnect(); }
}
function handleEventData(data){
  try{
    const j = JSON.parse(data);
    if(typeof j.battPct==="number") setText("battPctOut", `${j.battPct.toFixed(1)} %`);
    if(typeof j.battV==="number")   setText("battVOut",   `${j.battV.toFixed(3)} V`);
    if(j.battStateStr) setText("battStateOut", prettify(j.battStateStr));
    if(typeof j.onBatterySec==="number") setText("onBattOut", toHMS(j.onBatterySec));
    if(typeof j.sigPct==="number") setText("sigOut", `${j.sigPct}%`);
    if(j.carrier){ const cleaned=String(j.carrier).replace(/[^\x20-\x7E]/g,""); setText("carrierOut", PLMN[cleaned]||cleaned||"unknown"); }
    if(j.led) $("ledState").innerText = `LED: ${String(j.led).toUpperCase()}`;
  }catch{ appendLog(`[${new Date().toLocaleTimeString()}] ${data}`); }
  lastEventTs = Date.now();
}
function scheduleEvtReconnect(){ stopEventStream(); setTimeout(startEventStream, backoffMs); backoffMs=Math.min(backoffMs*2, backoffMax); }

// ---- UI helpers ----
function prettify(s){ return String(s).replace(/_/g," ").replace(/\b\w/g,c=>c.toUpperCase()); }
function toHMS(sec){ sec=Math.max(0,Math.floor(Number(sec)||0)); const h=Math.floor(sec/3600), m=Math.floor((sec%3600)/60), s=sec%60; if(h) return `${h}h ${m}m`; if(m) return `${m}m ${s}s`; return `${s}s`; }

// ---- Bootstrap ----
function bootstrap(tok){ auth=tok; refreshDevices(); }
document.addEventListener("DOMContentLoaded", () => {
  $("saveToken").addEventListener("click", saveToken);
  $("clearToken").addEventListener("click", clearToken);
  $("refreshDevices").addEventListener("click", refreshDevices);
  $("refreshNow").addEventListener("click", readAll);
  $("clearLogBtn").addEventListener("click", onClearLog);
  document.querySelectorAll(".ledBtn").forEach(b=>b.addEventListener("click", onLedClick));
  const tokHash = loadTokenFromHash(); const tok = tokHash || loadToken(); if(tok) bootstrap(tok);
});
