// === Configuration ===
// Hard-code your secure Particle token and device name/ID.
// NOTE: If you publish this publicly (e.g., GitHub Pages), rotate your token as needed.
const TOKEN  = 'c5faa926e596e02388dc06e8cfc333dbac4d785f'; // your token
const DEVICE = 'Grey_Fox_1';                                 // name or deviceID
const API    = 'https://api.particle.io/v1';

// Logging interval & policy (mirrors firmware): 10 minutes, discharge-only
const LOG_MINUTES = 10;

// ========== Helpers ==========
const $ = sel => document.querySelector(sel);
const msg = (t, cls='ok') => { const el = $('#msg'); el.textContent = t; el.className = cls; };

function fmtSecs(s){
  if (!Number.isFinite(s) || s <= 0) return '0s';
  const h = Math.floor(s/3600), m = Math.floor((s%3600)/60), ss = Math.floor(s%60);
  if (h) return `${h}h ${m}m`;
  if (m) return `${m}m ${ss}s`;
  return `${ss}s`;
}

async function getVar(name){
  const url = `${API}/devices/${encodeURIComponent(DEVICE)}/${encodeURIComponent(name)}?access_token=${TOKEN}`;
  const r = await fetch(url);
  if (!r.ok) throw new Error(`GET ${name} ${r.status}`);
  const j = await r.json();
  if (!('result' in j)) throw new Error(`No result for ${name}`);
  return j.result;
}

async function callFn(name, arg=''){
  const r = await fetch(`${API}/devices/${encodeURIComponent(DEVICE)}/${encodeURIComponent(name)}`, {
    method:'POST',
    headers:{ 'Content-Type':'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ access_token: TOKEN, arg })
  });
  if (!r.ok) throw new Error(`call ${name} ${r.status}`);
  const j = await r.json();
  return j.return_value;
}

// ========== UI actions ==========
async function refreshAll(){
  try{
    msg('Refreshing …');
    const [battPct, battV, battStateStr, onBatterySec, sigPct, carrier, logCount] = await Promise.all([
      getVar('battPct'),
      getVar('battV'),
      getVar('battStateStr'),
      getVar('onBatterySec'),
      getVar('sigPct'),
      getVar('carrier'),
      getVar('logCount').catch(()=>null) // older fw may not have it
    ]);

    $('#battPct').textContent = (typeof battPct === 'number') ? battPct.toFixed(1) : battPct;
    $('#battV').textContent   = (typeof battV === 'number')   ? battV.toFixed(3)   : battV;
    $('#battStateStr').textContent = battStateStr;
    $('#onBattery').textContent    = fmtSecs(Number(onBatterySec));
    $('#sigPct').textContent   = sigPct;
    $('#carrier').textContent  = carrier;

    if (logCount != null) {
      $('#logCount').textContent = logCount;
      const mins = Number(logCount) * LOG_MINUTES;
      const hours = (mins/60).toFixed(1);
      $('#histWindow').textContent = `≈ ${mins} min (${hours} h) on-battery history`;
    } else {
      $('#logCount').textContent = '—';
      $('#histWindow').textContent = '—';
    }

    msg('OK');
  } catch(err){
    console.error(err);
    msg(`Refresh failed: ${err.message}`, 'err');
  }
}

async function led(on){
  try{
    msg(`LED ${on?'ON':'OFF'} …`);
    const rv = await callFn('led', on?'on':'off');
    if (rv === 1) msg(`LED ${on?'ON':'OFF'} OK`);
    else msg(`LED returned ${rv}`, 'err');
  } catch(err){
    console.error(err);
    msg(`LED error: ${err.message}`, 'err');
  }
}

async function clearLog(){
  if (!confirm('Clear EEPROM ring log?')) return;
  try{
    msg('Clearing log …');
    const rv = await callFn('clrlog', '');
    await refreshAll();
    msg(`Cleared. Capacity slots ≈ ${rv}.`);
  } catch(err){
    console.error(err);
    msg(`Clear failed: ${err.message}`, 'err');
  }
}

// Download CSV by:
// 1) Getting expected count from logCount
// 2) Opening SSE on /events/log (token via querystring; headers can’t be set on EventSource)
// 3) Calling exportlog to stream rows
// 4) Closing when we collect expected rows or after a quiet timeout
async function downloadCsv(){
  const btn = $('#dlBtn');
  btn.disabled = true;
  try{
    msg('Preparing CSV export …');
    const expected = await getVar('logCount').catch(()=>0);
    if (!expected || expected <= 0){
      msg('No log entries to export.', 'err');
      btn.disabled = false;
      return;
    }

    const rows = ['ts,battV,battPct,battState,sigPct'];
    let got = 0;
    let quietTimer = null;
    const QUIET_MS = 8000; // stop if no events for 8s

    const url = `${API}/devices/${encodeURIComponent(DEVICE)}/events/log?access_token=${TOKEN}`;
    const es  = new EventSource(url);

    const stop = (why='done')=>{
      try{ es.close(); }catch(e){}
      clearTimeout(quietTimer);
      // Save
      const blob = new Blob([rows.join('\n')], {type:'text/csv'});
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `boron_log_${new Date().toISOString().replace(/[:.]/g,'-')}.csv`;
      a.click();
      URL.revokeObjectURL(a.href);
      msg(`CSV ${why}: ${got}/${expected} rows saved.`);
      btn.disabled = false;
    };

    const armQuiet = ()=>{
      clearTimeout(quietTimer);
      quietTimer = setTimeout(()=> stop('timeout'), QUIET_MS);
    };

    es.onmessage = (e)=>{
      try{
        const payload = JSON.parse(e.data); // {data:"...", name:"log", published_at:...}
        const line = payload && payload.data;
        if (typeof line === 'string' && /^\d+,\d+(\.\d+)?,/.test(line)) {
          rows.push(line);
          got++;
          msg(`Receiving ${got}/${expected} …`);
          if (got >= expected) stop('complete');
          else armQuiet();
        }
      }catch(parseErr){
        console.warn('bad event payload', parseErr);
      }
    };
    es.onerror = (ev)=>{
      console.warn('SSE error', ev);
      // If we already got some rows, save partial; otherwise report error.
      if (got>0) stop('partial');
      else { msg('Stream error — no rows received.', 'err'); btn.disabled=false; }
    };

    // Start quiet timer then trigger the device to publish the rows
    armQuiet();
    await callFn('exportlog','');
    msg('Export started … collecting rows …');
  } catch(err){
    console.error(err);
    msg(`Download failed: ${err.message}`, 'err');
    btn.disabled = false;
  }
}

// ========== Wire up ==========
window.addEventListener('DOMContentLoaded', ()=>{
  $('#devName').textContent = DEVICE;

  $('#refreshBtn').addEventListener('click', refreshAll);
  $('#ledOnBtn').addEventListener('click', ()=> led(true));
  $('#ledOffBtn').addEventListener('click', ()=> led(false));
  $('#clrLogBtn').addEventListener('click', clearLog);
  $('#dlBtn').addEventListener('click', downloadCsv);

  // No auto-poll to save data. User hits Refresh as needed.
  refreshAll();
});
