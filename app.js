// === Configuration ===
const TOKEN  = 'c5faa926e596e02388dc06e8cfc333dbac4d785f'; // your Particle token
const DEVICE = 'Grey_Fox_1';                                 // name or device ID
const API    = 'https://api.particle.io/v1';
const LOG_MINUTES = 10; // matches firmware (10-min logging, discharge-only)

// ===== Helpers =====
const $ = s => document.querySelector(s);
const msg = (t, cls='ok') => { const el=$('#msg'); el.textContent=t; el.className=cls; };

function fmtSecs(s){
  if(!Number.isFinite(s)||s<=0) return '0s';
  const h=Math.floor(s/3600), m=Math.floor((s%3600)/60), ss=Math.floor(s%60);
  if(h) return `${h}h ${m}m`; if(m) return `${m}m ${ss}s`; return `${ss}s`;
}

async function fetchJSON(url, opt={}){
  // Prefer Authorization header; no query token here.
  const headers = Object.assign({}, opt.headers||{}, { 'Authorization': `Bearer ${TOKEN}` });
  const r = await fetch(url, Object.assign({}, opt, { headers }));
  let bodyText = '';
  try { bodyText = await r.text(); } catch(e) {}
  if (!r.ok) {
    // Try to surface any message from the body
    let detail = bodyText;
    try { const j = JSON.parse(bodyText); detail = j.error || j.errors || bodyText; } catch(e){}
    throw new Error(`${r.status} ${r.statusText}${detail ? ` – ${detail}` : ''}`);
  }
  try { return bodyText ? JSON.parse(bodyText) : {}; } catch (e) {
    throw new Error(`Bad JSON: ${e.message}`);
  }
}

// Read a single variable; gracefully return null on failure
async function readVar(name){
  try{
    const j = await fetchJSON(`${API}/devices/${encodeURIComponent(DEVICE)}/${encodeURIComponent(name)}`);
    if (!('result' in j)) throw new Error(`No result for ${name}`);
    return j.result;
  }catch(e){
    console.warn(`Var ${name} failed:`, e.message);
    return null;
  }
}

// Call a Particle function; token via Authorization header
async function callFn(name, arg=''){
  const body = new URLSearchParams({ arg });
  return fetchJSON(`${API}/devices/${encodeURIComponent(DEVICE)}/${encodeURIComponent(name)}`, {
    method:'POST',
    headers:{ 'Content-Type':'application/x-www-form-urlencoded' },
    body
  }).then(j => j.return_value);
}

// ===== UI actions =====
async function refreshAll(){
  $('#devName').textContent = DEVICE;
  msg('Refreshing …');

  // Read each variable individually so one failure doesn’t kill the whole batch
  const battPct      = await readVar('battPct');
  const battV        = await readVar('battV');
  const battStateStr = await readVar('battStateStr');
  const onBatterySec = await readVar('onBatterySec');
  const sigPct       = await readVar('sigPct');
  const carrier      = await readVar('carrier');
  const logCount     = await readVar('logCount');

  // Update UI (leave “—” if null)
  if (battPct!=null)      $('#battPct').textContent      = Number.isFinite(battPct)? battPct.toFixed(1): battPct;
  if (battV!=null)        $('#battV').textContent        = Number.isFinite(battV)?   battV.toFixed(3): battV;
  if (battStateStr!=null) $('#battStateStr').textContent = battStateStr;
  if (onBatterySec!=null) $('#onBattery').textContent    = fmtSecs(Number(onBatterySec));
  if (sigPct!=null)       $('#sigPct').textContent       = sigPct;
  if (carrier!=null)      $('#carrier').textContent      = carrier;

  if (logCount!=null){
    $('#logCount').textContent = logCount;
    const mins = Number(logCount) * LOG_MINUTES;
    const hours = (mins/60).toFixed(1);
    $('#histWindow').textContent = `≈ ${mins} min (${hours} h) on-battery history`;
  }

  // If anything critical failed, surface a small notice
  const bad = [battPct,battV,battStateStr,onBatterySec,sigPct,carrier].some(v=>v==null);
  msg(bad ? 'Some fields failed (see console).' : 'OK', bad?'err':'ok');
}

async function led(on){
  try{
    msg(`LED ${on?'ON':'OFF'} …`);
    const rv = await callFn('led', on?'on':'off');
    if (rv===1) msg(`LED ${on?'ON':'OFF'} OK`);
    else msg(`LED returned ${rv}`, 'err');
  }catch(e){ console.error(e); msg(`LED error: ${e.message}`, 'err'); }
}

async function clearLog(){
  if (!confirm('Clear EEPROM ring log?')) return;
  try{
    msg('Clearing …');
    const rv = await callFn('clrlog','');
    await refreshAll();
    msg(`Cleared. Capacity slots ≈ ${rv}.`);
  }catch(e){ console.error(e); msg(`Clear failed: ${e.message}`, 'err'); }
}

// Download CSV via event stream (uses query token because EventSource can’t set headers)
async function downloadCsv(){
  const btn = $('#dlBtn'); btn.disabled = true;
  try{
    const expected = await readVar('logCount');
    if (!expected) { msg('No entries to export.', 'err'); btn.disabled=false; return; }

    const rows = ['ts,battV,battPct,battState,sigPct'];
    let got=0, quietTimer=null;
    const QUIET_MS=8000;

    const stop = (why='done')=>{
      try{ es.close(); }catch(e){}
      clearTimeout(quietTimer);
      const blob = new Blob([rows.join('\n')], {type:'text/csv'});
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `boron_log_${new Date().toISOString().replace(/[:.]/g,'-')}.csv`;
      a.click(); URL.revokeObjectURL(a.href);
      msg(`CSV ${why}: ${got}/${expected} rows saved.`);
      btn.disabled=false;
    };

    const armQuiet = ()=>{ clearTimeout(quietTimer); quietTimer=setTimeout(()=>stop('timeout'), QUIET_MS); };

    // EventSource cannot set headers, so we pass token in query (works for events).
    const url = `${API}/devices/${encodeURIComponent(DEVICE)}/events/log?access_token=${TOKEN}`;
    const es = new EventSource(url);

    es.onmessage = (e)=>{
      try{
        const payload = JSON.parse(e.data);
        const line = payload && payload.data;
        if (typeof line === 'string' && /^\d+,\d+(\.\d+)?,/.test(line)){
          rows.push(line); got++; msg(`Receiving ${got}/${expected} …`);
          if (got>=expected) stop('complete'); else armQuiet();
        }
      }catch(err){ console.warn('bad event payload', err); }
    };
    es.onerror = ev => { console.warn('SSE error', ev); if (got>0) stop('partial'); else { msg('Stream error', 'err'); btn.disabled=false; } };

    armQuiet();
    await callFn('exportlog','');
    msg('Export started … collecting rows …');
  }catch(e){ console.error(e); msg(`Download failed: ${e.message}`, 'err'); btn.disabled=false; }
}

// ===== Wire up =====
window.addEventListener('DOMContentLoaded', ()=>{
  $('#devName').textContent = DEVICE;
  $('#refreshBtn').addEventListener('click', refreshAll);
  $('#ledOnBtn').addEventListener('click', ()=>led(true));
  $('#ledOffBtn').addEventListener('click', ()=>led(false));
  $('#clrLogBtn').addEventListener('click', clearLog);
  $('#dlBtn').addEventListener('click', downloadCsv);

  // No auto-poll to save data
  refreshAll();
});
