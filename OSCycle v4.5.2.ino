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
  bool showCustom; char customText[32];
} app;

// RESTORED DEFAULTS & SECRETS
const NetConfig NET_DEFAULTS = { "WIFISSIDHERE", "WIFIPASSWORDHERE", "oscycle", "HOSTCOMPUTERIPHERE", 9000, 0xF702A8C0, 0x0102A8C0, 0x00FFFFFF }; 
const AppConfig APP_DEFAULTS = { 0.4f, 37, 0.493f, 10.0f, 150000, true, true, true, true, false, true, 3000, true, true, "oscycle by kersh" };

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
  String html = "<html><head><title>OSCycle</title><meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  html += "<style>body{background:#000;color:#0bc;font-family:monospace;text-align:center;margin:0;} ";
  html += ".header{background:#111;padding:15px;border-bottom:2px solid #0bc;display:flex;justify-content:space-between;align-items:center;} ";
  html += ".header h1{margin:0;font-size:1.2em;letter-spacing:3px;color:#0bc;text-transform:uppercase;} ";
  html += ".tab-bar{display:flex;background:#111;} .tab{flex:1;padding:12px 2px;cursor:pointer;color:#068;font-size:0.75em;} .tab.active{color:#0bc;border-bottom:2px solid #0bc;} ";
  html += ".page{display:none;padding:10px;} .active-page{display:block;} .card{border:1px solid #046;margin:5px;padding:10px;border-radius:12px;background:#050505;text-align:left;} ";
  html += ".btn{background:#012;color:#0bc;border:1px solid #0bc;padding:12px;border-radius:8px;width:100%;margin:5px 0;cursor:pointer;font-weight:bold;} ";
  html += ".big{font-size:3.2em;color:#fff;text-align:center;} .label{color:#068;font-size:0.75em;text-transform:uppercase;margin-bottom:8px;border-bottom:1px solid #023;} ";
  html += "input{background:#000;color:#0bc;border:1px solid #046;padding:5px;width:140px;text-align:right;font-family:monospace;} ";
  html += ".stat-row{display:flex;justify-content:space-around;font-size:0.8em;color:#aaa;margin-top:5px;text-align:center;} ";
  html += ".set-row{display:flex;justify-content:space-between;padding:8px 0;align-items:center;} ";
  html += ".help{font-size:0.65em;color:#555;margin-bottom:10px;line-height:1.2;} ";
  html += "#debugLog{background:#010;color:#0f0;padding:8px;font-size:0.75em;border:1px solid #030;margin-top:10px;height:45px;overflow:hidden;}</style></head><body>";
  
  html += "<div class='header'><h1>OSCycle</h1><div id='tt' style='font-size:0.75em;color:#068;'>UP: 00:00:00</div></div>";
  html += "<div class='tab-bar'><div class='tab active' onclick='openT(event,\"dash\")'>DASH</div><div class='tab' onclick='openT(event,\"tune\")'>TUNE</div><div class='tab' onclick='openT(event,\"osc\")'>OSC</div><div class='tab' onclick='openT(event,\"net\")'>NET</div><div class='tab' onclick='openT(event,\"sys\")'>SYS</div></div>";

  // DASHBOARD (Restored 2 Graphs)
  html += "<div id='dash' class='page active-page'><div class='card'><div class='stat-row'><div id='tw' style='color:#0f0;'>ACT: 00:00:00</div></div><div class='big' id='s'>0.0</div><div id='u' onclick='fetch(\"/t\").then(()=>location.reload())' style='cursor:pointer; font-weight:bold;text-align:center;'>"+String(app.useMPH?"MPH":"KMH")+"</div>";
  html += "<div class='stat-row'><div>AVG ACT<br><b id='aas'>0.0</b></div><div>AVG TOT<br><b id='as'>0.0</b></div><div>DIST<br><b id='dst'>0.0</b></div></div><button class='btn' onclick='fetch(\"/reset_trip\")'>RESET TRIP</button>";
  html += "<div style='display:flex;gap:5px;margin-top:5px;'><button class='btn' style='background:#034;flex:1' onclick='fetch(\"/drive_toggle\").then(()=>location.reload())'>"+String(driveDisabled?"STATIONARY":"DRIVING")+"</button>";
  html += "<button class='btn' style='background:"+String(isReverse?"#600":"#012")+";flex:1' onclick='fetch(\"/rev\").then(()=>location.reload())'>REV: "+String(isReverse?"ON":"OFF")+"</button></div></div>";
  html += "<div class='card' style='text-align:center;'><div style='display:flex;justify-content:center;'><button class='btn' style='width:28%;height:60px;font-size:2em;' onmousedown='st(-1)' onmouseup='st(0)' ontouchstart='st(-1)' ontouchend='st(0)'>&larr;</button><button class='btn' style='background:#034;width:38%;' onclick='fetch(\"/uturn\")'>180&deg;</button><button class='btn' style='width:28%;height:60px;font-size:2em;' onmousedown='st(1)' onmouseup='st(0)' ontouchstart='st(1)' ontouchend='st(0)'>&rarr;</button></div></div>";
  html += "<div class='card'><div class='label'>Speed vs Avg Active</div><canvas id='sC' style='max-height:80px;'></canvas></div>";
  html += "<div class='card'><div class='label'>Distance Over Time</div><canvas id='dC' style='max-height:80px;'></canvas></div></div>";

  // TUNE (Restored Help Text)
  html += "<div id='tune' class='page'><div class='card'><div class='label'>Hardware Calibration</div>";
  html += "<div class='set-row'>Wheel Circ:<input type='number' step='0.001' id='wc' value='"+String(app.wheelCirc,3)+"'></div><div class='help'>Distance (meters) for 1 rotation. Measure your tire!</div>";
  html += "<div class='set-row'>Speed Multi:<input type='number' id='sm' value='"+String(app.speedMulti)+"'></div><div class='help'>Digital boost to speed. Default 10.0 matches VRC locomotion.</div>";
  html += "<div class='set-row'>Debounce:<input type='number' id='db' value='"+String(app.debounce)+"'></div><div class='help'>Microseconds to ignore 'ghost' pulses. Increase if speed is jumpy.</div>";
  html += "<div class='set-row'>OSC Intensity:<input type='number' step='0.1' id='oi' value='"+String(app.oscIntensity)+"'></div><div class='help'>Sensitivity. Higher = faster avatar walking at low RPM.</div>";
  html += "</div><button class='btn' onclick='saveApp()'>SAVE TUNE</button></div>";

  // OSC PAGE (Includes Custom Text & Preview)
  html += "<div id='osc' class='page'><div class='card'><div class='label'>Chatbox Master</div><div class='set-row'>Enable Chatbox <input type='checkbox' id='cEn' "+String(app.chatboxEnable?"checked":"")+"></div>";
  html += "<div class='label'>Fields</div><div class='set-row'>Speed <input type='checkbox' id='cSpd' "+String(app.showSpd?"checked":"")+"></div><div class='set-row'>Dist <input type='checkbox' id='cDst' "+String(app.showDist?"checked":"")+"></div><div class='set-row'>Watts <input type='checkbox' id='cW' "+String(app.showWatts?"checked":"")+"></div><div class='set-row'>Total <input type='checkbox' id='cTm' "+String(app.showTime?"checked":"")+"></div><div class='set-row'>Active <input type='checkbox' id='cAc' "+String(app.showActive?"checked":"")+"></div>";
  html += "<div class='label' style='margin-top:10px;'>Custom Signature</div><div class='set-row'>Show Text <input type='checkbox' id='cCust' "+String(app.showCustom?"checked":"")+"></div>";
  html += "<div class='set-row'>Text:<input type='text' id='cTxt' maxlength='31' value='"+String(app.customText)+"'></div>";
  html += "<div class='set-row'>Update Rate (ms):<input type='number' id='cRate' value='"+String(app.chatRate)+"'></div>";
  html += "<div class='label'>Preview</div><div id='debugLog'>...</div></div><button class='btn' onclick='saveOsc()'>SAVE OSC SETTINGS</button></div>";

  // NET (Restored DNS/Port/Subnet)
  html += "<div id='net' class='page'><div class='card'><div class='label'>Wireless</div><div class='set-row'>SSID:<input type='text' id='ssid' value='"+String(net.ssid)+"'></div><div class='set-row'>Pass:<input type='password' id='pass' value='"+String(net.pass)+"'></div>";
  html += "<div class='set-row'>mDNS Name:<input type='text' id='dns' value='"+String(net.dnsName)+"'></div><div class='label' style='margin-top:10px;'>Target (VRChat PC)</div>";
  html += "<div class='set-row'>OSC IP:<input type='text' id='oip' value='"+String(net.oscIP)+"'></div><div class='set-row'>OSC Port:<input type='number' id='oport' value='"+String(net.oscPort)+"'></div>";
  html += "<div class='label' style='margin-top:10px;'>Device IP (Static)</div><div class='set-row'>Local IP:<input type='text' id='lip' value='"+IPAddress(net.localIP).toString()+"'></div><div class='set-row'>Gateway:<input type='text' id='gw' value='"+IPAddress(net.gateway).toString()+"'></div><div class='set-row'>Subnet:<input type='text' id='sub' value='"+IPAddress(net.subnet).toString()+"'></div>";
  html += "</div><button class='btn' onclick='saveNet()' style='background:#420;'>SAVE & REBOOT</button></div>";

  // SYS (Restored Backups, Version, IP)
  html += "<div id='sys' class='page'><div class='card'><div class='label'>System Info</div><div class='set-row'>Device IP:<b id='ip'>0.0.0.0</b></div><div class='set-row'>RAM Free:<b id='ram'>0</b></div><div class='set-row'>Version:<b>v4.5.2</b></div></div>";
  html += "<div class='card'><div class='label'>Maintenance</div><button class='btn' onclick='window.location=\"/net.bin\"'>BACKUP NET.BIN</button><button class='btn' onclick='window.location=\"/app.bin\"'>BACKUP APP.BIN</button></div>";
  html += "<button class='btn' onclick='fetch(\"/reset\")' style='color:#f33;border-color:#400;'>FACTORY RESET</button></div>";

  html += "<script>function openT(e,n){var i,p,t;p=document.getElementsByClassName('page');for(i=0;i<p.length;i++)p[i].classList.remove('active-page');t=document.getElementsByClassName('tab');for(i=0;i<t.length;i++)t[i].classList.remove('active');document.getElementById(n).classList.add('active-page');e.currentTarget.classList.add('active');}";
  html += "function st(v){fetch('/steer?v='+v);} ";
  html += "function saveOsc(){fetch('/sOsc?e='+(document.getElementById('cEn').checked?1:0)+'&s='+(document.getElementById('cSpd').checked?1:0)+'&d='+(document.getElementById('cDst').checked?1:0)+'&w='+(document.getElementById('cW').checked?1:0)+'&t='+(document.getElementById('cTm').checked?1:0)+'&a='+(document.getElementById('cAc').checked?1:0)+'&r='+document.getElementById('cRate').value+'&cs='+(document.getElementById('cCust').checked?1:0)+'&ct='+encodeURIComponent(document.getElementById('cTxt').value)).then(()=>alert('Saved'));}";
  html += "function saveApp(){fetch('/sApp?wc='+document.getElementById('wc').value+'&sm='+document.getElementById('sm').value+'&db='+document.getElementById('db').value+'&oi='+document.getElementById('oi').value).then(()=>alert('Saved'));}";
  html += "function saveNet(){let url='/sNet?s='+document.getElementById('ssid').value+'&p='+document.getElementById('pass').value+'&d='+document.getElementById('dns').value+'&o='+document.getElementById('oip').value+'&op='+document.getElementById('oport').value+'&li='+document.getElementById('lip').value+'&gw='+document.getElementById('gw').value+'&su='+document.getElementById('sub').value; fetch(url).then(()=>alert('Rebooting...'));}";
  html += "const sC=new Chart(document.getElementById('sC').getContext('2d'),{type:'line',data:{labels:[],datasets:[{label:'Speed',borderColor:'#0bc',data:[],pointRadius:0,borderWidth:2},{label:'AvgActive',borderColor:'#ff0',data:[],pointRadius:0,borderWidth:1,borderDash:[5,5]}]},options:{animation:false,scales:{y:{beginAtZero:true},x:{display:false}},plugins:{legend:{display:false}}}});";
  html += "const dC=new Chart(document.getElementById('dC').getContext('2d'),{type:'line',data:{labels:[],datasets:[{label:'Distance',borderColor:'#f0f',data:[],pointRadius:0,borderWidth:2}]},options:{animation:false,scales:{y:{beginAtZero:true},x:{display:false}},plugins:{legend:{display:false}}}});";
  html += "setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('s').innerText=d.s.toFixed(1); document.getElementById('dst').innerText=d.d.toFixed(2); document.getElementById('ram').innerText=d.f; document.getElementById('ip').innerText=d.ip; document.getElementById('tt').innerText='UP: '+d.tt; document.getElementById('tw').innerText='ACT: '+d.tw; document.getElementById('as').innerText=d.as.toFixed(1); document.getElementById('aas').innerText=d.aas.toFixed(1); document.getElementById('debugLog').innerText=d.dbg;";
  html += "if(sC.data.labels.length>30){sC.data.labels.shift();sC.data.datasets[0].data.shift();sC.data.datasets[1].data.shift();dC.data.labels.shift();dC.data.datasets[0].data.shift();} sC.data.labels.push(''); sC.data.datasets[0].data.push(d.s); sC.data.datasets[1].data.push(d.aas); sC.update(); dC.data.labels.push(''); dC.data.datasets[0].data.push(d.d); dC.update();});},1000);</script></body></html>";
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
    float conv = app.useMPH ? 0.621371 : 1.0;
    server.send(200, "application/json", "{\"s\":"+String(dashboardSpd*conv)+",\"d\":"+String(distanceKM*conv)+",\"as\":"+String((totalSamples>0?totalSpdSum/totalSamples:0)*conv)+",\"aas\":"+String((activeSamples>0?activeSpdSum/activeSamples:0)*conv)+",\"f\":"+String(ESP.getFreeHeap())+",\"ip\":\""+WiFi.localIP().toString()+"\",\"tt\":\""+formatTime(millis()-tripStartMs)+"\",\"tw\":\""+formatTime(activeWorkoutMs)+"\",\"dbg\":\""+lastRawOSC+"\"}");
  });
  server.on("/", handleRoot);
  server.serveStatic("/net.bin", LittleFS, "/net.bin");
  server.serveStatic("/app.bin", LittleFS, "/app.bin");
  server.on("/sApp", [](){ app.wheelCirc=server.arg("wc").toFloat(); app.speedMulti=server.arg("sm").toFloat(); app.debounce=server.arg("db").toInt(); app.oscIntensity=server.arg("oi").toFloat(); saveConfigs(); server.send(200); });
  server.on("/sOsc", [](){ 
    app.chatboxEnable=server.arg("e")=="1"; app.showSpd=server.arg("s")=="1"; app.showDist=server.arg("d")=="1"; 
    app.showWatts=server.arg("w")=="1"; app.showTime=server.arg("t")=="1"; app.showActive=server.arg("a")=="1"; 
    app.chatRate=server.arg("r").toInt(); app.showCustom=server.arg("cs")=="1";
    strncpy(app.customText, server.arg("ct").c_str(), 32);
    saveConfigs(); server.send(200); 
  });
  server.on("/sNet", [](){ 
    strncpy(net.ssid, server.arg("s").c_str(), 32); strncpy(net.pass, server.arg("p").c_str(), 32); 
    strncpy(net.dnsName, server.arg("d").c_str(), 32); strncpy(net.oscIP, server.arg("o").c_str(), 16);
    net.oscPort = server.arg("op").toInt(); net.localIP = strToIP(server.arg("li"));
    net.gateway = strToIP(server.arg("gw")); net.subnet = strToIP(server.arg("su"));
    saveConfigs(); server.send(200); delay(1000); ESP.restart(); 
  });
  server.on("/reset_trip", [](){ distanceKM = 0; totalPulses = 0; tripStartMs = millis(); activeWorkoutMs = 0; totalSpdSum = 0; totalSamples = 0; activeSpdSum = 0; activeSamples = 0; server.send(200); });
  server.on("/t", [](){ app.useMPH = !app.useMPH; saveConfigs(); server.send(200); });
  server.on("/uturn", [](){ for(int i=0; i<app.turnPulses; i++){ sendOSC("/input/LookHorizontal", 1.0f); delay(25); } sendOSC("/input/LookHorizontal", 0.0f); server.send(200); });
  server.on("/steer", [](){ steeringVal = server.arg("v").toFloat(); server.send(200); });
  server.on("/rev", [](){ isReverse = !isReverse; server.send(200); });
  server.on("/drive_toggle", [](){ driveDisabled = !driveDisabled; server.send(200); });
  server.on("/reset", [](){ LittleFS.format(); ESP.restart(); });
  server.begin();
}

void loop() {
  ArduinoOTA.handle(); server.handleClient();
  unsigned long now = millis();
  if (dashboardSpd > 0.5) { activeWorkoutMs += (now - lastActiveCheckMs); }
  lastActiveCheckMs = now;
  if (micros() - lastPulseMicros > 1500000) { dashboardSpd = 0; watts = 0; } 
  else if (pulseInterval > 0) {
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
      float conv = app.useMPH ? 0.621371 : 1.0;
      String msg = "🚴 "; 
      if(app.showActive) msg += "[" + formatTime(activeWorkoutMs) + "] ";
      if(app.showSpd) msg += String(dashboardSpd * conv, 1) + (app.useMPH ? "mph " : "kmh ");
      if(app.showDist) msg += "| " + String(distanceKM * conv, 2) + (app.useMPH ? "mi " : "km ");
      if(app.showWatts) msg += "| " + String(watts, 0) + "W ";
      if(app.showTime) msg += "| UP: " + formatTime(now - tripStartMs);
      if(app.showCustom) msg += " | " + String(app.customText);
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
