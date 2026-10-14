// Bicycle Control - Arduino UNO R4 WiFi: WiFi Access Point + Web Control Panel
// E-bike controller ko 0..3.4V throttle signal dene ke liye.
// Board: "Arduino UNO R4 WiFi" (R4 Minima me WiFi nahi hota, ye code uspe nahi chalega).
// Library: WiFiS3 (board package "Arduino UNO R4 Boards" ke saath aati hai).
//
// WiFi : "bicycle control" (WPA2, password AP_PASS)
// Page : http://192.168.4.1  (R4 par captive portal nahi hai, browser me ye address khud kholo)
//
// Buttons: START = console + TB6612 on | STOP = throttle 0 + driver standby
//          MOVE = 1V | Slider = 0 .. THROTTLE_MAX volt
// Safety : heartbeat 1s band -> throttle 0 | throttle ramp | boot par STBY LOW
//
// Output chain: D9 PWM (20 kHz) -> TB6612 PWMA -> AO1 (VM ka PWM)
//               -> inductor 100-220uH -> 220uF + 100nF -> throttle signal wire
//   Average voltage = duty x VM.
//
// Wiring (R4 ka logic 5V hai, isliye TB6612 VCC = 5V):
//   TB6612 VM   -> 5V supply (+)            TB6612 VCC  -> UNO 5V
//   TB6612 GND  -> UNO GND + supply (-) + controller GND (common)
//   TB6612 PWMA -> D9                       TB6612 AIN1 -> D7 (HIGH)
//   TB6612 AIN2 -> D8 (LOW)                 TB6612 STBY -> D4 + 10k to GND
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
const int PIN_STBY = 4;   // HIGH = driver on (10k pull-down laga ke rakho)
PwmOut pwm(9);            // D9 -> PWMA

const float VM             = 5.0;   // TB6612 VM supply (volt)
const float THROTTLE_MAX   = 3.4;   // controller ka max throttle voltage
const float RAMP_V_PER_SEC = 1.5;   // throttle kitni tezi se badhe
const unsigned long DEADMAN_MS = 1000;
const float PWM_FREQ_HZ = 20000.0;

WiFiServer server(80);

bool  systemOn = false;
float target   = 0.0;   // slider se mila voltage
float current  = 0.0;   // abhi actual output
unsigned long lastBeat = 0, lastTick = 0;

void writeVoltage(float v) {
  if (v < 0) v = 0;
  if (v > THROTTLE_MAX) v = THROTTLE_MAX;
  current = v;
  pwm.pulse_perc(v / VM * 100.0);
}

void allOff() {
  target = 0;
  writeVoltage(0);
}

const char PAGE1[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
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
  <input id="sl" type="range" min="0" max=")rawliteral";

const char PAGE2[] PROGMEM = R"rawliteral(" step="0.05" value="0" oninput="setV(this.value)">
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
// heartbeat: ye band hote hi board throttle 0 kar deta hai
setInterval(()=>{ if(on) cmd('/volt?v='+want); else cmd('/status'); }, 300);
cmd('/status');
</script></body></html>)rawliteral";

void sendJson(WiFiClient& c) {
  String j = String("{\"on\":") + (systemOn ? "true" : "false") + ",\"v\":" + String(current, 2) + "}";
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
    allOff();
    digitalWrite(PIN_STBY, LOW);
    sendJson(c);
  } else if (line.startsWith("GET /volt")) {
    int i = line.indexOf("v=");
    if (systemOn && i >= 0) {
      float v = line.substring(i + 2).toFloat();
      if (v < 0) v = 0;
      if (v > THROTTLE_MAX) v = THROTTLE_MAX;
      target = v;
      lastBeat = millis();
    }
    sendJson(c);
  } else if (line.startsWith("GET /status")) {
    sendJson(c);
  } else {
    sendPage(c);
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
  allOff();

  WiFi.config(IPAddress(192, 168, 4, 1));
  int st = WiFi.beginAP(AP_SSID, AP_PASS);
  if (st != WL_AP_LISTENING) {
    Serial.println("AP start fail");
    while (true) { allOff(); delay(1000); }   // throttle 0 par ruk jao
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
    if (now - lastBeat > DEADMAN_MS) target = 0;   // phone/WiFi gaya -> throttle 0

    float step = RAMP_V_PER_SEC * dt;
    if (target > current)      writeVoltage(min(current + step, target));
    else if (target < current) writeVoltage(max(current - 4 * step, target));  // utarna tez
  }
}
