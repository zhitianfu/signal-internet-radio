#include "web_server.h"
#include "config.h"
#include "debug.h"
#include "station_manager.h"
#include "audio.h"
#include "wifi_manager.h"
#include <WebServer.h>
#include <ArduinoJson.h>

static WebServer server(80);
extern StreamPlayer player;   // defined in the sketch

// ------------------------------------------------------------------
// Management page (embedded; dark theme, mobile friendly)
// ------------------------------------------------------------------
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Radio Config</title>
<style>
:root{--bg:#111;--fg:#eee;--acc:#fd20;--ok:#3d3}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--fg);font:16px system-ui,sans-serif;padding:16px;max-width:760px;margin:0 auto}
h1{font-size:22px;margin-bottom:4px}
#stat{color:#9a9;font-size:13px;margin-bottom:16px}
h2{font-size:17px;margin:22px 0 8px;color:#fda}
input{width:100%;padding:9px;margin:4px 0;background:#222;color:var(--fg);border:1px solid #444;border-radius:6px;font-size:15px}
button{padding:10px 14px;margin:6px 4px 0 0;border:none;border-radius:6px;background:#333;color:var(--fg);font-size:15px;cursor:pointer}
button.acc{background:var(--acc);color:#000;font-weight:bold}
button.del{background:#611}
.st{background:#1a1a1a;border:1px solid #333;border-radius:8px;padding:10px;margin:8px 0}
.st .nm{font-weight:bold}
.st .url{color:#777;font-size:12px;word-break:break-all}
.st .gn{color:#a97;font-size:12px}
.small{font-size:12px;color:#888}
#toast{position:fixed;bottom:16px;left:0;right:0;text-align:center;display:none;pointer-events:none}
#toast div{display:inline-block;background:#0a0;color:#fff;padding:8px 18px;border-radius:20px;font-size:14px}
</style></head><body>
<h1>ESP32 Internet Radio</h1>
<div id="stat">loading...</div>
<div style="margin:8px 0">
<button onclick="stopRadio()">Stop</button>
<span class="small">Volume</span>
<input type="range" id="vol" min="0" max="100" value="85" oninput="setVol(this.value)" style="width:55%;vertical-align:middle">
</div>

<h2>+ Add / edit station</h2>
<form id="f">
<input type="hidden" id="fid" value="">
<input id="fn" placeholder="Station name" required>
<input id="fu" placeholder="Stream URL (http / https)" required>
<input id="fg" placeholder="Genre (optional)">
<div>
<button type="submit" class="acc" id="fsave">Add</button>
<button type="button" id="fcancel" style="display:none">Cancel</button>
</div>
</form>

<h2>Stations</h2>
<div id="list"></div>

<h2>WiFi</h2>
<form id="wf">
<input id="ws" placeholder="WiFi SSID" required>
<input id="wp" placeholder="Password">
<div><button type="submit" class="acc">Save &amp; restart</button></div>
</form>
<div class="small">Open this page at http://<span id="ip">-</span></div>

<div id="toast"><div id="toastmsg"></div></div>
<script>
const $=id=>document.getElementById(id);
let stations=[];
function toast(m){const t=$('toast');$('toastmsg').textContent=m;t.style.display='block';setTimeout(()=>t.style.display='none',2500);}
function esc(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML;}
async function j(u,o){const r=await fetch(u,o);return r.json();}
async function load(){
  try{
    const s=await j('/api/status');
    $('stat').textContent='Mode: '+s.mode+' | IP: '+s.ip+' | RSSI: '+s.rssi+' dBm | Station: '+s.station+' | '+s.state;
    $('ip').textContent=s.ip;
    $('vol').value=s.volume;
  }catch(e){$('stat').textContent='status unavailable';}
  try{
    stations=await j('/api/stations');
  }catch(e){stations=[];}
  const l=$('list');l.innerHTML='';
  stations.forEach((st,i)=>{
    const el=document.createElement('div');el.className='st';
    el.innerHTML='<div class="nm">'+esc(st.name)+(st.playing?' <span style="color:var(--ok)">&#9654;</span>':'')+'</div>'+
      '<div class="url">'+esc(st.url)+'</div>'+
      (st.genre?'<div class="gn">'+esc(st.genre)+'</div>':'')+
      '<button class="acc" onclick="play('+i+')">Play</button>'+
      '<button onclick="edit('+i+')">Edit</button><button class="del" onclick="del('+i+')">Delete</button>';
    l.appendChild(el);
  });
}
async function play(i){const r=await j('/api/play',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'id='+i});toast(r.ok?'Playing':'Error');load();}
async function stopRadio(){const r=await j('/api/stop',{method:'POST'});toast(r.ok?'Stopped':'Error');load();}
async function setVol(v){await fetch('/api/volume',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'vol='+v});}
function edit(i){
  const st=stations[i];
  $('fid').value=i;$('fn').value=st.name;$('fu').value=st.url;$('fg').value=st.genre||'';
  $('fsave').textContent='Save';$('fcancel').style.display='inline';
}
function cancelEdit(){$('fid').value='';$('f').reset();$('fsave').textContent='Add';$('fcancel').style.display='none';}
async function del(i){
  if(!confirm('Delete "'+stations[i].name+'"?'))return;
  const r=await j('/api/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'id='+i});
  toast(r.ok?'Deleted':'Error: '+(r.error||'?'));
  load();
}
$('f').addEventListener('submit',async e=>{
  e.preventDefault();
  const id=$('fid').value;
  const body='name='+encodeURIComponent($('fn').value)+'&url='+encodeURIComponent($('fu').value)+'&genre='+encodeURIComponent($('fg').value)+(id?'&id='+id:'');
  const r=await j(id?'/api/update':'/api/add',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  toast(r.ok?(id?'Saved':'Added'):'Error: '+(r.error||'?'));
  if(r.ok){cancelEdit();load();}
});
$('wf').addEventListener('submit',async e=>{
  e.preventDefault();
  const body='ssid='+encodeURIComponent($('ws').value)+'&pass='+encodeURIComponent($('wp').value);
  toast('Saving — restarting...');
  try{await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});}catch(err){}
  setTimeout(()=>location.reload(),3000);
});
$('fcancel').addEventListener('click',cancelEdit);
load();
</script></body></html>
)HTML";

// ------------------------------------------------------------------
// API helpers
// ------------------------------------------------------------------
static void sendJson(JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void sendOk(bool ok, const char* err = nullptr) {
  JsonDocument doc;
  doc["ok"] = ok;
  if (!ok && err) doc["error"] = err;
  sendJson(doc);
}

static void handleStatus() {
  JsonDocument doc;
  doc["mode"]    = wifiIsAP() ? "AP" : "STA";
  doc["ip"]      = wifiGetIP();
  doc["rssi"]    = wifiIsAP() ? 0 : WiFi.RSSI();
  doc["station"] = stationManager.getCurrentStation().name;
  doc["state"]   = player.stateName();
  doc["volume"]  = player.getVolume();
  sendJson(doc);
}

static void handleList() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  uint8_t cur = stationManager.getCurrentIndex();
  for (uint8_t i = 0; i < stationManager.getStationCount(); i++) {
    RadioStation st = stationManager.getStation(i);
    JsonObject o = arr.add<JsonObject>();
    o["name"]    = st.name;
    o["url"]     = st.url;
    o["genre"]   = st.genre;
    o["playing"] = (i == cur);
  }
  sendJson(doc);
}

static void handleAdd() {
  const char* name  = server.hasArg("name")  ? server.arg("name").c_str()  : "";
  const char* url   = server.hasArg("url")   ? server.arg("url").c_str()   : "";
  const char* genre = server.hasArg("genre") ? server.arg("genre").c_str() : "";
  if (stationManager.addStation(name, url, genre)) sendOk(true);
  else sendOk(false, "invalid or list full");
}

static void handleUpdate() {
  int id = server.hasArg("id") ? server.arg("id").toInt() : -1;
  const char* name  = server.hasArg("name")  ? server.arg("name").c_str()  : "";
  const char* url   = server.hasArg("url")   ? server.arg("url").c_str()   : "";
  const char* genre = server.hasArg("genre") ? server.arg("genre").c_str() : "";
  if (id >= 0 && stationManager.updateStation((uint8_t)id, name, url, genre)) sendOk(true);
  else sendOk(false, "bad id or invalid fields");
}

static void handleDelete() {
  int id = server.hasArg("id") ? server.arg("id").toInt() : -1;
  if (id >= 0 && stationManager.deleteStation((uint8_t)id)) sendOk(true);
  else sendOk(false, "bad id");
}

// ---- remote control (also usable via plain GET in a browser) ----
static void handlePlay() {
  int id = server.hasArg("id") ? server.arg("id").toInt() : -1;
  if (id >= 0 && id < (int)stationManager.getStationCount()) {
    stationManager.setStation((uint8_t)id);
    player.startStream(stationManager.getCurrentStation().url.c_str());
    sendOk(true);
  } else {
    sendOk(false, "bad id");
  }
}

static void handleStop() {
  player.stopStream();
  sendOk(true);
}

static void handleVolume() {
  int v = server.hasArg("vol") ? server.arg("vol").toInt() : -1;
  if (v >= 0 && v <= 100) {
    player.setVolume((uint8_t)v);
    sendOk(true);
  } else {
    sendOk(false, "bad vol (0-100)");
  }
}

static void handleStep(bool next) {
  if (next) stationManager.nextStation();
  else stationManager.previousStation();
  player.startStream(stationManager.getCurrentStation().url.c_str());
  sendOk(true);
}

static void handleWifi() {
  const char* ssid = server.hasArg("ssid") ? server.arg("ssid").c_str() : "";
  const char* pass = server.hasArg("pass") ? server.arg("pass").c_str() : "";
  if (!ssid || !*ssid) { sendOk(false, "ssid required"); return; }
  wifiSaveCredentials(ssid, pass);
  sendOk(true);
  server.client().flush();     // make sure the reply goes out
  delay(400);
  ESP.restart();               // clean reconnect with the new credentials
}

// ------------------------------------------------------------------
void webInit() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/api/status",  HTTP_GET,  handleStatus);
  server.on("/api/stations", HTTP_GET, handleList);
  server.on("/api/add",     HTTP_POST, handleAdd);
  server.on("/api/update",  HTTP_POST, handleUpdate);
  server.on("/api/delete",  HTTP_POST, handleDelete);
  server.on("/api/wifi",    HTTP_POST, handleWifi);
  server.on("/api/play",    HTTP_ANY,  handlePlay);
  server.on("/api/stop",    HTTP_ANY,  handleStop);
  server.on("/api/volume",  HTTP_ANY,  handleVolume);
  server.on("/api/next",    HTTP_ANY,  []() { handleStep(true); });
  server.on("/api/prev",    HTTP_ANY,  []() { handleStep(false); });

  server.onNotFound([]() {
    // Captive portal: answer unknown hosts so phones get redirected
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  LOG_I("web", "config server up — http://%s", wifiGetIP().c_str());
}

void webLoop() {
  wifiLoop();                 // captive-portal DNS in AP mode
  server.handleClient();
}
