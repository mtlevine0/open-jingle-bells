#pragma once

// The control page, served at /. One file: no external requests, so it
// works on the setup AP with no internet.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Open Jingle Bells</title>
<style>
:root{--bg:#f6f4ef;--card:#fff;--ink:#1d1b18;--muted:#6f6a61;--line:#e4e0d6;--accent:#b3261e;--accent-ink:#fff;--ok:#2e7d32;--warn:#b26a00}
@media (prefers-color-scheme:dark){:root{--bg:#161513;--card:#22201d;--ink:#eeeae2;--muted:#a49e93;--line:#37342f;--accent:#e2574c;--accent-ink:#fff;--ok:#7cc47f;--warn:#e0a24a}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:14px 16px 6px;max-width:640px;margin:auto}
h1{font-size:20px;margin:0}
.pill{font-size:13px;padding:3px 10px;border-radius:99px;background:var(--line);color:var(--muted);white-space:nowrap}
.pill.ok{color:var(--ok)}.pill.warn{color:var(--warn)}
nav{display:flex;gap:4px;padding:6px 16px;max-width:640px;margin:auto;position:sticky;top:0;background:var(--bg);z-index:1}
nav button{flex:1;border:0;background:none;color:var(--muted);font:inherit;font-weight:600;padding:8px 0;border-bottom:2px solid transparent;cursor:pointer}
nav button.on{color:var(--ink);border-color:var(--accent)}
main{max-width:640px;margin:auto;padding:4px 16px 40px}
section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin:10px 0}
.card h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin:0 0 10px}
.now{font-size:20px;font-weight:650;margin:0 0 8px}
.bar{height:6px;background:var(--line);border-radius:3px;overflow:hidden}.bar i{display:block;height:100%;background:var(--accent);width:0}
.times{display:flex;justify-content:space-between;font-size:13px;color:var(--muted);margin-top:4px;font-variant-numeric:tabular-nums}
.row{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
button.b{border:1px solid var(--line);background:var(--card);color:var(--ink);font:inherit;font-weight:600;padding:10px 16px;border-radius:10px;cursor:pointer;flex:1}
button.b.primary{background:var(--accent);border-color:var(--accent);color:var(--accent-ink)}
button.b:active{transform:scale(.98)}
a.b{display:block;text-align:center;text-decoration:none;border:1px solid var(--line);background:var(--card);color:var(--ink);font-weight:600;padding:10px 16px;border-radius:10px;flex:1}
.seg{display:flex;border:1px solid var(--line);border-radius:10px;overflow:hidden;margin-top:12px}
.seg button{flex:1;border:0;background:none;color:var(--muted);font:inherit;padding:8px 0;cursor:pointer}
.seg button.on{background:var(--line);color:var(--ink);font-weight:600}
ul{list-style:none;margin:0;padding:0}
li.song{display:flex;justify-content:space-between;gap:8px;padding:10px 4px;border-bottom:1px solid var(--line);cursor:pointer}
li.song:last-child{border:0}li.song.on{color:var(--accent);font-weight:600}
li.song span:last-child{color:var(--muted);font-size:13px;white-space:nowrap;font-variant-numeric:tabular-nums}
.bells{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.bells button{aspect-ratio:1;border:1px solid var(--line);border-radius:50%;background:var(--card);color:var(--ink);font:inherit;font-weight:650;cursor:pointer}
.bells button:active{background:var(--accent);color:var(--accent-ink)}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:0;font-size:15px}
dt{color:var(--muted)}dd{margin:0;text-align:right;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
label{display:block;font-size:13px;color:var(--muted);margin:10px 0 4px}
input{width:100%;font:inherit;padding:10px;border:1px solid var(--line);border-radius:10px;background:var(--bg);color:var(--ink)}
li.net{display:flex;justify-content:space-between;padding:10px 4px;border-bottom:1px solid var(--line);cursor:pointer}
li.net span:last-child{color:var(--muted);font-size:13px}
.note{font-size:14px;color:var(--muted);margin:8px 0 0}
.sync{font-size:14px;color:var(--muted);margin:-2px 0 10px}
.sync b{color:var(--ink);font-weight:600}
.peer{padding:10px 0;border-bottom:1px solid var(--line)}
.peer:last-child{border:0}
.peer .top{display:flex;justify-content:space-between;gap:8px;align-items:baseline}
.peer a,.peer .top>span:first-child{color:var(--ink);font-weight:600;text-decoration:none}
.peer .meta{font-size:13px;color:var(--muted);margin-top:2px;font-variant-numeric:tabular-nums}
.tag{font-size:12px;padding:1px 8px;border-radius:99px;background:var(--line);color:var(--muted);margin-left:4px;white-space:nowrap}
.tag.ok{color:var(--ok)}.tag.warn{color:var(--warn)}
.note a{color:var(--accent)}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--ink);color:var(--bg);padding:10px 16px;border-radius:10px;font-size:14px;opacity:0;transition:opacity .2s;pointer-events:none;max-width:90%}
#toast.on{opacity:1}
</style>
</head>
<body>
<header><h1 id="name">Open Jingle Bells</h1><span class="pill" id="conn">…</span></header>
<nav>
<button data-tab="play" class="on">Play</button><button data-tab="bells">Bells</button><button data-tab="health">Health</button><button data-tab="setup">Setup</button>
</nav>
<main>
<section id="play" class="on">
<div class="card">
<h2>Now playing</h2>
<p class="now" id="now">—</p>
<p class="sync" id="syncline"></p>
<div class="bar"><i id="prog"></i></div>
<div class="times"><span id="t0">0:00</span><span id="t1">0:00</span></div>
<div class="row"><button class="b primary" id="playstop">Play</button><button class="b" id="next">Next</button></div>
<div class="seg" id="mode"><button data-mode="single">Once</button><button data-mode="loop">Repeat</button><button data-mode="all">Play all</button></div>
</div>
<div class="card"><h2>Songs</h2><ul id="songs"></ul><div class="row"><a class="b" href="/compose">Compose a song</a></div></div>
</section>
<section id="bells">
<div class="card"><h2>Bell test</h2><div class="bells" id="bellgrid"></div><p class="note">Tap a bell to strike it on this unit only.</p></div>
</section>
<section id="health">
<div class="card"><h2>Group</h2><dl id="h-sync"></dl><div id="peers" style="margin-top:10px"></div></div>
<div class="card"><h2>Playback</h2><dl id="h-play"></dl></div>
<div class="card"><h2>Network</h2><dl id="h-net"></dl></div>
<div class="card"><h2>Device</h2><dl id="h-dev"></dl></div>
</section>
<section id="setup">
<div class="card"><h2>Wi-Fi</h2><dl id="w-status"></dl><p class="note" id="w-note"></p>
<label for="ssid">Network</label><input id="ssid" autocomplete="off" autocapitalize="off">
<label for="pass">Password</label><input id="pass" type="password" autocomplete="off">
<div class="row"><button class="b primary" id="join">Join network</button><button class="b" id="forget">Forget</button></div>
</div>
<div class="card"><h2>Nearby networks</h2><ul id="nets"><li class="note">Scanning…</li></ul><div class="row"><button class="b" id="rescan">Scan again</button></div></div>
<div class="card"><h2>This unit</h2>
<label for="cfg-name">Name (also &lt;name&gt;.local)</label><input id="cfg-name" autocapitalize="off">
<label for="cfg-pulse">Strike length, ms (5–500)</label><input id="cfg-pulse" type="number" min="5" max="500">
<label for="cfg-ch">Setup AP channel (1–13)</label><input id="cfg-ch" type="number" min="1" max="13">
<label for="cfg-group">Sync group (1–255; units only play with their own group)</label><input id="cfg-group" type="number" min="1" max="255">
<label for="cfg-ofs">Strike offset, ms (−100–100; positive strikes this unit earlier)</label><input id="cfg-ofs" type="number" min="-100" max="100">
<div class="row"><button class="b primary" id="save">Save</button></div>
</div>
</section>
</main>
<div id="toast"></div>
<script>
const $=s=>document.querySelector(s);
const BELLS=[["C6",2],["D6",1],["E6",0],["F6",11],["F#6",10],["G6",9],["A6",8],["A#6",4],["B6",5],["C7",6],["D7",7],["E7",3]];
let songs=[],st=null,tabPicked=false,scanTimer=null;
const esc=s=>String(s).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const mmss=ms=>{const s=Math.max(0,Math.round(ms/1000));return Math.floor(s/60)+":"+String(s%60).padStart(2,"0")};
const dur=(n,step)=>mmss(n*step);
function toast(msg){const t=$("#toast");t.textContent=msg;t.classList.add("on");clearTimeout(t._h);t._h=setTimeout(()=>t.classList.remove("on"),2500)}
async function api(path,data){
  try{
    const r=await fetch(path,data?{method:"POST",body:new URLSearchParams(data)}:{cache:"no-store"});
    const j=await r.json();
    if(j&&j.ok===false)toast(j.error);
    return j;
  }catch(e){return null}
}
function tab(name){
  document.querySelectorAll("nav button").forEach(b=>b.classList.toggle("on",b.dataset.tab===name));
  document.querySelectorAll("section").forEach(s=>s.classList.toggle("on",s.id===name));
  if(name==="setup")scan();
}
document.querySelectorAll("nav button").forEach(b=>b.onclick=()=>{tabPicked=true;tab(b.dataset.tab)});
function dl(id,rows){$(id).innerHTML=rows.map(([k,v])=>`<dt>${esc(k)}</dt><dd>${esc(v)}</dd>`).join("")}
function render(){
  if(!st)return;
  const p=st.playback,w=st.wifi,d=st.device;
  $("#name").textContent=d.name;document.title=d.name;
  const c=$("#conn");
  c.className="pill "+(w.connected?"ok":"warn");
  c.textContent=w.connected?(w.ssid+" · "+w.rssi+" dBm"):(w.ap?"Setup mode":w.state);
  $("#now").textContent=p.playing?p.song_name:"Stopped";
  const frame=Math.max(0,p.frame);
  $("#prog").style.width=p.playing?(100*frame/p.frames)+"%":"0";
  $("#t0").textContent=p.playing?mmss(frame*p.step_ms):"0:00";
  $("#t1").textContent=dur(p.frames,p.step_ms);
  const ps=$("#playstop");ps.textContent=p.playing?"Stop":"Play";
  document.querySelectorAll("#mode button").forEach(b=>b.classList.toggle("on",b.dataset.mode===p.mode));
  document.querySelectorAll("li.song").forEach(li=>li.classList.toggle("on",p.playing&&+li.dataset.id===p.song));
  renderSync(st.sync,d,p);
  dl("#h-play",[["State",p.playing?"Playing":"Stopped"],["Song",p.song_name],["Frame",p.playing?`${frame} / ${p.frames}`:"—"],["Late strikes (>2 ms)",p.late_strikes],["Worst lateness",(p.max_late_us/1000).toFixed(2)+" ms"],["Strikes",d.strikes],["Library",`${st.library.songs} songs · ${st.library.crc}`]]);
  dl("#h-net",[["State",w.state],["Network",w.ssid||"—"],["Address",w.ip||"—"],["Host name",w.hostname],["Signal",w.rssi==null?"—":w.rssi+" dBm"],["Channel",w.channel],["Setup AP",w.ap?`${w.ap_ssid} (${w.ap_clients} connected)`:"off"]]);
  dl("#h-dev",[["Firmware",d.firmware],["Board",d.board],["Uptime",mmss(d.uptime_s*1000)],["Free heap",Math.round(d.heap/1024)+" KB (min "+Math.round(d.min_heap/1024)+" KB)"],["Last reset",d.reset_reason],["Strike length",d.pulse_ms+" ms"],["MAC",d.mac]]);
  dl("#w-status",[["State",w.state],["Network",w.ssid||"not set"],["Address",w.ip||"—"]]);
  let note="";
  if(w.connected&&w.ap)note=`Joined! Reconnect your phone to <b>${esc(w.ssid)}</b>, then open <a href="http://${esc(w.hostname)}/">http://${esc(w.hostname)}/</a> or <a href="http://${esc(w.ip)}/">http://${esc(w.ip)}/</a>.`;
  else if(w.state==="connecting")note=`Joining ${esc(w.ssid)}…`;
  else if(w.state==="fallback")note=`Couldn't join ${esc(w.ssid)}. Check the password, or pick another network. It keeps retrying in the background.`;
  else if(w.state==="setup")note="Pick your network to put this unit on Wi-Fi.";
  $("#w-note").innerHTML=note;
  if(!tabPicked&&!w.connected){tabPicked=true;tab("setup")}
}
const us=v=>v==null||v<0?"—":(v/1000).toFixed(2)+" ms";
function renderSync(y,d,p){
  let line;
  if(!y.active)line="Playing alone (not on Wi-Fi)";
  else if(y.library_mismatch)line="<b>Library mismatch</b>: this unit's songs differ from the conductor's. Reflash it.";
  else if(!y.synced)line="Syncing clock with the group…";
  else{
    const inSync=1+y.peers.filter(x=>x.synced).length,total=1+y.peers.length;
    const who=y.is_conductor?"this unit":(y.conductor?esc(y.conductor):null);
    line=total===1?"Alone in group "+y.group:`<b>${inSync} of ${total}</b> units in sync`;
    if(who)line+=` · conducted by <b>${who}</b>`;
  }
  $("#syncline").innerHTML=line;
  dl("#h-sync",[["Sync",!y.active?"off (not on Wi-Fi)":(y.synced?"synced":"syncing")],["Group",y.group],["Clock master",y.is_master?"this unit":(y.master||"—")],["Conductor",y.is_conductor?"this unit":(y.conductor||"none")],["Round trip",y.is_master?"—":us(y.rtt_us)],["Offset jitter",y.is_master?"—":us(y.jitter_us)],["Strike offset",y.strike_offset_ms+" ms"],["Packets",`${y.rx_packets} received`+(y.dropped_packets?`, ${y.dropped_packets} dropped`:"")]]);
  const self={name:d.name+" (this unit)",ip:null,synced:y.synced,master:y.is_master,conductor:y.is_conductor,library_match:!y.library_mismatch,rtt_us:y.is_master?0:y.rtt_us,jitter_us:y.is_master?0:y.jitter_us,late_strikes:p.late_strikes,rssi:st.wifi.rssi};
  const rows=y.active?[self,...y.peers]:[];
  $("#peers").innerHTML=rows.length<2?'<p class="note">'+(y.active?"No other units heard in this group yet.":"Join Wi-Fi to play with other units.")+"</p>":rows.map(x=>{
    const tags=(x.master?'<span class="tag">clock</span>':"")+(x.conductor?'<span class="tag">conductor</span>':"")+(x.library_match?"":'<span class="tag warn">library mismatch</span>')+`<span class="tag ${x.synced?"ok":"warn"}">${x.synced?"synced":"syncing"}</span>`;
    const name=x.ip?`<a href="http://${esc(x.ip)}/">${esc(x.name)}</a>`:`<span>${esc(x.name)}</span>`;
    return `<div class="peer"><div class="top">${name}<span>${tags}</span></div><div class="meta">${x.master?"clock source":"rtt "+us(x.rtt_us)+" · jitter "+us(x.jitter_us)} · late ${x.late_strikes} · ${x.rssi==null?"—":x.rssi+" dBm"}</div></div>`;
  }).join("");
}
async function poll(){const j=await api("/api/status");if(j){st=j;render()}setTimeout(poll,1000)}
async function loadSongs(){
  songs=await api("/api/songs")||[];
  $("#songs").innerHTML=songs.map(s=>`<li class="song" data-id="${s.id}"><span>${esc(s.name)}${s.user?'<span class="tag">composed</span>':""}</span><span>${dur(s.frames,s.step_ms)}</span></li>`).join("");
  document.querySelectorAll("li.song").forEach(li=>li.onclick=()=>api("/api/play",{song:li.dataset.id}).then(poll1));
}
async function poll1(){const j=await api("/api/status");if(j){st=j;render()}}
async function loadConfig(){
  const c=await api("/api/config");if(!c)return;
  $("#cfg-name").value=c.name;$("#cfg-pulse").value=c.pulse_ms;$("#cfg-ch").value=c.ap_channel;
  $("#cfg-group").value=c.group;$("#cfg-ofs").value=c.strike_offset_ms;
}
async function scan(){
  clearTimeout(scanTimer);
  const nets=await api("/api/wifi/scan");
  if(nets===null||nets===undefined){scanTimer=setTimeout(scan,1500);return}
  $("#nets").innerHTML=nets.length?nets.map(n=>`<li class="net" data-ssid="${esc(n.ssid)}"><span>${esc(n.ssid)}${n.secure?" 🔒":""}</span><span>${n.rssi} dBm · ch ${n.channel}</span></li>`).join(""):'<li class="note">No networks found.</li>';
  document.querySelectorAll("li.net").forEach(li=>li.onclick=()=>{$("#ssid").value=li.dataset.ssid;$("#pass").focus()});
}
$("#playstop").onclick=()=>api(st&&st.playback.playing?"/api/stop":"/api/play",{}).then(poll1);
$("#next").onclick=()=>api("/api/next",{}).then(poll1);
document.querySelectorAll("#mode button").forEach(b=>b.onclick=()=>api("/api/mode",{mode:b.dataset.mode}).then(poll1));
$("#bellgrid").innerHTML=BELLS.map(([n,bit])=>`<button data-word="${1<<bit}">${n}</button>`).join("");
document.querySelectorAll("#bellgrid button").forEach(b=>b.onclick=()=>api("/api/strike",{word:b.dataset.word}));
$("#join").onclick=async()=>{
  const ssid=$("#ssid").value.trim();
  if(!ssid){toast("Pick a network first");return}
  const r=await api("/api/wifi",{ssid,pass:$("#pass").value});
  if(r&&r.ok)toast("Joining "+ssid+"…");
};
$("#forget").onclick=async()=>{
  if(!confirm("Forget the Wi-Fi network? The unit goes back to setup mode."))return;
  const r=await api("/api/wifi/forget",{});
  if(r&&r.ok)toast("Forgotten. Join the "+(st?st.wifi.ap_ssid:"JingleBells")+" network to set it up again.");
};
$("#rescan").onclick=()=>{$("#nets").innerHTML='<li class="note">Scanning…</li>';scan()};
$("#save").onclick=async()=>{
  const r=await api("/api/config",{name:$("#cfg-name").value,pulse_ms:$("#cfg-pulse").value,ap_channel:$("#cfg-ch").value,group:$("#cfg-group").value,strike_offset_ms:$("#cfg-ofs").value});
  if(r&&r.ok!==false){toast("Saved");loadConfig();poll1()}
};
loadSongs();loadConfig();poll();
</script>
</body>
</html>
)HTML";
