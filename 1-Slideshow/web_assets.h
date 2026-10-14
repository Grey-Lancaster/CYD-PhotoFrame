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
[hidden]{display:none!important}
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
.lang{display:flex;gap:4px;align-items:center}
.lang button{padding:3px 10px;font-size:12px}
.lang button.on{background:var(--acc)}
</style>
</head>
<body>
<header><h1>📷 PhotoFrame <small id="ver"></small></h1><div style="display:flex;gap:12px;align-items:center;flex-wrap:wrap"><div id="stat" style="color:var(--mut)"></div><div class="lang"><button id="lEN" class="alt" type="button">EN</button><button id="lES" class="alt" type="button">ES</button></div></div></header>
<main>

<section class="card">
  <div class="screen"><img id="cur" alt=""></div>
  <div id="curname"></div>
  <div class="row">
    <button data-c="prev" class="alt" data-i="prev">◀ Prev</button>
    <button id="pp" class="alt">⏸ Pause</button>
    <button data-c="next" class="alt" data-i="next">Next ▶</button>
    <button data-c="info" class="alt" data-i="info">ℹ Show IP / QR</button>
  </div>
</section>

<section class="card">
  <h2><span data-i="photos">Photos</span> <small id="cnt" style="color:var(--mut)"></small></h2>
  <div id="drop">
    <span data-ih="drop">Drop photos here (or a <b>music.wav / .mp3</b> sound file), or</span>
    <label class="btn" style="margin:0 4px"><span data-i="choose">Choose files</span><input type="file" id="files" multiple accept="image/*,audio/*,.wav,.mp3" hidden></label>
    <div>
      <label class="opt"><input type="checkbox" id="resize" checked> <span data-i="rs1">Resize for the</span> <span id="scrsz">320×240</span> <span data-i="rs2">screen</span></label>
      <label class="opt"><input type="checkbox" id="fill"> <span data-i="crop">Crop to fill (otherwise letterbox)</span></label>
    </div>
  </div>
  <ul id="uplist"></ul>
  <div id="grid"></div>
  <div class="row">
    <button id="selall" class="alt" data-i="selall">Select all</button>
    <button id="delsel" class="bad" disabled>Delete selected</button>
  </div>
</section>

<section class="card">
  <h2 data-i="settings">Settings</h2>
  <form id="sf" class="grid">
    <label><span data-i="speed">Seconds per photo</span><input type="number" id="speed" min="2" max="3600"></label>
    <label><span data-i="bright">Screen brightness</span> <span id="bv"></span><input type="range" id="bright" min="5" max="100"></label>
    <label><span data-i="volume">Sound volume</span> <span id="vv"></span><input type="range" id="volume" min="0" max="100"></label>
    <label class="chk"><input type="checkbox" id="shuffle"> <span data-i="shuffle">Shuffle order</span></label>
    <label class="chk"><input type="checkbox" id="invert"> <span data-i="invert">Invert colours (use if photos look like a negative)</span></label>
    <label class="chk"><input type="checkbox" id="swaprb"> <span data-i="swaprb">Swap red and blue (use if photo colours look wrong)</span></label>
    <label id="rotl" hidden><span data-i="rot">Screen rotation</span><select id="rot">
      <option value="0" data-i="rot0">0&deg; (landscape)</option><option value="1">90&deg;</option><option value="2">180&deg;</option><option value="3">270&deg;</option>
    </select><small data-i="rothint">Turns the picture, for example to put the USB port on the other side. The frame restarts when this changes.</small></label>
    <label class="chk"><input type="checkbox" id="sndup"> <span data-i="sndup">Play the sound when a file is uploaded</span></label>
    <label class="chk"><input type="checkbox" id="night"> <span data-i="night">Turn screen off at night</span></label>
    <label><span data-i="offfrom">Off from</span><select id="nstart"></select></label>
    <label><span data-i="backon">Back on at</span><select id="nend"></select></label>
    <label><span data-i="tz">Time zone</span><select id="tz">
      <option value="EST5EDT,M3.2.0,M11.1.0" data-i="tz0">US Eastern</option>
      <option value="CST6CDT,M3.2.0,M11.1.0" data-i="tz1">US Central</option>
      <option value="MST7MDT,M3.2.0,M11.1.0" data-i="tz2">US Mountain</option>
      <option value="MST7" data-i="tz3">US Arizona</option>
      <option value="PST8PDT,M3.2.0,M11.1.0" data-i="tz4">US Pacific</option>
      <option value="AKST9AKDT,M3.2.0,M11.1.0" data-i="tz5">US Alaska</option>
      <option value="HST10" data-i="tz6">US Hawaii</option>
      <option value="GMT0BST,M3.5.0/1,M10.5.0" data-i="tz7">UK / Ireland</option>
      <option value="CET-1CEST,M3.5.0,M10.5.0" data-i="tz8">Central Europe</option>
      <option value="EET-2EEST,M3.5.0/3,M10.5.0/4" data-i="tz9">Eastern Europe</option>
      <option value="IST-5:30" data-i="tz10">India</option>
      <option value="CST-8" data-i="tz11">China</option>
      <option value="JST-9" data-i="tz12">Japan</option>
      <option value="AEST-10AEDT,M10.1.0,M4.1.0/3" data-i="tz13">Australia East</option>
      <option value="UTC0" data-i="tz14">UTC</option>
    </select></label>
  </form>
  <div class="row"><button id="save" data-i="save">Save settings</button></div>
</section>

<section class="card">
  <h2 data-i="sound">Sound</h2>
  <p id="audioinfo" style="color:var(--mut);margin:0"></p>
  <div class="row"><button id="play" class="alt" data-i="play">▶ Play sound</button><button id="delaudio" class="alt" data-i="delaudio">Remove sound file</button></div>
</section>

<section class="card">
  <h2 data-i="system">System</h2>
  <dl id="sys"></dl>
  <div class="row"><a class="btn alt" href="/update" data-i="fw">Firmware update</a><button id="recal" class="alt" hidden data-i="recal">Recalibrate touch</button></div>
  <details><summary data-i="about">About</summary>
    <p>CYD PhotoFrame by Grey Lancaster, built with help from ChatGPT and Claude, and the open-source community:
    TouchWifiProvisioner, LVGL, ESPAsyncWebServer, TFT_eSPI, XPT2046_Bitbang, SdFat, JPEGDEC, QRCode, ESP8266Audio and ElegantOTA.</p>
  </details>
</section>
</main>
<div id="msg"></div>

<script>
const $=s=>document.querySelector(s);
const L={en:{prev:'◀ Prev',next:'Next ▶',info:'ℹ Show IP / QR',pause:'⏸ Pause',resume:'▶ Resume',photos:'Photos',drop:'Drop photos here (or a <b>music.wav / .mp3</b> sound file), or',choose:'Choose files',rs1:'Resize for the',rs2:'screen',crop:'Crop to fill (otherwise letterbox)',selall:'Select all',delsel:'Delete selected',settings:'Settings',speed:'Seconds per photo',bright:'Screen brightness',volume:'Sound volume',shuffle:'Shuffle order',invert:'Invert colours (use if photos look like a negative)',swaprb:'Swap red and blue (use if photo colours look wrong)',rot:'Screen rotation',rot0:'0° (landscape)',rothint:'Turns the picture, for example to put the USB port on the other side. The frame restarts when this changes.',sndup:'Play the sound when a file is uploaded',night:'Turn screen off at night',offfrom:'Off from',backon:'Back on at',tz:'Time zone',save:'Save settings',sound:'Sound',play:'▶ Play sound',delaudio:'Remove sound file',system:'System',fw:'Firmware update',recal:'Recalibrate touch',about:'About',offline:'offline',scroff:'screen off · ',paused:'paused · ',of:' of ',nophotos:'no photos',sndfile:'Sound file: ',playing:' (playing)',nosnd:'No sound file on the SD card. Upload a WAV or MP3.',addr:'Address',wifi:'WiFi signal',mem:'Free memory',up:'Uptime',lastr:'Last restart',sd:'SD card',clock:'Clock',ok:'OK',notfound:'not found',unknown:'unknown',noclock:'not set (night mode needs internet)',midnight:' (midnight)',noon:' (noon)',show:'Click to show on the frame',saved:'Saved',deleted:'Deleted',delq:'Delete {n} photo(s) from the SD card?',recalq:'Restart the frame and calibrate the touch screen again?',recalmsg:'Restarting - touch the corners shown on the frame',delaq:'Remove the sound file?',busyup:'Upload already running',prep:'preparing…',done:'done',tz0:'US Eastern',tz1:'US Central',tz2:'US Mountain',tz3:'US Arizona',tz4:'US Pacific',tz5:'US Alaska',tz6:'US Hawaii',tz7:'UK / Ireland',tz8:'Central Europe',tz9:'Eastern Europe',tz10:'India',tz11:'China',tz12:'Japan',tz13:'Australia East',tz14:'UTC'},es:{prev:'◀ Anterior',next:'Siguiente ▶',info:'ℹ Mostrar IP / QR',pause:'⏸ Pausa',resume:'▶ Reanudar',photos:'Fotos',drop:'Arrastra aquí las fotos (o un archivo de sonido <b>music.wav / .mp3</b>), o',choose:'Elegir archivos',rs1:'Ajustar a la pantalla de',rs2:'',crop:'Recortar para llenar la pantalla (si no, quedan bandas negras)',selall:'Seleccionar todo',delsel:'Eliminar seleccionadas',settings:'Ajustes',speed:'Segundos por foto',bright:'Brillo de la pantalla',volume:'Volumen del sonido',shuffle:'Orden aleatorio',invert:'Invertir colores (si las fotos parecen un negativo)',swaprb:'Intercambiar rojo y azul (si los colores de las fotos se ven mal)',rot:'Rotación de la pantalla',rot0:'0° (horizontal)',rothint:'Gira la imagen, por ejemplo para poner el puerto USB en el otro lado. El marco se reinicia al cambiar esto.',sndup:'Reproducir el sonido al subir un archivo',night:'Apagar la pantalla por la noche',offfrom:'Apagar desde',backon:'Encender a las',tz:'Zona horaria',save:'Guardar ajustes',sound:'Sonido',play:'▶ Reproducir sonido',delaudio:'Quitar el archivo de sonido',system:'Sistema',fw:'Actualizar el firmware',recal:'Recalibrar la pantalla táctil',about:'Acerca de',offline:'sin conexión',scroff:'pantalla apagada · ',paused:'en pausa · ',of:' de ',nophotos:'sin fotos',sndfile:'Archivo de sonido: ',playing:' (reproduciendo)',nosnd:'No hay archivo de sonido en la tarjeta SD. Sube un WAV o un MP3.',addr:'Dirección',wifi:'Señal WiFi',mem:'Memoria libre',up:'Tiempo encendido',lastr:'Último reinicio',sd:'Tarjeta SD',clock:'Reloj',ok:'OK',notfound:'no encontrada',unknown:'desconocido',noclock:'sin ajustar (el modo nocturno necesita internet)',midnight:' (medianoche)',noon:' (mediodía)',show:'Clic para mostrarla en el marco',saved:'Guardado',deleted:'Eliminado',delq:'¿Eliminar {n} foto(s) de la tarjeta SD?',recalq:'¿Reiniciar el marco y calibrar de nuevo la pantalla táctil?',recalmsg:'Reiniciando: toca las esquinas que se muestran en el marco',delaq:'¿Quitar el archivo de sonido?',busyup:'Ya hay una subida en curso',prep:'preparando…',done:'listo',tz0:'EE. UU. Este',tz1:'EE. UU. Centro',tz2:'EE. UU. Montaña',tz3:'EE. UU. Arizona',tz4:'EE. UU. Pacífico',tz5:'EE. UU. Alaska',tz6:'EE. UU. Hawái',tz7:'Reino Unido / Irlanda',tz8:'Europa central (España)',tz9:'Europa oriental',tz10:'India',tz11:'China',tz12:'Japón',tz13:'Australia este',tz14:'UTC'}};
let lang=(navigator.language||'').toLowerCase().startsWith('es')?'es':'en',langBusy=false;
const t=k=>(L[lang]&&L[lang][k]!==undefined)?L[lang][k]:(L.en[k]!==undefined?L.en[k]:k);
const tr=t;   // alias for code that has a local variable called t
let st={},imgs=[],sel=new Set(),lastCur=null,dirty=false,busyUp=false;
const toast=(t)=>{const m=$('#msg');m.textContent=t;m.style.display='block';clearTimeout(toast.t);toast.t=setTimeout(()=>m.style.display='none',2500)};
async function j(u,o){const r=await fetch(u,o);const t=await r.text();let d;try{d=JSON.parse(t)}catch(e){}if(!r.ok)throw new Error((d&&d.error)||t||r.status);return d}
const post=(u,d)=>j(u,{method:'POST',body:d instanceof URLSearchParams?d:new URLSearchParams(d||{})});
const cmd=(c,n)=>post('/api/cmd',n?{c,n}:{c}).then(loadStatus).catch(e=>toast(e.message));

function fillNight(){
  const a=$('#nstart').value,b=$('#nend').value;$('#nstart').innerHTML='';$('#nend').innerHTML='';
  for(let h=0;h<24;h++){const o=(lang==='es'?(h<10?'0':'')+h+':00':(h%12||12)+(h<12?' AM':' PM'))+(h==0?t('midnight'):h==12?t('noon'):'');$('#nstart').add(new Option(o,h));$('#nend').add(new Option(o,h))}
  if(a!=='')$('#nstart').value=a;if(b!=='')$('#nend').value=b;
}
function applyLang(){
  document.documentElement.lang=lang;
  document.querySelectorAll('[data-i]').forEach(e=>e.textContent=t(e.dataset.i));
  document.querySelectorAll('[data-ih]').forEach(e=>e.innerHTML=t(e.dataset.ih));
  $('#lEN').classList.toggle('on',lang==='en');$('#lES').classList.toggle('on',lang==='es');
  fillNight();renderStatus();upd();
}
async function setLang(l){
  lang=l;applyLang();langBusy=true;
  try{await post('/api/settings',{lang:l})}catch(e){}
  langBusy=false;
}
$('#lEN').onclick=()=>setLang('en');$('#lES').onclick=()=>setLang('es');

function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'d ':'')+h+'h '+m+'m'}

function fillRot(s){
  if(s.sw)$('#scrsz').textContent=s.sw+'×'+s.sh;
  const n=s.rotn||4;[...$('#rot').options].forEach(o=>{o.hidden=o.disabled=(n==2&&(o.value&1)==1)});
  if(s.rot!==undefined){$('#rotl').hidden=false;$('#rot').value=s.rot}
}

function fillSettings(s){
  $('#speed').value=s.speed;$('#bright').value=s.bright;$('#volume').value=s.volume;
  $('#shuffle').checked=s.shuffle;$('#swaprb').checked=s.swaprb;fillRot(s);$('#invert').checked=s.invert;$('#sndup').checked=s.sndup;$('#night').checked=s.night;
  $('#nstart').value=s.nstart;$('#nend').value=s.nend;
  const tz=$('#tz');if(![...tz.options].some(o=>o.value===s.tz))tz.add(new Option(s.tz,s.tz));tz.value=s.tz;
  $('#bv').textContent=s.bright+'%';$('#vv').textContent=s.volume+'%';
}

function renderStatus(){
  if(st.v===undefined)return;
  $('#ver').textContent='v'+st.v;
  $('#stat').textContent=st.count?((st.night?t('scroff'):st.paused?t('paused'):'')+st.pos+t('of')+st.count):t('nophotos');
  $('#curname').textContent=st.cur||'';
  $('#pp').textContent=st.paused?t('resume'):t('pause');
  $('#audioinfo').textContent=st.audio?(t('sndfile')+st.audio+(st.audioBusy?t('playing'):'')):t('nosnd');
  $('#sys').innerHTML='';
  const rows=[[t('addr'),st.ip+' / '+st.host+'.local'],[t('wifi'),st.rssi+' dBm'],[t('mem'),Math.round(st.heap/1024)+' KB'],[t('up'),fmtUp(st.up)],[t('lastr'),st.reset||t('unknown')],[t('sd'),st.sd?t('ok'):t('notfound')],[t('clock'),st.time||t('noclock')]];
  for(const[k,v]of rows){const a=document.createElement('dt'),b=document.createElement('dd');a.textContent=k;b.textContent=v;$('#sys').append(a,b)}
}

async function loadStatus(){
  try{st=await j('/api/status')}catch(e){$('#stat').textContent=t('offline');return}
  if(st.lang&&st.lang!==lang&&!langBusy){lang=st.lang;applyLang()}   // the frame's own language setting (WiFi picker) wins
  if(st.cur!==lastCur){lastCur=st.cur;$('#cur').src=st.cur?'/current_image?t='+Date.now():''}
  document.querySelectorAll('.th').forEach(t=>t.classList.toggle('cur',t.dataset.n===st.cur));
  if(!dirty)fillSettings(st.s);
  $('#recal').hidden=!st.touchcal;
  $('#play').disabled=!st.audio||st.audioBusy;$('#delaudio').disabled=!st.audio;
  renderStatus();
}

async function loadImages(){
  try{imgs=await j('/api/images')}catch(e){return}
  const g=$('#grid');g.innerHTML='';
  sel=new Set([...sel].filter(n=>imgs.some(i=>i.n===n)));
  for(const i of imgs){
    const t=document.createElement('div');t.className='th';t.dataset.n=i.n;t.title=tr('show');
    const cb=document.createElement('input');cb.type='checkbox';cb.checked=sel.has(i.n);
    cb.onclick=e=>{e.stopPropagation();cb.checked?sel.add(i.n):sel.delete(i.n);upd()};
    const im=document.createElement('img');im.loading='lazy';im.decoding='async';im.src='/img?n='+encodeURIComponent(i.n)+'&v='+i.s;
    const sp=document.createElement('span');sp.textContent=i.n;
    t.append(cb,im,sp);t.onclick=()=>cmd('show',i.n);g.append(t);
  }
  $('#cnt').textContent='('+imgs.length+')';upd();
  document.querySelectorAll('.th').forEach(t=>t.classList.toggle('cur',t.dataset.n===st.cur));
}
function upd(){$('#delsel').disabled=!sel.size;$('#delsel').textContent=sel.size?t('delsel')+' ('+sel.size+')':t('delsel')}

document.querySelectorAll('[data-c]').forEach(b=>b.onclick=()=>cmd(b.dataset.c));
$('#pp').onclick=()=>cmd(st.paused?'resume':'pause');
$('#selall').onclick=()=>{const all=sel.size!==imgs.length;sel=new Set(all?imgs.map(i=>i.n):[]);document.querySelectorAll('.th input').forEach(c=>c.checked=all);upd()};
$('#delsel').onclick=async()=>{
  if(!confirm(t('delq').replace('{n}',sel.size)))return;
  const p=new URLSearchParams();sel.forEach(n=>p.append('n',n));
  try{await post('/api/delete',p);sel.clear();toast(t('deleted'))}catch(e){toast(e.message)}
  loadImages();loadStatus();
};
$('#play').onclick=()=>post('/api/play').then(loadStatus).catch(e=>toast(e.message));
$('#recal').onclick=async()=>{if(confirm(t('recalq'))){try{await post('/api/recal');toast(t('recalmsg'))}catch(e){toast(e.message)}}};
$('#delaudio').onclick=async()=>{if(confirm(t('delaq'))){try{await post('/api/delete',{n:st.audio})}catch(e){toast(e.message)}loadStatus()}};

document.querySelectorAll('#sf input,#sf select').forEach(e=>e.oninput=()=>{dirty=true;$('#bv').textContent=$('#bright').value+'%';$('#vv').textContent=$('#volume').value+'%'});
$('#save').onclick=async()=>{
  try{
    await post('/api/settings',{speed:$('#speed').value,bright:$('#bright').value,volume:$('#volume').value,
      shuffle:$('#shuffle').checked?1:0,swaprb:$('#swaprb').checked?1:0,rot:$('#rot').value,invert:$('#invert').checked?1:0,sndup:$('#sndup').checked?1:0,night:$('#night').checked?1:0,
      nstart:$('#nstart').value,nend:$('#nend').value,tz:$('#tz').value});
    dirty=false;toast(t('saved'));loadStatus();
  }catch(e){toast(e.message)}
};

// ------------------------------------------------------------ uploads
async function prep(f){
  if(!$('#resize').checked||!f.type.startsWith('image/'))return {blob:f,name:f.name};
  const bm=await createImageBitmap(f);
  const W=st.sw||320,H=st.sh||240,fill=$('#fill').checked;
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
  if(busyUp)return toast(t('busyup'));busyUp=true;
  for(const f of files){
    const li=document.createElement('li');const a=document.createElement('span'),b=document.createElement('span');
    a.textContent=f.name;b.textContent=t('prep');li.append(a,b);$('#uplist').prepend(li);
    try{
      const p=await prep(f);
      await sendFile(p.blob,p.name,pc=>b.textContent=pc+'%');
      b.textContent=t('done');b.className='ok';
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
applyLang();
loadStatus();loadImages();
setInterval(()=>{if(!document.hidden&&!busyUp)loadStatus()},5000);
</script>
</body>
</html>
)rawliteral";
