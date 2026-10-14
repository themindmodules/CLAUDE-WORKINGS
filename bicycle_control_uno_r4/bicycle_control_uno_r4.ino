// Bicycle Control - Arduino UNO R4 WiFi: WiFi Access Point + Web Control Panel
// E-bike controller ko throttle signal dene ke liye.
// Board: "Arduino UNO R4 WiFi" (R4 Minima me WiFi nahi hota, ye code uspe nahi chalega).
// Library: WiFiS3 (board package "Arduino UNO R4 Boards" ke saath aati hai).
//
// WiFi : "bicycle control" (WPA2, password AP_PASS)
// Page : http://192.168.4.1  (R4 par captive portal nahi hai, browser me ye address khud kholo)
//
// Buttons: START      = console + TB6612 on
//          STOP       = cycle band, throttle 0, driver standby
//          CALL CYCLE = dabate hi throttle CALL_VOLTAGE (1.8V) par chala deta hai
//                       (cycle chalne ki threshold). Dobara dabane par ruk jata hai.
//          SPEED slider = 0% -> 1.8V (threshold), 100% -> THROTTLE_MAX (3.4V),
//                       cycle chalte waqt live badalta hai
// Safety : page ka status-poll 1s band -> throttle 0 | throttle ramp | boot par STBY LOW
//
// Output chain: D9 PWM (20 kHz) -> TB6612 PWMA -> AO1 (VM ka PWM)
//               -> inductor 100-220uH -> 220uF + 100nF -> throttle signal wire
//   Average voltage = duty x VM.
//
// Wiring (R4 ka logic 5V hai, isliye TB6612 VCC = 5V):
//   TB6612 VM   -> 5V supply (+)            TB6612 VCC  -> UNO 5V
//   TB6612 GND  -> UNO GND + supply (-) + controller GND (common)
//   TB6612 PWMA -> D9                       TB6612 AIN1 -> D7 (HIGH)
//   TB6612 AIN2 -> D8 (LOW)                 TB6612 STBY -> D4 (+ 10k to GND, optional)
//   TB6612 AO1  -> inductor -> signal wire (cap 220uF+100nF to GND)
//   TB6612 AO2  -> khali
//
// Pehle bina controller ke multimeter/resistor par test karo.

#include <WiFiS3.h>
#include "pwm.h"

const char* AP_SSID = "bicycle control";
const char* AP_PASS = "bicycle123";        // kam se kam 8 character, badal lena

const int PIN_AIN1 = 7;   // HIGH
const int PIN_AIN2 = 8;   // LOW
const int PIN_STBY = 4;   // HIGH = driver on
PwmOut pwm(9);            // D9 -> PWMA

const float VM             = 5.0;   // TB6612 VM supply (volt)
const float THROTTLE_MAX   = 3.4;   // controller ka max throttle voltage
const float CALL_VOLTAGE   = 1.8;   // CALL CYCLE dabane par ye voltage (cycle chalne ki threshold)
const float RAMP_V_PER_SEC = 1.5;   // throttle kitni tezi se badhe
const unsigned long DEADMAN_MS = 1000;
const unsigned long HOLD_TIMEOUT_MS = 400;   // HOLD button ka signal itni der na aaye to throttle 0
const float HOLD_RAMP_V_PER_SEC = 6.0;       // HOLD dabane par throttle jaldi 1.8V tak pahunche
const float PWM_FREQ_HZ = 20000.0;

WiFiServer server(80);

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
  pwm.pulse_perc(v / VM * 100.0);
}


// cycle chal rahi ho to: 1.8V (speed 0%) se THROTTLE_MAX (speed 100%) ke beech
float targetVolt() {
  if (!calling) return 0.0;
  return CALL_VOLTAGE + speedPct / 100.0 * (THROTTLE_MAX - CALL_VOLTAGE);
}

const char PAGE1[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8">
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
#hold.down{background:var(--g);transform:scale(.98)}
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
<button id="hold" disabled onpointerdown="holdOn(event)" onpointerup="holdOff()" onpointercancel="holdOff()" onpointerleave="holdOff()" oncontextmenu="return false">HOLD TO RUN</button>
<div class="hint">Dabaye rakho = threshold par chalegi, chhodte hi ruk jayegi</div>
</div>
<div class="card">
<div class="lab"><span>SPEED CONTROL</span><b id="sp">0% (1.80V)</b></div>
<input id="sl" type="range" min="0" max="100" step="1" value="0" disabled oninput="setSpeed(this.value)" onpointerdown="drag=1" onpointerup="drag=0" onchange="drag=0;api('/speed?p='+this.value)">
<div class="lab"><span id="lo">Start 1.8V</span><span id="hi">Max 3.4V</span></div>
</div></div>
<script>
var MAX=)rawliteral";
const char PAGE2[] PROGMEM = R"rawliteral(,st={on:false,call:false,hold:false,v:0,sp:0,lo:1.8},lost=0,drag=0,last=0,ht=null;
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
function holdOn(e){e.preventDefault();if(!st.on||st.call||ht)return;api('/hold?on=1');ht=setInterval(function(){api('/hold?on=1')},120);$('hold').classList.add('down')}
function holdOff(){if(!ht)return;clearInterval(ht);ht=null;$('hold').classList.remove('down');api('/hold?on=0')}
window.addEventListener('blur',holdOff);document.addEventListener('visibilitychange',holdOff);
function callCycle(){api('/call?on='+(st.call?0:1))}
setInterval(function(){api('/status')},300);api('/status');
</script></body></html>)rawliteral";

void sendJson(WiFiClient& c) {
  String j = String("{\"on\":") + (systemOn ? "true" : "false") +
             ",\"call\":" + (calling ? "true" : "false") +
             ",\"hold\":" + (holding ? "true" : "false") +
             ",\"v\":" + String(current, 2) +
             ",\"sp\":" + String(speedPct) +
             ",\"lo\":" + String(CALL_VOLTAGE, 2) + "}";
  c.print(F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n"));
  c.print(j);
}

void sendPage(WiFiClient& c) {
  c.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"));
  c.print(PAGE1);
  c.print(THROTTLE_MAX, 2);
  c.print(PAGE2);
}

void handleClient(WiFiClient& c) {
  // sirf pehli request line chahiye: "GET /path HTTP/1.1"
  String line = "";
  unsigned long t0 = millis();
  while (c.connected() && millis() - t0 < 200) {
    if (c.available()) {
      char ch = c.read();
      if (ch == '\n') break;
      if (ch != '\r' && line.length() < 120) line += ch;
    }
  }
  // baaki headers padh kar phenk do
  t0 = millis();
  while (c.available() && millis() - t0 < 50) c.read();

  if (line.startsWith("GET /start")) {
    systemOn = true;
    lastBeat = millis();
    digitalWrite(PIN_STBY, HIGH);
    sendJson(c);
  } else if (line.startsWith("GET /stop")) {
    systemOn = false;
    calling = false;
    holding = false;
    speedPct = 0;
    writeVoltage(0);
    digitalWrite(PIN_STBY, LOW);
    sendJson(c);
  } else if (line.startsWith("GET /call")) {
    if (systemOn) {
      calling = line.indexOf("on=1") >= 0;
      if (!calling) speedPct = 0;
      lastBeat = millis();
    }
    sendJson(c);
  } else if (line.startsWith("GET /hold")) {
    if (systemOn) {
      if (line.indexOf("on=1") >= 0) {
        holding = true;
        lastHold = millis();
      } else if (holding) {
        holding = false;
        if (!calling) writeVoltage(0);   // button chhodte hi turant ruk jao
      }
      lastBeat = millis();
    }
    sendJson(c);
  } else if (line.startsWith("GET /speed")) {
    int i = line.indexOf("p=");
    if (systemOn && i >= 0) {
      speedPct = constrain(line.substring(i + 2).toInt(), 0, 100);
      lastBeat = millis();
    }
    sendJson(c);
  } else if (line.startsWith("GET /status")) {
    if (systemOn) lastBeat = millis();   // page ka poll = heartbeat
    sendJson(c);
  } else if (line.startsWith("GET / ")) {
    sendPage(c);
  } else {
    c.print(F("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"));
  }
  delay(1);
  c.stop();
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_STBY, OUTPUT); digitalWrite(PIN_STBY, LOW);   // driver band
  pinMode(PIN_AIN1, OUTPUT); digitalWrite(PIN_AIN1, HIGH);
  pinMode(PIN_AIN2, OUTPUT); digitalWrite(PIN_AIN2, LOW);
  pwm.begin(PWM_FREQ_HZ, 0.0f);
  writeVoltage(0);

  WiFi.config(IPAddress(192, 168, 4, 1));
  int st = WiFi.beginAP(AP_SSID, AP_PASS);
  if (st != WL_AP_LISTENING) {
    Serial.println("AP start fail");
    while (true) { writeVoltage(0); delay(1000); }   // throttle 0 par ruk jao
  }
  Serial.print("AP: "); Serial.print(AP_SSID);
  Serial.print("  http://"); Serial.println(WiFi.localIP());

  server.begin();
  lastTick = millis();
}

void loop() {
  WiFiClient client = server.available();
  if (client) handleClient(client);

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
