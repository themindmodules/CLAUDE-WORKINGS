// Bicycle Control - ESP32 WiFi Access Point + Web Control Panel
// Board: ESP32 (classic, has real DAC on GPIO25 / GPIO26)
//
// WiFi name : "bicycle control"  (open network, no password)
// Page      : http://192.168.4.1  (phone connect hote hi captive portal se khud khul jayega)
//
// Features:
//   START / STOP : START = console on, STOP = soft shutdown (output 0V, controls lock)
//   MOVE button  : analog pin par 1V output
//   Slider       : analog voltage 0V - 3V
//
// Note: "Shutdown" soft hai. Deep sleep me jaate hi WiFi band ho jata aur START
// button wali page dobara khul hi nahi paati, isliye WiFi chalu rehta hai aur
// sirf outputs/console band hote hain. START dabane par sab wapas on.

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

const char* AP_SSID = "bicycle control";   // open network
const int   DAC_PIN = 25;                  // analog output pin (GPIO25 = DAC1, GPIO26 = DAC2)
const float VREF    = 3.3;                 // ESP32 DAC full scale

WebServer server(80);
DNSServer dns;
const IPAddress apIP(192, 168, 4, 1);

bool  systemOn = false;
float volts    = 0.0;

void setVoltage(float v) {
  if (v < 0) v = 0;
  if (v > 3.0) v = 3.0;
  volts = v;
  dacWrite(DAC_PIN, (uint8_t)(v / VREF * 255.0 + 0.5));
}

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bicycle Control</title>
<style>
 body{font-family:sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:20px}
 h1{margin:8px 0 20px}
 button{font-size:20px;padding:14px 28px;margin:8px;border:0;border-radius:10px;color:#fff}
 #start{background:#2e9e4f} #stop{background:#c0392b} #move{background:#2471a3;width:80%}
 #console{margin-top:20px;padding:16px;border-radius:12px;background:#222;opacity:.35;pointer-events:none}
 #console.on{opacity:1;pointer-events:auto}
 input[type=range]{width:90%;height:40px}
 #state{font-weight:bold}
</style></head><body>
<h1>Bicycle Control</h1>
<div>Status: <span id="state">OFF</span></div>
<button id="start" onclick="cmd('/start')">START</button>
<button id="stop"  onclick="cmd('/stop')">STOP</button>
<div id="console">
  <button id="move" onclick="moveBtn()">MOVE (1V)</button>
  <p>Voltage: <span id="v">0.00</span> V</p>
  <input id="sl" type="range" min="0" max="3" step="0.05" value="0" oninput="slide(this.value)">
</div>
<script>
function ui(s){
  document.getElementById('state').textContent = s.on ? 'ON' : 'OFF';
  document.getElementById('console').classList.toggle('on', s.on);
  document.getElementById('v').textContent = s.v.toFixed(2);
  document.getElementById('sl').value = s.v;
}
function cmd(u){ fetch(u).then(r=>r.json()).then(ui); }
function moveBtn(){ cmd('/move'); }
var t;
function slide(x){
  document.getElementById('v').textContent = parseFloat(x).toFixed(2);
  clearTimeout(t); t = setTimeout(()=>cmd('/volt?v='+x), 40);
}
cmd('/status');
</script></body></html>
)rawliteral";

void sendStatus() {
  String j = String("{\"on\":") + (systemOn ? "true" : "false") + ",\"v\":" + String(volts, 2) + "}";
  server.send(200, "application/json", j);
}

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleStart() { systemOn = true; sendStatus(); }

void handleStop() {
  systemOn = false;
  setVoltage(0);
  sendStatus();
}

void handleMove() {
  if (systemOn) setVoltage(1.0);   // 1V on analog pin
  sendStatus();
}

void handleVolt() {
  if (systemOn && server.hasArg("v")) setVoltage(server.arg("v").toFloat());
  sendStatus();
}

// Phone/laptop ke captive-portal checks ko page par redirect kar do
void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  setVoltage(0);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID);            // no password
  Serial.print("AP started: "); Serial.println(AP_SSID);
  Serial.print("Open: http://"); Serial.println(WiFi.softAPIP());

  dns.start(53, "*", apIP);        // har domain ko ESP ke IP par bhejo (auto-open page)

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/move", handleMove);
  server.on("/volt", handleVolt);
  server.on("/status", sendStatus);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
}
