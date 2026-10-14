// Bicycle Control - ESP32-C6 WiFi Access Point + Web Control Panel
// E-bike controller ko throttle signal dene ke liye.
// Arduino-ESP32 core 3.x, board "ESP32C6 Dev Module".
//
// WiFi : "bicycle control" (WPA2, password AP_PASS)
// Page : http://192.168.4.1 (connect karte hi khud khulta hai)
//
// Buttons: START      = console + TB6612 on
//          STOP       = cycle band, throttle 0, driver standby
//          CALL CYCLE = dabate hi throttle CALL_VOLTAGE (1.8V) par chala deta hai
//                       (cycle chalne ki threshold). Dobara dabane par ruk jata hai.
//          HOLD TO RUN = jab tak dabaye rakho tab tak CALL_VOLTAGE (1.8V) par chalegi, chhodte hi ruk jayegi
//          SPEED slider = 0% -> 1.8V (threshold), 100% -> THROTTLE_MAX (3.4V),
//                       cycle chalte waqt live badalta hai
// Safety : page ka status-poll 1s band -> throttle 0 | throttle ramp | boot par STBY LOW
//
// Output chain: GPIO0 PWM -> TB6612 PWMA -> AO1 (VM ka PWM)
//               -> inductor 100-220uH -> 220uF + 100nF -> throttle signal wire
//   Average voltage = duty x VM.
//
// Wiring (poori detail connections.txt me):
//   TB6612 VM   -> 5V supply (+)          TB6612 VCC  -> ESP32 3V3
//   TB6612 GND  -> ESP32 GND + supply (-) + controller GND (common)
//   TB6612 PWMA -> GPIO0                  TB6612 AIN1 -> GPIO1 (HIGH)
//   TB6612 AIN2 -> GPIO2 (LOW)            TB6612 STBY -> GPIO3 (+ 10k to GND, optional)
//   TB6612 AO1  -> inductor -> signal wire (cap 220uF+100nF to GND)
//   TB6612 AO2  -> khali
//
// Pehle bina controller ke multimeter/resistor par test karo.

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

const char* AP_SSID = "bicycle control";
const char* AP_PASS = "bicycle123";        // kam se kam 8 character, badal lena

// TB6612 pins (strapping pins avoid kiye hain)
const int PIN_PWMA = 0;   // PWM -> PWMA
const int PIN_AIN1 = 1;   // HIGH
const int PIN_AIN2 = 2;   // LOW
const int PIN_STBY = 3;   // HIGH = driver on

const float VM             = 5.0;   // TB6612 VM supply (volt)
const float THROTTLE_MAX   = 3.4;   // controller ka max throttle voltage
const float CALL_VOLTAGE   = 1.8;   // CALL CYCLE dabane par ye voltage (cycle chalne ki threshold)
const float RAMP_V_PER_SEC = 1.5;   // throttle kitni tezi se badhe
const unsigned long DEADMAN_MS = 1000;
const unsigned long HOLD_TIMEOUT_MS = 500;   // HOLD button ka signal itni der na aaye to throttle 0
const float HOLD_RAMP_V_PER_SEC = 20.0;       // HOLD dabane par throttle jaldi 1.8V tak pahunche

const int PWM_RES  = 10;
const int PWM_FREQ = 20000;

WebServer server(80);
DNSServer dns;
const IPAddress apIP(192, 168, 4, 1);

bool  systemOn = false;
bool  calling  = false;  // CALL CYCLE chalu hai?
bool  holding  = false;  // HOLD button dabaa hua hai?
unsigned long lastHold = 0;
int   speedPct = 0;      // speed slider: 0% = CALL_VOLTAGE (1.8V), 100% = THROTTLE_MAX
float current  = 0.0;    // abhi actual output voltage
unsigned long lastBeat = 0, lastTick = 0;

void writeVoltage(float v) {
  if (v < 0) v = 0;
  if (v > THROTTLE_MAX) v = THROTTLE_MAX;
  current = v;
  ledcWrite(PIN_PWMA, (uint32_t)(v / VM * ((1 << PWM_RES) - 1) + 0.5));
}


// cycle chal rahi ho to: 1.8V (speed 0%) se THROTTLE_MAX (speed 100%) ke beech
float targetVolt() {
  if (!calling) return holding ? CALL_VOLTAGE : 0.0;
  return CALL_VOLTAGE + speedPct / 100.0 * (THROTTLE_MAX - CALL_VOLTAGE);
}

const char PAGE_HEAD[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Bicycle Control</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--line:#262d38;--txt:#e6edf3;--mut:#8b949e;--g:#2ea043;--r:#da3633;--b:#1f6feb;--y:#d29922}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;background:radial-gradient(circle at top,#16202e,#0d1117 60%);color:var(--txt);font-family:system-ui,sans-serif;display:flex;justify-content:center}
.app{width:100%;max-width:420px;padding:20px 16px}
h1{font-size:22px;text-align:center;margin:8px 0 2px}
.sub{text-align:center;color:var(--mut);font-size:13px;margin-bottom:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:18px;text-align:center}
.pill{display:inline-block;padding:5px 14px;border-radius:99px;font-size:13px;font-weight:600;background:#30363d}
.pill.ready{background:#9e6a03}.pill.run{background:var(--g)}
.gauge{--p:0;width:200px;height:200px;margin:16px auto;border-radius:50%;background:conic-gradient(var(--b) calc(var(--p)*1%),#232a35 0);display:flex;align-items:center;justify-content:center}
.gauge.run{background:conic-gradient(var(--g) calc(var(--p)*1%),#232a35 0)}
.gauge div{width:156px;height:156px;border-radius:50%;background:var(--card);display:flex;flex-direction:column;align-items:center;justify-content:center}
.gauge b{font-size:38px}.gauge span{color:var(--mut);font-size:13px}
.row{display:flex;gap:10px}
button{flex:1;border:0;border-radius:14px;padding:16px;font-size:17px;font-weight:700;color:#fff;cursor:pointer}
button:active{transform:scale(.97)}
#start{background:var(--g)}#stop{background:var(--r)}
#call{width:100%;margin-top:12px;padding:22px;font-size:20px;background:var(--b)}
#call.run{background:var(--y);color:#111}
button:disabled{opacity:.35;pointer-events:none}
input[type=range]{width:100%;height:36px;accent-color:var(--b)}
.lab{display:flex;justify-content:space-between;font-size:13px;color:var(--mut);margin:4px 0}
.lab b{color:var(--txt);font-size:15px}
.card+.card{margin-top:14px}
#hold{width:100%;margin-top:12px;padding:22px;font-size:18px;background:#6e40c9;touch-action:none;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none}
#hold.down{background:var(--g)}
.hint{color:var(--mut);font-size:12px;margin-top:6px}
#warn{display:none;background:#5a1d1d;color:#ffb4b4;text-align:center;border-radius:10px;padding:8px;font-size:13px;margin-bottom:12px}
</style></head><body><div class="app">
<h1>Bicycle Control</h1><div class="sub">WiFi: bicycle control</div>
<div id="warn">Connection lost</div>
<div class="card">
<span class="pill" id="pill">OFF</span>
<div class="gauge" id="g"><div><b id="v">0.0</b><span>volt</span></div></div>
<div class="row"><button id="start" onclick="api('/start')">START</button><button id="stop" onclick="api('/stop')">STOP</button></div>
<button id="call" onclick="callCycle()">CALL CYCLE</button>
<button id="hold" disabled onpointerdown="holdOn(event)" onpointerup="holdOff()" onpointercancel="holdOff()" onlostpointercapture="holdOff()" oncontextmenu="return false">HOLD TO RUN</button>
<div class="hint">Dabaye rakho = threshold par chalegi, chhodte hi ruk jayegi</div>
</div>
<div class="card">
<div class="lab"><span>SPEED CONTROL</span><b id="sp">0% (1.80V)</b></div>
<input id="sl" type="range" min="0" max="100" step="1" value="0" disabled oninput="setSpeed(this.value)" onpointerdown="drag=1" onpointerup="drag=0" onchange="drag=0;api('/speed?p='+this.value)">
<div class="lab"><span id="lo">Start 1.8V</span><span id="hi">Max 3.4V</span></div>
</div></div>
<script>
var MAX=)rawliteral";
const char PAGE_TAIL[] PROGMEM = R"rawliteral(,st={on:false,call:false,hold:false,v:0,sp:0,lo:1.8},lost=0,drag=0,last=0,ht=null;
function $(i){return document.getElementById(i)}
function render(){
 var p=$('pill'),g=$('g'),c=$('call');
 var run=st.call||st.hold;
 p.className='pill'+(run?' run':(st.on?' ready':''));
 p.textContent=st.call?'CYCLE RUNNING':(st.hold?'HOLD RUNNING':(st.on?'READY':'OFF'));
 g.className='gauge'+(run?' run':'');
 $('hold').disabled=!st.on||st.call;
 g.style.setProperty('--p',Math.min(100,st.v/MAX*100));
 $('v').textContent=st.v.toFixed(1);
 c.disabled=!st.on;
 c.className=st.call?'run':'';
 c.textContent=st.call?'STOP CYCLE':'CALL CYCLE';
 var vv=st.lo+st.sp/100*(MAX-st.lo);
 if(!drag)$('sl').value=st.sp;
 $('sl').disabled=!st.on;
 $('sp').textContent=st.sp+'% ('+vv.toFixed(2)+'V)';
 $('lo').textContent='Start '+st.lo.toFixed(1)+'V';
 $('hi').textContent='Max '+MAX.toFixed(1)+'V';
 $('warn').style.display=lost>3?'block':'none';
}
function api(u){fetch(u).then(function(r){return r.json()}).then(function(s){st=s;lost=0;render()}).catch(function(){lost++;render()})}
function setSpeed(x){var n=Date.now();if(n-last>80){last=n;api('/speed?p='+x)}}
function holdOn(e){e.preventDefault();if(!st.on||st.call||ht)return;try{e.target.setPointerCapture(e.pointerId)}catch(x){}ht=1;$('hold').classList.add('down');holdTick()}
function holdTick(){if(!ht)return;fetch('/hold?on=1').then(function(r){return r.json()}).then(function(s){st=s;lost=0;render()}).catch(function(){lost++}).then(function(){if(ht)ht=setTimeout(holdTick,100)})}
function holdOff(){if(!ht)return;clearTimeout(ht);ht=null;$('hold').classList.remove('down');api('/hold?on=0')}
window.addEventListener('blur',holdOff);document.addEventListener('visibilitychange',holdOff);
function callCycle(){api('/call?on='+(st.call?0:1))}
setInterval(function(){if(!ht)api('/status')},300);api('/status');
</script></body></html>)rawliteral";

void sendStatus() {
  String j = String("{\"on\":") + (systemOn ? "true" : "false") +
             ",\"call\":" + (calling ? "true" : "false") +
             ",\"hold\":" + (holding ? "true" : "false") +
             ",\"v\":" + String(current, 2) +
             ",\"sp\":" + String(speedPct) +
             ",\"lo\":" + String(CALL_VOLTAGE, 2) + "}";
  server.send(200, "application/json", j);
}

void handleRoot() {
  String p = FPSTR(PAGE_HEAD);
  p += String(THROTTLE_MAX, 2);
  p += FPSTR(PAGE_TAIL);
  server.send(200, "text/html", p);
}

void handleStart() {
  systemOn = true;
  lastBeat = millis();
  digitalWrite(PIN_STBY, HIGH);
  sendStatus();
}

void handleStop() {
  systemOn = false;
  calling = false;
  holding = false;
  speedPct = 0;
  writeVoltage(0);
  digitalWrite(PIN_STBY, LOW);
  sendStatus();
}

void handleCall() {
  if (systemOn && server.hasArg("on")) {
    calling = server.arg("on") == "1";
    if (!calling) speedPct = 0;
    lastBeat = millis();
  }
  sendStatus();
}

void handleHold() {
  if (systemOn && server.hasArg("on")) {
    if (server.arg("on") == "1") {
      holding = true;
      lastHold = millis();
      if (!calling) writeVoltage(CALL_VOLTAGE);   // dabate hi seedha 1.8V
    } else if (holding) {
      holding = false;
      if (!calling) writeVoltage(0);   // button chhodte hi turant ruk jao
    }
    lastBeat = millis();
  }
  sendStatus();
}

void handleSpeed() {
  if (systemOn && server.hasArg("p")) {
    speedPct = constrain(server.arg("p").toInt(), 0, 100);
    lastBeat = millis();
  }
  sendStatus();
}

void handleStatus() {
  if (systemOn) lastBeat = millis();   // page ka poll = heartbeat
  sendStatus();
}

void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_STBY, OUTPUT); digitalWrite(PIN_STBY, LOW);   // driver band
  pinMode(PIN_AIN1, OUTPUT); digitalWrite(PIN_AIN1, HIGH);
  pinMode(PIN_AIN2, OUTPUT); digitalWrite(PIN_AIN2, LOW);
  ledcAttach(PIN_PWMA, PWM_FREQ, PWM_RES);
  writeVoltage(0);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP: "); Serial.print(AP_SSID);
  Serial.print("  http://"); Serial.println(WiFi.softAPIP());

  dns.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/call", handleCall);
  server.on("/speed", handleSpeed);
  server.on("/hold", handleHold);
  server.on("/status", handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();
  lastTick = millis();
}

void loop() {
  dns.processNextRequest();
  server.handleClient();

  unsigned long now = millis();
  float dt = (now - lastTick) / 1000.0;
  lastTick = now;

  if (systemOn) {
    if (now - lastBeat > DEADMAN_MS) { calling = false; holding = false; speedPct = 0; }   // phone/WiFi gaya -> throttle 0
    if (holding && now - lastHold > HOLD_TIMEOUT_MS) {   // button ka signal band -> turant ruk jao
      holding = false;
      if (!calling) writeVoltage(0);
    }

    float target = targetVolt();
    float step = ((holding && !calling) ? HOLD_RAMP_V_PER_SEC : RAMP_V_PER_SEC) * dt;
    if (target > current)      writeVoltage(min(current + step, target));
    else if (target < current) writeVoltage(max(current - 4 * step, target));  // utarna tez
  }
}
