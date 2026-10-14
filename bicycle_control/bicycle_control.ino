// Bicycle Control - ESP32-C6 WiFi Access Point + Web Control Panel
// Output: TB6612FNG PWM -> inductor + capacitor -> 0..3.4V throttle signal
// Arduino-ESP32 core 3.x, board "ESP32C6 Dev Module".
// Wiring ke liye README.md dekho.
//
// WiFi : "bicycle control" (WPA2, password neeche AP_PASS)
// Page : http://192.168.4.1 (connect karte hi khud khulta hai)
//
//   START : console on + TB6612 chalu        STOP : throttle 0 + TB6612 standby
//   MOVE  : 1V                               Slider : 0 .. THROTTLE_MAX volt
//   Safety: page ka heartbeat 1s band hua to throttle 0 ho jata hai
//           throttle dheere (RAMP_V_PER_SEC) badhta hai

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

const char* AP_SSID = "bicycle control";
const char* AP_PASS = "bicycle123";        // kam se kam 8 character, badal lena

// TB6612 pins (strapping pins avoid kiye hain)
const int PIN_PWMA = 0;   // PWM -> PWMA
const int PIN_AIN1 = 1;   // HIGH
const int PIN_AIN2 = 2;   // LOW
const int PIN_STBY = 3;   // HIGH = driver on (10k pull-down laga ke rakho)

const float VM            = 5.0;   // TB6612 VM supply (volt)
const float THROTTLE_MAX  = 3.4;   // controller ka max throttle voltage
const float RAMP_V_PER_SEC = 1.5;  // throttle kitni tezi se badhe
const unsigned long DEADMAN_MS = 1000;

const int PWM_RES  = 10;
const int PWM_FREQ = 20000;

WebServer server(80);
DNSServer dns;
const IPAddress apIP(192, 168, 4, 1);

bool  systemOn = false;
float target   = 0.0;   // slider se mila voltage
float current  = 0.0;   // abhi actual output
unsigned long lastBeat = 0, lastTick = 0;

void writeVoltage(float v) {
  if (v < 0) v = 0;
  if (v > THROTTLE_MAX) v = THROTTLE_MAX;
  current = v;
  ledcWrite(PIN_PWMA, (uint32_t)(v / VM * ((1 << PWM_RES) - 1) + 0.5));
}

void allOff() {
  target = 0;
  writeVoltage(0);
}

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bicycle Control</title>
<style>
 body{font-family:sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:20px}
 button{font-size:20px;padding:14px 28px;margin:8px;border:0;border-radius:10px;color:#fff}
 #start{background:#2e9e4f} #stop{background:#c0392b} #move{background:#2471a3;width:80%}
 #console{margin-top:20px;padding:16px;border-radius:12px;background:#222;opacity:.35;pointer-events:none}
 #console.on{opacity:1;pointer-events:auto}
 input[type=range]{width:90%;height:40px}
</style></head><body>
<h1>Bicycle Control</h1>
<div>Status: <b id="state">OFF</b></div>
<button id="start" onclick="cmd('/start')">START</button>
<button id="stop"  onclick="cmd('/stop')">STOP</button>
<div id="console">
  <button id="move" onclick="setV(1.0)">MOVE (1V)</button>
  <p>Throttle: <span id="v">0.00</span> V</p>
  <input id="sl" type="range" min="0" max="__MAX__" step="0.05" value="0" oninput="setV(this.value)">
</div>
<script>
var want=0, on=false;
function ui(s){
  on=s.on;
  document.getElementById('state').textContent = s.on ? 'ON' : 'OFF';
  document.getElementById('console').classList.toggle('on', s.on);
  if(!s.on){want=0;document.getElementById('sl').value=0;}
  document.getElementById('v').textContent = want.toFixed(2);
}
function cmd(u){ fetch(u).then(r=>r.json()).then(ui).catch(()=>{}); }
function setV(x){
  want=parseFloat(x);
  document.getElementById('sl').value=want;
  document.getElementById('v').textContent=want.toFixed(2);
  cmd('/volt?v='+want);
}
// heartbeat: ye band hote hi ESP throttle 0 kar deta hai
setInterval(()=>{ if(on) cmd('/volt?v='+want); else cmd('/status'); }, 300);
cmd('/status');
</script></body></html>
)rawliteral";

void sendStatus() {
  String j = String("{\"on\":") + (systemOn ? "true" : "false") + ",\"v\":" + String(current, 2) + "}";
  server.send(200, "application/json", j);
}

void handleRoot() {
  String p = FPSTR(PAGE);
  p.replace("__MAX__", String(THROTTLE_MAX, 2));
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
  allOff();
  digitalWrite(PIN_STBY, LOW);
  sendStatus();
}

void handleVolt() {
  if (systemOn && server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (v < 0) v = 0;
    if (v > THROTTLE_MAX) v = THROTTLE_MAX;
    target = v;
    lastBeat = millis();
  }
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
  allOff();

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP: "); Serial.print(AP_SSID);
  Serial.print("  http://"); Serial.println(WiFi.softAPIP());

  dns.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/volt", handleVolt);
  server.on("/status", sendStatus);
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
    if (now - lastBeat > DEADMAN_MS) target = 0;   // phone/WiFi gaya -> throttle 0

    float step = RAMP_V_PER_SEC * dt;
    if (target > current)      writeVoltage(min(current + step, target));
    else if (target < current) writeVoltage(max(current - 4 * step, target));  // utarna tez
  }
}
