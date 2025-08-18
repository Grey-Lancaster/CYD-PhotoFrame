// === Configuration ===
const TOKEN  = 'c5faa926e596e02388dc06e8cfc333dbac4d785f'; // Particle token
const DEVICE = 'Grey_Fox_1';                                 // name or device ID
const API    = 'https://api.particle.io/v1';
const LOG_MINUTES = 10; // firmware logs every 10 min, discharge-only

// ===== Helpers =====
const $ = s => document.querySelector(s);
const msg = (t, cls='ok') => { const el=$('#msg'); el.textContent=t; el.className=cls; };

function fmtSecs(s){
  if(!Number.isFinite(s)||s<=0) return '0s';
  const h=Math.floor(s/3600), m=Math.floor((s%3600)/60), ss=Math.floor(s%60);
  if(h) return `${h}h ${m}m`; if(m) return `${m}m ${ss}s`; return `${ss}s`;
}

async function fetchJSON(url, opt={}){
  const headers = Object.assign({}, opt.headers||{}, { 'Authorization': `Bearer ${TOKEN}` });
  const r = await fetch(url, Object.assign({}, opt, { headers }));
  let text = '';
  try { text = await r.text(); } catch {}
  if (!r.ok) {
    try { const j = JSON.parse(text); throw new Error(`${r.status} – ${j.error||j.errors||r.statusText}`); }
    catch { throw new Error(`${r.status} – ${text || r.statusText}`); }
  }
  return text ? JSON.parse(text) : {};
}

// Single variable read (doesn’t fail whole refresh)
async function readVar(name){
  try{
    const j = await fetchJSON(`${API}/devices/${encodeURIComponent(DEVICE)}/${encodeURIComponent(name)}`);
    return ('result' in j) ? j.result : null;
  }catch(e){ console.warn(`Var ${name} failed:`, e.message); return null; }
}

// Call a function (Authorization header)
async function callFn(name, arg=''){
  const body = new URLSearchParams({ arg });
  const j = await fetchJSON(
    `${API}/devices/${encodeURIComponent(DEVICE)}/${encodeURIComponent(name)}`,
    { method:'POST', headers:{ 'Content-Type':'application/x-www-form-urlencoded' }, body }
  );
  return j.return_value;
}

// ===== UI actions =====
async function refreshAll(){
  $('#devName').textContent = DEVICE;
  msg('Refreshing …');

  const battPct      = await readVar('battPct');
  const battV        = await readVar('battV');
  const battStateStr = await readVar('battStateStr');
  const onBatterySec = await readVar('onBatterySec');
  const sigPct       = await readVar('sigPct');
  const carrier      = await readVar('carrier');
  const logCount     = await readVar('logCount');

  // runtime estimator (firmware adds these)
  const pctPerHour   = await readVar('pctPerHour'); // negative while draining
  const mAhPerHour   = await readVar('mAhPerHour'); // positive
  const hoursLeft    = await readVar('hoursLeft');  // hours

  if (battPct!=null)      $('#battPct').textContent      = Number.isFinite(battPct)? battPct.toFixed(1): battPct;
  if (battV!=null)        $('#battV').textContent        = Number.isFinite(battV)?   battV.toFixed(3): battV;
  if (battStateStr!=null) $('#battStateStr').textContent = battStateStr;
  if (onBatterySec!=null) $('#onBattery').textContent    = fmtSecs(Number(onBatterySec));
  if (sigPct!=null)       $('#sigPct').textContent       = sigPct;
  if (carrier!=null)      $('#carrier').textContent      = carrier;

  if (logCount!=null){
    $('#logCount').textContent = logCount;
    const mins = Number(logCount) * LOG_MINUTES;
    $('#histWindow').textContent = `≈ ${mins} min (${(mins/60).toFixed(1)} h) on-battery history`;
  }

  if (pctPerHour!=null && Number.isFinite(pctPerHour)) {
    const rateTxt = `${(-pctPerHour).toFixed(2)} %/h` + (Number.isFinite(mAhPerHour)? `  (~${mAhPerHour.toFixed(0)} mAh/h)` : '');
    $('#drainRate').textContent = rateTxt;
  }
  if (hoursLeft!=null && Number.isFinite(hoursLeft)) {
    $('#hoursLeft').textContent = `${hoursLeft.toFixed(1)} h`;
  }

  const bad = [battPct,battV,battStateStr,onBatterySec,sigPct,carrier].some(v=>v==null);
  msg(bad ? 'Some fields failed (see console).' : 'OK', bad?'err':'ok');
}

async function led(on){
  try{ msg(`LED ${on?'ON':'OFF'} …`);
       const rv = await callFn('led', on?'on':'off');
       msg(rv===1?`LED ${on?'ON':'OFF'} OK`:`LED returned ${rv}`, rv===1?'ok':'err');
  }catch(e){ console.error(e); msg(`LED error: ${e.message}`, 'err'); }
}

async function clearLog(){
  if (!confirm('Clear EEPROM ring log?')) return;
  try{ msg('Clearing …'); const rv = await callFn('clrlog',''); await refreshAll(); msg(`Cleared. Capacity slots ≈ ${rv}.`); }
  catch(e){ console.error(e); msg(`Clear failed: ${e.message}`, 'err'); }
}

// ---- CSV download using Fetch-streamed SSE with Authorization header ----
async function downloadCsv(){
  const btn = $('#dlBtn'); btn.disabled = true;
  try{
    const expected = await readVar('logCount');
    if (!expected) { msg('No entries to export.', 'err'); btn.disabled=false; return; }

    msg(`Starting export … expecting ${expected} rows`);
    const rows = ['ts,battV,battPct,battState,sigPct'];
    let got = 0;

    // Open SSE with headers via fetch (EventSource can’t set Authorization)
    const resp = await fetch(
      `${API}/devices/${encodeURIComponent(DEVICE)}/events/log`,
      { headers:{ 'Authorization': `Bearer ${TOKEN}` } }
    );
    if (!resp.ok) throw new Error(`${resp.status} – ${resp.statusText}`);

    const reader  = resp.body.getReader();
    const decoder = new TextDecoder();
    let buffer    = '';
    let done      = false;

    // Quiet timeout (resets on each received row)
    let quietTimer;
    const QUIET_MS = 30000;
    const bumpQuiet = ()=>{ clearTimeout(quietTimer); quietTimer = setTimeout(()=>{ try{reader.cancel();}catch{} }, QUIET_MS); };
    bumpQuiet();

    // Trigger the export after the stream is open
    callFn('exportlog','').catch(e=>console.warn('exportlog call failed', e));

    while (!done){
      const {value, done: d} = await reader.read();
      done = d;
      if (value){
        buffer += decoder.decode(value, { stream:true });

        // Parse SSE frames (separated by blank line)
        let idx;
        while ((idx = buffer.indexOf('\n\n')) >= 0){
          const frame = buffer.slice(0, idx); buffer = buffer.slice(idx+2);
          // Extract "data:" line
          const line = frame.split('\n').find(l => l.startsWith('data:'));
          if (!line) continue;
          const json = line.slice(5).trim();
          try{
            const payload = JSON.parse(json);
            const csvLine = payload && payload.data;
            if (typeof csvLine === 'string' && /^\d+,\d+(\.\d+)?,/.test(csvLine)){
              rows.push(csvLine); got++;
              msg(`Receiving ${got}/${expected} …`);
              bumpQuiet();
              if (got >= expected){ try{ reader.cancel(); }catch{}; done = true; break; }
            }
          }catch(e){ /* ignore parse errors */ }
        }
      }
    }
    clearTimeout(quietTimer);

    // Save whatever we got (partial or complete)
    const blob = new Blob([rows.join('\n')], {type:'text/csv'});
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `boron_log_${new Date().toISOString().replace(/[:.]/g,'-')}.csv`;
    a.click(); URL.revokeObjectURL(a.href);

    msg(`CSV saved: ${got}/${expected} rows.`);
  }catch(e){
    console.error(e);
    msg(`Download failed: ${e.message}`, 'err');
  }finally{
    btn.disabled = false;
  }
}

// ===== Wire up =====
window.addEventListener('DOMContentLoaded', ()=>{
  $('#devName').textContent = DEVICE;
  $('#refreshBtn').addEventListener('click', refreshAll);
  $('#ledOnBtn').addEventListener('click', ()=>led(true));
  $('#ledOffBtn').addEventListener('click', ()=>led(false));
  $('#clrLogBtn').addEventListener('click', clearLog);
  $('#dlBtn').addEventListener('click', downloadCsv);
  refreshAll(); // manual refresh model (no auto-poll)
});
