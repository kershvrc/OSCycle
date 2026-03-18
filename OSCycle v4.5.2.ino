#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <LittleFS.h>

struct NetConfig {
  char ssid[32]; char pass[32]; char dnsName[32]; char oscIP[16];
  int oscPort; uint32_t localIP; uint32_t gateway; uint32_t subnet;
} net;

struct AppConfig {
  float oscIntensity; int turnPulses; float wheelCirc; float speedMulti;
  uint32_t debounce; bool useMPH; bool showSpd; bool showDist; bool showWatts;
  bool showTime; bool showActive; int chatRate; bool chatboxEnable;
  bool showCustom; char customText[32]; int uiTheme;
} app;

const NetConfig NET_DEFAULTS = { "roflpwnt", "lesswire", "bike", "192.168.2.131", 9000, 0xF702A8C0, 0x0102A8C0, 0x00FFFFFF };
const AppConfig APP_DEFAULTS = { 0.25f, 37, 0.493f, 10.0f, 150000, true, true, true, true, false, true, 3000, true, true, "oscycle by kersh", 0 };

WiFiUDP Udp;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
const int sensorPin = 12;
volatile uint32_t lastPulseMicros = 0, pulseInterval = 0, totalPulses = 0;
float dashboardSpd = 0, distanceKM = 0, watts = 0, steeringVal = 0;
bool isReverse = false, driveDisabled = false;
unsigned long lastOscMs = 0, lastMetricMs = 0, lastChatMs = 0, tripStartMs = 0, activeWorkoutMs = 0, lastActiveCheckMs = 0;
float totalSpdSum = 0, activeSpdSum = 0;
uint32_t totalSamples = 0, activeSamples = 0;
String lastRawOSC = "Waiting for data...";

void ICACHE_RAM_ATTR handlePulse() {
  uint32_t now = micros();
  if (now - lastPulseMicros > app.debounce) { pulseInterval = now - lastPulseMicros; lastPulseMicros = now; totalPulses++; }
}

void saveConfigs() {
  File f = LittleFS.open("/net.bin", "w"); if(f){ f.write((uint8_t*)&net, sizeof(NetConfig)); f.close(); }
  f = LittleFS.open("/app.bin", "w"); if(f){ f.write((uint8_t*)&app, sizeof(AppConfig)); f.close(); }
}

void loadConfigs() {
  if (LittleFS.exists("/net.bin")) { File f = LittleFS.open("/net.bin", "r"); f.read((uint8_t*)&net, sizeof(NetConfig)); f.close(); } else { net = NET_DEFAULTS; }
  if (LittleFS.exists("/app.bin")) { File f = LittleFS.open("/app.bin", "r"); f.read((uint8_t*)&app, sizeof(AppConfig)); f.close(); } else { app = APP_DEFAULTS; }
}

uint32_t strToIP(String s) { IPAddress ip; ip.fromString(s); return (uint32_t)ip; }

void sendOSC(const char* path, float value) {
  Udp.beginPacket(net.oscIP, net.oscPort);
  Udp.write(path);
  int pad = 4 - (strlen(path) % 4); for (int i = 0; i < pad; i++) Udp.write((uint8_t)0);
  Udp.write(",f"); Udp.write((uint8_t)0); Udp.write((uint8_t)0);
  union { float f; uint8_t b[4]; } u; u.f = value;
  Udp.write(u.b[3]); Udp.write(u.b[2]); Udp.write(u.b[1]); Udp.write(u.b[0]);
  Udp.endPacket();
}

void sendChat(String msg) {
  Udp.beginPacket(net.oscIP, net.oscPort);
  const char* path = "/chatbox/input";
  Udp.write(path);
  int pPad = 4 - (strlen(path) % 4); for(int i=0; i<pPad; i++) Udp.write((uint8_t)0);
  Udp.write(",sT"); Udp.write((uint8_t)0);
  Udp.write(msg.c_str());
  int mPad = 4 - (msg.length() % 4); for(int i=0; i<mPad; i++) Udp.write((uint8_t)0);
  Udp.endPacket();
  lastRawOSC = msg;
}

String formatTime(uint32_t ms) {
  uint32_t totalSecs = ms / 1000;
  int h = totalSecs / 3600; int m = (totalSecs % 3600) / 60; int s = totalSecs % 60;
  char buf[12]; sprintf(buf, "%02d:%02d:%02d", h, m, s);
  return String(buf);
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>OSCycle</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  html += "<style>";

  // ── Theme variables ───────────────────────────────────────────────────
  // 0: Cyan/black hacker (original)
  // 1: Neon green terminal
  // 2: Red/dark sporty
  // 3: Minimal white on dark
  if (app.uiTheme == 1) {
    html += ":root{--bg:#000;--bg2:#0a0a0a;--bg3:#050505;--acc:#00ff41;--acc2:#007a1f;--acc3:#001a05;--txt:#00ff41;--txt2:#aaa;--txt3:#fff;--brd:#005c17;--brd2:#002a0a;--btn-bg:#001a05;--btn-brd:#00ff41;--card-bg:#050505;--tab-inactive:#007a1f;--tab-active:#00ff41;--graph-spd:#00ff41;--graph-avg:#ffe000;--graph-dst:#ff41c8;--danger:#f33;--danger-brd:#400;--rev-on:#002200;}";
  } else if (app.uiTheme == 2) {
    html += ":root{--bg:#0d0000;--bg2:#1a0000;--bg3:#0a0000;--acc:#ff3322;--acc2:#882211;--acc3:#2a0000;--txt:#ff3322;--txt2:#cc4433;--txt3:#fff;--brd:#661100;--brd2:#330800;--btn-bg:#1a0000;--btn-brd:#ff3322;--card-bg:#0d0000;--tab-inactive:#882211;--tab-active:#ff3322;--graph-spd:#ff3322;--graph-avg:#ff8844;--graph-dst:#ff44aa;--danger:#ff6666;--danger-brd:#600;--rev-on:#400;}";
  } else if (app.uiTheme == 3) {
    html += ":root{--bg:#111;--bg2:#1a1a1a;--bg3:#0e0e0e;--acc:#fff;--acc2:#888;--acc3:#333;--txt:#fff;--txt2:#aaa;--txt3:#fff;--brd:#444;--brd2:#2a2a2a;--btn-bg:#1a1a1a;--btn-brd:#fff;--card-bg:#161616;--tab-inactive:#666;--tab-active:#fff;--graph-spd:#fff;--graph-avg:#aaa;--graph-dst:#ccc;--danger:#ff4444;--danger-brd:#500;--rev-on:#3a0000;}";
  } else {
    // default: cyan/black hacker
    html += ":root{--bg:#000;--bg2:#111;--bg3:#050505;--acc:#0bc;--acc2:#068;--acc3:#023;--txt:#0bc;--txt2:#aaa;--txt3:#fff;--brd:#046;--brd2:#023;--btn-bg:#012;--btn-brd:#0bc;--card-bg:#050505;--tab-inactive:#068;--tab-active:#0bc;--graph-spd:#0bc;--graph-avg:#ff0;--graph-dst:#f0f;--danger:#f33;--danger-brd:#400;--rev-on:#600;}";
  }

  html += "*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;}";
  html += "body{background:var(--bg);color:var(--txt);font-family:monospace;text-align:center;margin:0;}";

  // Header
  html += ".header{background:var(--bg2);padding:14px 16px;border-bottom:2px solid var(--acc);display:flex;justify-content:space-between;align-items:center;}";
  html += ".header h1{margin:0;font-size:1.4em;letter-spacing:3px;color:var(--acc);text-transform:uppercase;}";
  html += ".header-right{display:flex;align-items:center;gap:10px;}";
  html += "#tt{font-size:0.9em;color:var(--acc2);}";

  // Theme dots
  html += ".theme-dots{display:flex;gap:7px;align-items:center;}";
  html += ".tdot{width:16px;height:16px;border-radius:50%;cursor:pointer;border:2px solid transparent;transition:border 0.2s;flex-shrink:0;}";
  html += ".tdot.active{border-color:#fff;}";
  html += ".tdot-0{background:#0bc;}.tdot-1{background:#00ff41;}.tdot-2{background:#ff3322;}.tdot-3{background:#888;}";

  // Tabs
  html += ".tab-bar{display:flex;background:var(--bg2);}";
  html += ".tab{flex:1;padding:14px 2px;cursor:pointer;color:var(--tab-inactive);font-size:0.85em;font-weight:bold;border-bottom:3px solid transparent;transition:color 0.2s,border 0.2s;}";
  html += ".tab.active{color:var(--tab-active);border-bottom:3px solid var(--acc);}";

  // Pages & cards
  html += ".page{display:none;padding:10px;} .active-page{display:block;}";
  html += ".card{border:1px solid var(--brd);margin:6px 0;padding:12px;border-radius:12px;background:var(--card-bg);text-align:left;}";

  // Buttons — no user-select, touch-action manipulation to kill long-press menu
  html += ".btn{background:var(--btn-bg);color:var(--txt);border:1px solid var(--btn-brd);padding:14px;border-radius:8px;width:100%;margin:5px 0;cursor:pointer;font-weight:bold;font-size:1em;font-family:monospace;letter-spacing:1px;-webkit-appearance:none;user-select:none;-webkit-user-select:none;touch-action:manipulation;}";
  html += ".btn:active{opacity:0.7;}";

  // Big speed
  html += ".big{font-size:4.5em;color:var(--txt3);text-align:center;font-weight:bold;line-height:1.1;}";
  html += ".unit-lbl{font-size:1.1em;color:var(--acc2);text-align:center;cursor:pointer;font-weight:bold;letter-spacing:3px;margin-bottom:6px;}";

  // Stat row
  html += ".stat-row{display:flex;justify-content:space-around;font-size:0.95em;color:var(--txt2);margin-top:6px;text-align:center;}";
  html += ".stat-row b{font-size:1.1em;color:var(--txt3);}";

  // Label
  html += ".label{color:var(--acc2);font-size:0.8em;text-transform:uppercase;letter-spacing:2px;margin-bottom:8px;border-bottom:1px solid var(--brd2);padding-bottom:5px;}";

  // Inputs
  html += "input[type=text],input[type=number],input[type=password]{background:var(--bg);color:var(--txt);border:1px solid var(--brd);padding:8px;border-radius:6px;font-family:monospace;font-size:1em;width:150px;text-align:right;}";
  html += "input[type=checkbox]{width:22px;height:22px;cursor:pointer;accent-color:var(--acc);}";
  html += "input[type=range]{width:100%;accent-color:var(--acc);height:6px;margin:6px 0;}";
  html += ".set-row{display:flex;justify-content:space-between;padding:9px 0;align-items:center;font-size:1em;border-bottom:1px solid var(--brd2);}";
  html += ".set-row:last-child{border-bottom:none;}";
  html += ".help{font-size:0.78em;color:var(--acc2);margin-bottom:8px;line-height:1.4;}";

  // Control buttons — same long-press fix
  html += ".cbtn{flex:1;background:var(--btn-bg);color:var(--txt);border:1px solid var(--btn-brd);border-radius:8px;font-size:1.9em;font-weight:bold;cursor:pointer;padding:16px 4px;text-align:center;-webkit-appearance:none;user-select:none;-webkit-user-select:none;touch-action:manipulation;}";
  html += ".cbtn:active{opacity:0.6;}";
  html += ".cbtn-mid{flex:1.3;font-size:1em;background:#034;letter-spacing:1px;}";
  html += ".cbtn-strafe{font-size:1em;letter-spacing:2px;padding:15px 4px;}";
  html += ".ctrl-row{display:flex;gap:8px;margin:5px 0;}";

  // Active timer
  html += "#tw{font-size:1.1em;color:#0f0;font-weight:bold;text-align:center;margin-bottom:4px;}";

  // Sensitivity slider card
  html += ".sens-val{font-size:1.6em;font-weight:bold;color:var(--txt3);text-align:center;margin:4px 0;}";

  // Debug log
  html += "#debugLog{background:#010;color:#0f0;padding:10px;font-size:0.9em;border:1px solid #030;margin-top:10px;min-height:44px;border-radius:6px;word-break:break-all;}";

  html += "</style></head><body>";

  // ── Header ──────────────────────────────────────────────────────────────
  html += "<div class='header'>";
  html += "<h1>OSCycle</h1>";
  html += "<div class='header-right'>";
  html += "<div class='theme-dots'>";
  for(int i=0;i<4;i++){
    html += "<div class='tdot tdot-"+String(i)+(app.uiTheme==i?" active":"")+"' onclick='setTheme("+String(i)+")'></div>";
  }
  html += "</div>";
  html += "<div id='tt'>UP: 00:00:00</div>";
  html += "</div></div>";

  // ── Tab Bar ─────────────────────────────────────────────────────────────
  html += "<div class='tab-bar'>";
  html += "<div class='tab active' onclick='openT(event,\"dash\")'>DASH</div>";
  html += "<div class='tab' onclick='openT(event,\"tune\")'>TUNE</div>";
  html += "<div class='tab' onclick='openT(event,\"osc\")'>OSC</div>";
  html += "<div class='tab' onclick='openT(event,\"net\")'>NET</div>";
  html += "<div class='tab' onclick='openT(event,\"sys\")'>SYS</div>";
  html += "</div>";

  // ── DASHBOARD ───────────────────────────────────────────────────────────
  html += "<div id='dash' class='page active-page'>";

  // Speed card
  html += "<div class='card'>";
  html += "<div id='tw'>ACT: 00:00:00</div>";
  html += "<div class='big' id='s'>0.0</div>";
  html += "<div class='unit-lbl' id='u' onclick='fetch(\"/t\").then(()=>location.reload())'>"+String(app.useMPH?"MPH":"KMH")+"</div>";
  html += "<div class='stat-row'><div>AVG ACT<br><b id='aas'>0.0</b></div><div>AVG TOT<br><b id='as'>0.0</b></div><div>DIST<br><b id='dst'>0.0</b></div></div>";
  html += "<button class='btn' onclick='fetch(\"/reset_trip\")' style='margin-top:10px;'>RESET TRIP</button>";
  html += "</div>";

  // Mode toggles
  html += "<div style='display:flex;gap:8px;margin:6px 0;'>";
  html += "<button class='btn' style='background:#034;' onclick='fetch(\"/drive_toggle\").then(()=>location.reload())'>"+String(driveDisabled?"STATIONARY":"DRIVING")+"</button>";
  html += "<button class='btn' style='background:"+String(isReverse?"var(--rev-on)":"var(--btn-bg)")+";border-color:"+String(isReverse?"var(--danger)":"var(--btn-brd)")+"' onclick='fetch(\"/rev\").then(()=>location.reload())'>REV: "+String(isReverse?"ON":"OFF")+"</button>";
  html += "</div>";

  // OSC Sensitivity slider
  html += "<div class='card'>";
  html += "<div class='label'>OSC Sensitivity</div>";
  html += "<div class='sens-val' id='oiVal'>"+String(app.oscIntensity,2)+"</div>";
  html += "<input type='range' min='0.05' max='1.5' step='0.05' id='oiSlider' value='"+String(app.oscIntensity,2)+"' oninput='updateSens(this.value)'>";
  html += "<div class='help' style='text-align:center;margin-top:4px;'>Moving too slow in VR? Increase. Moving too fast? Decrease.</div>";
  html += "</div>";

  // Controls card
  html += "<div class='card'>";
  html += "<div class='label'>Controls</div>";
  html += "<div class='ctrl-row'>";
  html += "<button class='cbtn' onmousedown='st(-1)' onmouseup='st(0)' ontouchstart='st(-1)' ontouchend='st(0)'>&#8592;</button>";
  html += "<button class='cbtn cbtn-mid' onclick='fetch(\"/uturn\")'>180&deg;</button>";
  html += "<button class='cbtn' onmousedown='st(1)' onmouseup='st(0)' ontouchstart='st(1)' ontouchend='st(0)'>&#8594;</button>";
  html += "</div>";
  html += "<div class='ctrl-row'>";
  html += "<button class='cbtn cbtn-strafe' onmousedown='sf(-1)' onmouseup='sf(0)' ontouchstart='sf(-1)' ontouchend='sf(0)'>&#9664; STRAFE</button>";
  html += "<button class='cbtn cbtn-strafe' onmousedown='sf(1)' onmouseup='sf(0)' ontouchstart='sf(1)' ontouchend='sf(0)'>STRAFE &#9654;</button>";
  html += "</div>";
  html += "</div>";

  // Graphs
  html += "<div class='card'><div class='label'>Speed vs Avg Active</div><canvas id='sC' style='max-height:90px;'></canvas></div>";
  html += "<div class='card'><div class='label'>Distance Over Time</div><canvas id='dC' style='max-height:90px;'></canvas></div>";

  html += "</div>"; // end dash

  // ── TUNE ────────────────────────────────────────────────────────────────
  html += "<div id='tune' class='page'><div class='card'><div class='label'>Hardware Calibration</div>";
  html += "<div class='set-row'>Wheel Circ (m)<input type='number' step='0.001' id='wc' value='"+String(app.wheelCirc,3)+"'></div>";
  html += "<div class='help'>Distance in meters for 1 rotation. Measure your tire!</div>";
  html += "<div class='set-row'>Speed Multi<input type='number' id='sm' value='"+String(app.speedMulti)+"'></div>";
  html += "<div class='help'>Digital boost to speed. Default 10.0 matches VRC locomotion.</div>";
  html += "<div class='set-row'>Debounce (μs)<input type='number' id='db' value='"+String(app.debounce)+"'></div>";
  html += "<div class='help'>Microseconds to ignore ghost pulses. Increase if speed is jumpy.</div>";
  html += "<div class='set-row'>OSC Intensity<input type='number' step='0.05' id='oi' value='"+String(app.oscIntensity,2)+"'></div>";
  html += "<div class='help'>Also adjustable via slider on the DASH tab.</div>";
  html += "</div><button class='btn' onclick='saveApp()'>SAVE TUNE</button></div>";

  // ── OSC ─────────────────────────────────────────────────────────────────
  html += "<div id='osc' class='page'><div class='card'><div class='label'>Chatbox Master</div>";
  html += "<div class='set-row'>Enable Chatbox<input type='checkbox' id='cEn' "+String(app.chatboxEnable?"checked":"")+" ></div>";
  html += "<div class='label' style='margin-top:10px;'>Fields</div>";
  html += "<div class='set-row'>Speed<input type='checkbox' id='cSpd' "+String(app.showSpd?"checked":"")+" ></div>";
  html += "<div class='set-row'>Distance<input type='checkbox' id='cDst' "+String(app.showDist?"checked":"")+" ></div>";
  html += "<div class='set-row'>Watts<input type='checkbox' id='cW' "+String(app.showWatts?"checked":"")+" ></div>";
  html += "<div class='set-row'>Total Time<input type='checkbox' id='cTm' "+String(app.showTime?"checked":"")+" ></div>";
  html += "<div class='set-row'>Active Time<input type='checkbox' id='cAc' "+String(app.showActive?"checked":"")+" ></div>";
  html += "<div class='label' style='margin-top:10px;'>Custom Signature</div>";
  html += "<div class='set-row'>Show Text<input type='checkbox' id='cCust' "+String(app.showCustom?"checked":"")+" ></div>";
  html += "<div class='set-row'>Text<input type='text' id='cTxt' maxlength='31' value='"+String(app.customText)+"'></div>";
  html += "<div class='set-row'>Update Rate (ms)<input type='number' id='cRate' value='"+String(app.chatRate)+"'></div>";
  html += "<div class='label'>Preview</div><div id='debugLog'>...</div>";
  html += "</div><button class='btn' onclick='saveOsc()'>SAVE OSC SETTINGS</button></div>";

  // ── NET ─────────────────────────────────────────────────────────────────
  html += "<div id='net' class='page'><div class='card'><div class='label'>Wireless</div>";
  html += "<div class='set-row'>SSID<input type='text' id='ssid' value='"+String(net.ssid)+"'></div>";
  html += "<div class='set-row'>Pass<input type='password' id='pass' value='"+String(net.pass)+"'></div>";
  html += "<div class='set-row'>mDNS Name<input type='text' id='dns' value='"+String(net.dnsName)+"'></div>";
  html += "</div><div class='card'><div class='label'>Target (VRChat PC)</div>";
  html += "<div class='set-row'>OSC IP<input type='text' id='oip' value='"+String(net.oscIP)+"'></div>";
  html += "<div class='set-row'>OSC Port<input type='number' id='oport' value='"+String(net.oscPort)+"'></div>";
  html += "</div><div class='card'><div class='label'>Device IP (Static)</div>";
  html += "<div class='set-row'>Local IP<input type='text' id='lip' value='"+IPAddress(net.localIP).toString()+"'></div>";
  html += "<div class='set-row'>Gateway<input type='text' id='gw' value='"+IPAddress(net.gateway).toString()+"'></div>";
  html += "<div class='set-row'>Subnet<input type='text' id='sub' value='"+IPAddress(net.subnet).toString()+"'></div>";
  html += "</div><button class='btn' onclick='saveNet()' style='background:#420;'>SAVE & REBOOT</button></div>";

  // ── SYS ─────────────────────────────────────────────────────────────────
  html += "<div id='sys' class='page'><div class='card'><div class='label'>System Info</div>";
  html += "<div class='set-row'>Device IP<b id='ip'>0.0.0.0</b></div>";
  html += "<div class='set-row'>RAM Free<b id='ram'>0</b></div>";
  html += "<div class='set-row'>Version<b>v4.5.4</b></div>";
  html += "</div><div class='card'><div class='label'>Maintenance</div>";
  html += "<button class='btn' onclick='window.location=\"/net.bin\"'>BACKUP NET.BIN</button>";
  html += "<button class='btn' onclick='window.location=\"/app.bin\"'>BACKUP APP.BIN</button>";
  html += "</div>";
  html += "<button class='btn' onclick='fetch(\"/reset\")' style='color:var(--danger);border-color:var(--danger-brd);'>FACTORY RESET</button></div>";

  // ── Scripts ─────────────────────────────────────────────────────────────
  html += "<script>";

  html += "function openT(e,n){var i,p,t;p=document.getElementsByClassName('page');for(i=0;i<p.length;i++)p[i].classList.remove('active-page');t=document.getElementsByClassName('tab');for(i=0;i<t.length;i++)t[i].classList.remove('active');document.getElementById(n).classList.add('active-page');e.currentTarget.classList.add('active');}";

  html += "function st(v){fetch('/steer?v='+v);}";
  html += "function sf(v){fetch('/strafe?v='+v);}";

  html += "function setTheme(n){fetch('/sTheme?t='+n).then(()=>location.reload());}";

  html += "var sensTimer=null;";
  html += "function updateSens(v){";
  html += "document.getElementById('oiVal').innerText=parseFloat(v).toFixed(2);";
  html += "clearTimeout(sensTimer);";
  html += "sensTimer=setTimeout(function(){fetch('/sSens?v='+v);},400);}";

  html += "function saveOsc(){fetch('/sOsc?e='+(document.getElementById('cEn').checked?1:0)+'&s='+(document.getElementById('cSpd').checked?1:0)+'&d='+(document.getElementById('cDst').checked?1:0)+'&w='+(document.getElementById('cW').checked?1:0)+'&t='+(document.getElementById('cTm').checked?1:0)+'&a='+(document.getElementById('cAc').checked?1:0)+'&r='+document.getElementById('cRate').value+'&cs='+(document.getElementById('cCust').checked?1:0)+'&ct='+encodeURIComponent(document.getElementById('cTxt').value)).then(()=>alert('Saved'));}";

  html += "function saveApp(){fetch('/sApp?wc='+document.getElementById('wc').value+'&sm='+document.getElementById('sm').value+'&db='+document.getElementById('db').value+'&oi='+document.getElementById('oi').value).then(()=>alert('Saved'));}";

  html += "function saveNet(){let url='/sNet?s='+document.getElementById('ssid').value+'&p='+document.getElementById('pass').value+'&d='+document.getElementById('dns').value+'&o='+document.getElementById('oip').value+'&op='+document.getElementById('oport').value+'&li='+document.getElementById('lip').value+'&gw='+document.getElementById('gw').value+'&su='+document.getElementById('sub').value;fetch(url).then(()=>alert('Rebooting...'));}";

  html += "const sC=new Chart(document.getElementById('sC').getContext('2d'),{type:'line',data:{labels:[],datasets:[{label:'Speed',borderColor:'var(--graph-spd)',data:[],pointRadius:0,borderWidth:2},{label:'AvgActive',borderColor:'var(--graph-avg)',data:[],pointRadius:0,borderWidth:1,borderDash:[5,5]}]},options:{animation:false,scales:{y:{beginAtZero:true},x:{display:false}},plugins:{legend:{display:false}}}});";
  html += "const dC=new Chart(document.getElementById('dC').getContext('2d'),{type:'line',data:{labels:[],datasets:[{label:'Distance',borderColor:'var(--graph-dst)',data:[],pointRadius:0,borderWidth:2}]},options:{animation:false,scales:{y:{beginAtZero:true},x:{display:false}},plugins:{legend:{display:false}}}});";

  html += "setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('s').innerText=d.s.toFixed(1);";
  html += "document.getElementById('dst').innerText=d.d.toFixed(2);";
  html += "document.getElementById('ram').innerText=d.f;";
  html += "document.getElementById('ip').innerText=d.ip;";
  html += "document.getElementById('tt').innerText='UP: '+d.tt;";
  html += "document.getElementById('tw').innerText='ACT: '+d.tw;";
  html += "document.getElementById('as').innerText=d.as.toFixed(1);";
  html += "document.getElementById('aas').innerText=d.aas.toFixed(1);";
  html += "document.getElementById('debugLog').innerText=d.dbg;";
  html += "if(sC.data.labels.length>30){sC.data.labels.shift();sC.data.datasets[0].data.shift();sC.data.datasets[1].data.shift();dC.data.labels.shift();dC.data.datasets[0].data.shift();}";
  html += "sC.data.labels.push('');sC.data.datasets[0].data.push(d.s);sC.data.datasets[1].data.push(d.aas);sC.update();";
  html += "dC.data.labels.push('');dC.data.datasets[0].data.push(d.d);dC.update();";
  html += "});},1000);";

  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

void setup() {
  system_update_cpu_freq(160); LittleFS.begin(); loadConfigs();
  WiFi.config(net.localIP, net.gateway, net.subnet); WiFi.begin(net.ssid, net.pass);
  while (WiFi.status() != WL_CONNECTED && millis() < 8000) delay(500);
  tripStartMs = millis(); lastActiveCheckMs = millis();
  MDNS.begin(net.dnsName); httpUpdater.setup(&server); ArduinoOTA.begin();
  pinMode(sensorPin, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(sensorPin), handlePulse, FALLING);

  server.on("/data", [](){
    float conv = app.useMPH ? 0.621371f : 1.0f;
    server.send(200, "application/json",
      "{\"s\":"+String(dashboardSpd*conv,2)+
      ",\"d\":"+String(distanceKM*conv,3)+
      ",\"as\":"+String((totalSamples>0?totalSpdSum/totalSamples:0)*conv,2)+
      ",\"aas\":"+String((activeSamples>0?activeSpdSum/activeSamples:0)*conv,2)+
      ",\"f\":"+String(ESP.getFreeHeap())+
      ",\"ip\":\""+WiFi.localIP().toString()+"\""+
      ",\"tt\":\""+formatTime(millis()-tripStartMs)+"\""+
      ",\"tw\":\""+formatTime(activeWorkoutMs)+"\""+
      ",\"dbg\":\""+lastRawOSC+"\"}"
    );
  });

  server.on("/", handleRoot);
  server.serveStatic("/net.bin", LittleFS, "/net.bin");
  server.serveStatic("/app.bin", LittleFS, "/app.bin");

  server.on("/sApp", [](){
    app.wheelCirc=server.arg("wc").toFloat();
    app.speedMulti=server.arg("sm").toFloat();
    app.debounce=server.arg("db").toInt();
    app.oscIntensity=server.arg("oi").toFloat();
    saveConfigs(); server.send(200);
  });

  server.on("/sSens", [](){
    app.oscIntensity=server.arg("v").toFloat();
    saveConfigs(); server.send(200);
  });

  server.on("/sTheme", [](){
    app.uiTheme=server.arg("t").toInt();
    saveConfigs(); server.send(200);
  });

  server.on("/sOsc", [](){
    app.chatboxEnable=server.arg("e")=="1";
    app.showSpd=server.arg("s")=="1";
    app.showDist=server.arg("d")=="1";
    app.showWatts=server.arg("w")=="1";
    app.showTime=server.arg("t")=="1";
    app.showActive=server.arg("a")=="1";
    app.chatRate=server.arg("r").toInt();
    app.showCustom=server.arg("cs")=="1";
    strncpy(app.customText, server.arg("ct").c_str(), 32);
    saveConfigs(); server.send(200);
  });

  server.on("/sNet", [](){
    strncpy(net.ssid, server.arg("s").c_str(), 32);
    strncpy(net.pass, server.arg("p").c_str(), 32);
    strncpy(net.dnsName, server.arg("d").c_str(), 32);
    strncpy(net.oscIP, server.arg("o").c_str(), 16);
    net.oscPort=server.arg("op").toInt();
    net.localIP=strToIP(server.arg("li"));
    net.gateway=strToIP(server.arg("gw"));
    net.subnet=strToIP(server.arg("su"));
    saveConfigs(); server.send(200); delay(1000); ESP.restart();
  });

  server.on("/reset_trip", [](){
    distanceKM=0; totalPulses=0; tripStartMs=millis();
    activeWorkoutMs=0; totalSpdSum=0; totalSamples=0;
    activeSpdSum=0; activeSamples=0;
    server.send(200);
  });

  server.on("/t",            [](){ app.useMPH=!app.useMPH; saveConfigs(); server.send(200); });
  server.on("/uturn",        [](){ for(int i=0;i<app.turnPulses;i++){ sendOSC("/input/LookHorizontal",1.0f); delay(25); } sendOSC("/input/LookHorizontal",0.0f); server.send(200); });
  server.on("/steer",        [](){ steeringVal=server.arg("v").toFloat(); server.send(200); });
  server.on("/strafe",       [](){ sendOSC("/input/Horizontal", server.arg("v").toFloat()); server.send(200); });
  server.on("/rev",          [](){ isReverse=!isReverse; server.send(200); });
  server.on("/drive_toggle", [](){ driveDisabled=!driveDisabled; server.send(200); });
  server.on("/reset",        [](){ LittleFS.format(); ESP.restart(); });

  server.begin();
}

void loop() {
  ArduinoOTA.handle(); server.handleClient();
  unsigned long now = millis();

  if (dashboardSpd > 0.5) { activeWorkoutMs += (now - lastActiveCheckMs); }
  lastActiveCheckMs = now;

  if (micros() - lastPulseMicros > 1500000) {
    dashboardSpd = 0; watts = 0;
  } else if (pulseInterval > 0) {
    dashboardSpd = ((app.wheelCirc / (pulseInterval / 1000000.0)) * 3.6) * app.speedMulti;
    watts = (pow(dashboardSpd, 3) * 0.015) + (dashboardSpd * 1.2);
  }

  if (now - lastOscMs >= 45) {
    float move = driveDisabled ? 0.0f : min(1.0f, (dashboardSpd * app.oscIntensity) / 10.0f);
    sendOSC("/input/Vertical", isReverse ? -move : move);
    sendOSC("/input/LookHorizontal", steeringVal);
    lastOscMs = now;
  }

  if (now - lastChatMs >= (unsigned long)app.chatRate) {
    if (app.chatboxEnable) {
      float conv = app.useMPH ? 0.621371f : 1.0f;
      String msg = "🚴 ";
      if(app.showActive) msg += "["+formatTime(activeWorkoutMs)+"] ";
      if(app.showSpd)    msg += String(dashboardSpd*conv,1)+(app.useMPH?"mph ":"kmh ");
      if(app.showDist)   msg += "| "+String(distanceKM*conv,2)+(app.useMPH?"mi ":"km ");
      if(app.showWatts)  msg += "| "+String(watts,0)+"W ";
      if(app.showTime)   msg += "| UP: "+formatTime(now-tripStartMs);
      if(app.showCustom) msg += " | "+String(app.customText);
      sendChat(msg);
    }
    lastChatMs = now;
  }

  if (now - lastMetricMs >= 1000) {
    if (dashboardSpd > 1.0) distanceKM += (dashboardSpd / 3600.0);
    totalSpdSum += dashboardSpd; totalSamples++;
    if (dashboardSpd > 0.5) { activeSpdSum += dashboardSpd; activeSamples++; }
    lastMetricMs = now;
  }
}
