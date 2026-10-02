// Single-page web UI, served from flash.
#pragma once

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PhotoFrame</title>
<link rel="icon" href="/favicon.ico">
<style>
:root{--bg:#14161a;--card:#1e2128;--fg:#e8eaed;--mut:#9aa0a6;--acc:#4caf50;--acc2:#3d8b40;--bad:#e5534b;--line:#2d313a}
*{box-sizing:border-box}
body{margin:0;font:15px/1.45 system-ui,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:14px 16px;background:#0e1013;border-bottom:1px solid var(--line);display:flex;justify-content:space-between;align-items:baseline;flex-wrap:wrap;gap:6px}
h1{margin:0;font-size:20px}h1 small{color:var(--mut);font-weight:400;font-size:13px}
h2{margin:0 0 10px;font-size:16px}
main{max-width:900px;margin:0 auto;padding:12px;display:grid;gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
.screen{background:#000;border-radius:8px;text-align:center;aspect-ratio:4/3;max-width:420px;margin:0 auto;overflow:hidden}
.screen img{width:100%;height:100%;object-fit:contain}
#curname{text-align:center;color:var(--mut);margin:8px 0;word-break:break-all;min-height:1.4em}
.row{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;margin-top:8px}
button,.btn{background:var(--acc);color:#fff;border:0;border-radius:8px;padding:9px 16px;font:inherit;cursor:pointer;text-decoration:none;display:inline-block}
button:hover,.btn:hover{background:var(--acc2)}
button.alt,.btn.alt{background:#363b46}button.alt:hover{background:#444a57}
button.bad{background:var(--bad)}
button:disabled{opacity:.4;cursor:default}
#drop{border:2px dashed #3b414d;border-radius:10px;padding:16px;text-align:center;color:var(--mut)}
#drop.over{border-color:var(--acc);background:#1b2a1d}
#drop label.opt{display:inline-block;margin:6px 10px 0}
#uplist{list-style:none;margin:8px 0 0;padding:0;font-size:13px}
#uplist li{display:flex;gap:8px;justify-content:space-between;padding:3px 0;border-bottom:1px solid var(--line)}
#uplist .err{color:var(--bad)}#uplist .ok{color:var(--acc)}
#grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:8px;margin-top:12px}
.th{position:relative;background:#000;border-radius:8px;overflow:hidden;border:2px solid transparent;cursor:pointer}
.th.cur{border-color:var(--acc)}
.th img{display:block;width:100%;aspect-ratio:4/3;object-fit:contain}
.th input{position:absolute;top:6px;left:6px;width:18px;height:18px}
.th span{display:block;font-size:11px;padding:2px 4px;color:var(--mut);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;background:#111}
form.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:12px}
form.grid label{display:block;color:var(--mut);font-size:13px}
form.grid input[type=number],form.grid select{width:100%;padding:7px;border-radius:6px;border:1px solid #3b414d;background:#14161a;color:var(--fg);font:inherit}
form.grid input[type=range]{width:100%}
.chk{display:flex!important;gap:8px;align-items:center;color:var(--fg)!important;font-size:15px!important}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;margin:0}dt{color:var(--mut)}dd{margin:0;text-align:right}
#msg{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);background:#000c;padding:8px 16px;border-radius:20px;display:none;max-width:90%}
details{margin-top:12px;color:var(--mut);font-size:13px}
</style>
</head>
<body>
<header><h1>📷 PhotoFrame <small id="ver"></small></h1><div id="stat" style="color:var(--mut)"></div></header>
<main>

<section class="card">
  <div class="screen"><img id="cur" alt=""></div>
  <div id="curname"></div>
  <div class="row">
    <button data-c="prev" class="alt">◀ Prev</button>
    <button id="pp" class="alt">⏸ Pause</button>
    <button data-c="next" class="alt">Next ▶</button>
    <button data-c="info" class="alt">ℹ Show IP / QR</button>
  </div>
</section>

<section class="card">
  <h2>Photos <small id="cnt" style="color:var(--mut)"></small></h2>
  <div id="drop">
    Drop photos here (or a <b>music.wav / .mp3</b> sound file), or
    <label class="btn" style="margin:0 4px">Choose files<input type="file" id="files" multiple accept="image/*,audio/*,.wav,.mp3" hidden></label>
    <div>
      <label class="opt"><input type="checkbox" id="resize" checked> Resize for the 320×240 screen</label>
      <label class="opt"><input type="checkbox" id="fill"> Crop to fill (otherwise letterbox)</label>
    </div>
  </div>
  <ul id="uplist"></ul>
  <div id="grid"></div>
  <div class="row">
    <button id="selall" class="alt">Select all</button>
    <button id="delsel" class="bad" disabled>Delete selected</button>
  </div>
</section>

<section class="card">
  <h2>Settings</h2>
  <form id="sf" class="grid">
    <label>Seconds per photo<input type="number" id="speed" min="2" max="3600"></label>
    <label>Screen brightness <span id="bv"></span><input type="range" id="bright" min="5" max="100"></label>
    <label>Sound volume <span id="vv"></span><input type="range" id="volume" min="0" max="100"></label>
    <label class="chk"><input type="checkbox" id="shuffle"> Shuffle order</label>
    <label class="chk"><input type="checkbox" id="sndup"> Play the sound when a file is uploaded</label>
    <label class="chk"><input type="checkbox" id="night"> Turn screen off at night</label>
    <label>Off from<select id="nstart"></select></label>
    <label>Back on at<select id="nend"></select></label>
    <label>Time zone<select id="tz">
      <option value="EST5EDT,M3.2.0,M11.1.0">US Eastern</option>
      <option value="CST6CDT,M3.2.0,M11.1.0">US Central</option>
      <option value="MST7MDT,M3.2.0,M11.1.0">US Mountain</option>
      <option value="MST7">US Arizona</option>
      <option value="PST8PDT,M3.2.0,M11.1.0">US Pacific</option>
      <option value="AKST9AKDT,M3.2.0,M11.1.0">US Alaska</option>
      <option value="HST10">US Hawaii</option>
      <option value="GMT0BST,M3.5.0/1,M10.5.0">UK / Ireland</option>
      <option value="CET-1CEST,M3.5.0,M10.5.0">Central Europe</option>
      <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Eastern Europe</option>
      <option value="IST-5:30">India</option>
      <option value="CST-8">China</option>
      <option value="JST-9">Japan</option>
      <option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Australia East</option>
      <option value="UTC0">UTC</option>
    </select></label>
  </form>
  <div class="row"><button id="save">Save settings</button></div>
</section>

<section class="card">
  <h2>Sound</h2>
  <p id="audioinfo" style="color:var(--mut);margin:0"></p>
  <div class="row"><button id="play" class="alt">▶ Play sound</button><button id="delaudio" class="alt">Remove sound file</button></div>
</section>

<section class="card">
  <h2>System</h2>
  <dl id="sys"></dl>
  <div class="row"><a class="btn alt" href="/update">Firmware update</a></div>
  <details><summary>About</summary>
    <p>CYD PhotoFrame by Grey Lancaster, built with help from ChatGPT and Claude, and the open-source community:
    WiFiManager, ESPAsyncWebServer, TFT_eSPI, XPT2046_Bitbang, SdFat, JPEGDEC, QRCode, ESP8266Audio and ElegantOTA.</p>
  </details>
</section>
</main>
<div id="msg"></div>

<script>
const $=s=>document.querySelector(s);
let st={},imgs=[],sel=new Set(),lastCur=null,dirty=false,busyUp=false;
const toast=(t)=>{const m=$('#msg');m.textContent=t;m.style.display='block';clearTimeout(toast.t);toast.t=setTimeout(()=>m.style.display='none',2500)};
async function j(u,o){const r=await fetch(u,o);const t=await r.text();let d;try{d=JSON.parse(t)}catch(e){}if(!r.ok)throw new Error((d&&d.error)||t||r.status);return d}
const post=(u,d)=>j(u,{method:'POST',body:d instanceof URLSearchParams?d:new URLSearchParams(d||{})});
const cmd=(c,n)=>post('/api/cmd',n?{c,n}:{c}).then(loadStatus).catch(e=>toast(e.message));

for(let h=0;h<24;h++){const o=(h%12||12)+(h<12?' AM':' PM')+(h==0?' (midnight)':h==12?' (noon)':'');$('#nstart').add(new Option(o,h));$('#nend').add(new Option(o,h))}

function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'d ':'')+h+'h '+m+'m'}

function fillSettings(s){
  $('#speed').value=s.speed;$('#bright').value=s.bright;$('#volume').value=s.volume;
  $('#shuffle').checked=s.shuffle;$('#sndup').checked=s.sndup;$('#night').checked=s.night;
  $('#nstart').value=s.nstart;$('#nend').value=s.nend;
  const tz=$('#tz');if(![...tz.options].some(o=>o.value===s.tz))tz.add(new Option(s.tz,s.tz));tz.value=s.tz;
  $('#bv').textContent=s.bright+'%';$('#vv').textContent=s.volume+'%';
}

async function loadStatus(){
  try{st=await j('/api/status')}catch(e){$('#stat').textContent='offline';return}
  $('#ver').textContent='v'+st.v;
  $('#stat').textContent=st.count?((st.night?'screen off · ':st.paused?'paused · ':'')+st.pos+' of '+st.count):'no photos';
  $('#curname').textContent=st.cur||'';
  $('#pp').textContent=st.paused?'▶ Resume':'⏸ Pause';
  if(st.cur!==lastCur){lastCur=st.cur;$('#cur').src=st.cur?'/current_image?t='+Date.now():''}
  document.querySelectorAll('.th').forEach(t=>t.classList.toggle('cur',t.dataset.n===st.cur));
  if(!dirty)fillSettings(st.s);
  $('#audioinfo').textContent=st.audio?('Sound file: '+st.audio+(st.audioBusy?' (playing)':'')):'No sound file on the SD card. Upload a WAV or MP3.';
  $('#play').disabled=!st.audio||st.audioBusy;$('#delaudio').disabled=!st.audio;
  $('#sys').innerHTML='';
  const rows=[['Address',st.ip+' / '+st.host+'.local'],['WiFi signal',st.rssi+' dBm'],['Free memory',Math.round(st.heap/1024)+' KB'],['Uptime',fmtUp(st.up)],['SD card',st.sd?'OK':'not found'],['Clock',st.time||'not set (night mode needs internet)']];
  for(const[k,v]of rows){const a=document.createElement('dt'),b=document.createElement('dd');a.textContent=k;b.textContent=v;$('#sys').append(a,b)}
}

async function loadImages(){
  try{imgs=await j('/api/images')}catch(e){return}
  const g=$('#grid');g.innerHTML='';
  sel=new Set([...sel].filter(n=>imgs.some(i=>i.n===n)));
  for(const i of imgs){
    const t=document.createElement('div');t.className='th';t.dataset.n=i.n;t.title='Click to show on the frame';
    const cb=document.createElement('input');cb.type='checkbox';cb.checked=sel.has(i.n);
    cb.onclick=e=>{e.stopPropagation();cb.checked?sel.add(i.n):sel.delete(i.n);upd()};
    const im=document.createElement('img');im.loading='lazy';im.decoding='async';im.src='/img?n='+encodeURIComponent(i.n)+'&v='+i.s;
    const sp=document.createElement('span');sp.textContent=i.n;
    t.append(cb,im,sp);t.onclick=()=>cmd('show',i.n);g.append(t);
  }
  $('#cnt').textContent='('+imgs.length+')';upd();
  document.querySelectorAll('.th').forEach(t=>t.classList.toggle('cur',t.dataset.n===st.cur));
}
function upd(){$('#delsel').disabled=!sel.size;$('#delsel').textContent=sel.size?'Delete selected ('+sel.size+')':'Delete selected'}

document.querySelectorAll('[data-c]').forEach(b=>b.onclick=()=>cmd(b.dataset.c));
$('#pp').onclick=()=>cmd(st.paused?'resume':'pause');
$('#selall').onclick=()=>{const all=sel.size!==imgs.length;sel=new Set(all?imgs.map(i=>i.n):[]);document.querySelectorAll('.th input').forEach(c=>c.checked=all);upd()};
$('#delsel').onclick=async()=>{
  if(!confirm('Delete '+sel.size+' photo(s) from the SD card?'))return;
  const p=new URLSearchParams();sel.forEach(n=>p.append('n',n));
  try{await post('/api/delete',p);sel.clear();toast('Deleted')}catch(e){toast(e.message)}
  loadImages();loadStatus();
};
$('#play').onclick=()=>post('/api/play').then(loadStatus).catch(e=>toast(e.message));
$('#delaudio').onclick=async()=>{if(confirm('Remove the sound file?')){try{await post('/api/delete',{n:st.audio})}catch(e){toast(e.message)}loadStatus()}};

document.querySelectorAll('#sf input,#sf select').forEach(e=>e.oninput=()=>{dirty=true;$('#bv').textContent=$('#bright').value+'%';$('#vv').textContent=$('#volume').value+'%'});
$('#save').onclick=async()=>{
  try{
    await post('/api/settings',{speed:$('#speed').value,bright:$('#bright').value,volume:$('#volume').value,
      shuffle:$('#shuffle').checked?1:0,sndup:$('#sndup').checked?1:0,night:$('#night').checked?1:0,
      nstart:$('#nstart').value,nend:$('#nend').value,tz:$('#tz').value});
    dirty=false;toast('Saved');loadStatus();
  }catch(e){toast(e.message)}
};

// ------------------------------------------------------------ uploads
async function prep(f){
  if(!$('#resize').checked||!f.type.startsWith('image/'))return {blob:f,name:f.name};
  const bm=await createImageBitmap(f);
  const W=320,H=240,fill=$('#fill').checked;
  let w,h,cw,ch;
  if(fill){cw=W;ch=H;const s=Math.max(W/bm.width,H/bm.height);w=bm.width*s;h=bm.height*s}
  else{const s=Math.min(1,W/bm.width,H/bm.height);w=cw=Math.max(1,Math.round(bm.width*s));h=ch=Math.max(1,Math.round(bm.height*s))}
  const c=document.createElement('canvas');c.width=cw;c.height=ch;
  const x=c.getContext('2d');x.fillStyle='#000';x.fillRect(0,0,cw,ch);
  x.imageSmoothingQuality='high';x.drawImage(bm,(cw-w)/2,(ch-h)/2,w,h);
  const blob=await new Promise(r=>c.toBlob(r,'image/jpeg',0.88));
  return {blob,name:f.name.replace(/\.[^.]*$/,'')+'.jpg'};
}
function sendFile(blob,name,onp){return new Promise((res,rej)=>{
  const x=new XMLHttpRequest();x.open('POST','/api/upload');
  x.upload.onprogress=e=>e.lengthComputable&&onp(Math.round(e.loaded*100/e.total));
  x.onload=()=>{let r;try{r=JSON.parse(x.responseText)}catch(e){return rej(new Error('HTTP '+x.status))}r.ok?res(r):rej(new Error(r.error||'failed'))};
  x.onerror=()=>rej(new Error('network error'));
  const fd=new FormData();fd.append('file',blob,name);x.send(fd)})}
async function upload(files){
  if(busyUp)return toast('Upload already running');busyUp=true;
  for(const f of files){
    const li=document.createElement('li');const a=document.createElement('span'),b=document.createElement('span');
    a.textContent=f.name;b.textContent='preparing…';li.append(a,b);$('#uplist').prepend(li);
    try{
      const p=await prep(f);
      await sendFile(p.blob,p.name,pc=>b.textContent=pc+'%');
      b.textContent='done';b.className='ok';
    }catch(e){b.textContent=e.message;b.className='err'}
    await loadImages();
  }
  busyUp=false;loadStatus();
}
$('#files').onchange=e=>{upload([...e.target.files]);e.target.value=''};
const dz=$('#drop');
['dragenter','dragover'].forEach(n=>dz.addEventListener(n,e=>{e.preventDefault();dz.classList.add('over')}));
['dragleave','drop'].forEach(n=>dz.addEventListener(n,e=>{e.preventDefault();dz.classList.remove('over')}));
dz.addEventListener('drop',e=>upload([...e.dataTransfer.files]));

// ------------------------------------------------------------ live updates
let ws;
function connect(){
  ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');
  ws.onmessage=e=>{if(e.data==='update')loadStatus();else if(e.data==='list'){loadImages();loadStatus()}};
  ws.onclose=()=>setTimeout(connect,2000);
}
connect();
loadStatus();loadImages();
setInterval(()=>{if(!document.hidden&&!busyUp)loadStatus()},5000);
</script>
</body>
</html>
)rawliteral";
