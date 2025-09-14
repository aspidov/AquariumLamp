#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "secrets.h"

// ------------------- PINOUT & COUNTS -------------------
#define DIM_STRIP_PIN 4   // regular dimmable LED strip (MOSFET -> low-side)
#define WS1_PIN 17        // first WS2812 strip data
#define WS2_PIN 18        // second WS2812 strip data

#define WS1_COUNT 15
#define WS2_COUNT 15

// ------------------- LED OBJECTS -------------------
Adafruit_NeoPixel strip1(WS1_COUNT, WS1_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(WS2_COUNT, WS2_PIN, NEO_GRB + NEO_KHZ800);

// ------------------- HOST / NETWORK -------------------
static const char* HOSTNAME = "aquarium-lamp";  // visible as aquarium-lamp.local

// ------------------- PWM (LEDC) CONFIG -------------------
static const int DIM_CH     = 0;     // LEDC channel for PWM
static const int DIM_FREQ   = 5000;  // 5 kHz is fine for LED dimming
static const int DIM_RES    = 8;     // 8-bit (0..255 duty)

// ------------------- SERVER -------------------
AsyncWebServer server(80);

// ------------------- STATE -------------------
struct StripState {
  uint8_t brightness;     // 0..255 (for both types)
  uint8_t r, g, b;        // solid color for addressable strips
  bool on;
};

StripState dimState   {255, 255, 255, 255, true}; // brightness used as PWM duty; rgb unused
StripState ws1State   {128, 255, 255, 255, true};
StripState ws2State   {128, 255, 255, 255, true};

// Dirty flags to trigger non-blocking updates from loop()
volatile bool ws1Dirty = false;
volatile bool ws2Dirty = false;

// ------------------- HELPERS -------------------
void applyDimPwm()
{
  uint8_t duty = dimState.on ? dimState.brightness : 0;
  ledcWrite(DIM_CH, duty);
}

void applyStripSolid(Adafruit_NeoPixel& strip, const StripState& st)
{
  // NeoPixel setBrightness scales colors internally (0..255)
  strip.setBrightness(st.on ? st.brightness : 0);
  uint32_t c = strip.Color(st.r, st.g, st.b);
  strip.fill(c, 0, strip.numPixels());
  strip.show(); // Blocking but very short; we’re not animating
}

void markDirty()
{
  ws1Dirty = true;
  ws2Dirty = true;
}

void markDirty(volatile bool &flag)
{
  flag = true;
}

String htmlIndex()
{
  // minimal UI with buttons and sliders
  return R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <title>Aquarium Lamp</title>
  <style>
    body { font-family: system-ui, sans-serif; max-width: 720px; margin: 2rem auto; padding: 0 1rem; }
    h1 { font-size: 1.2rem; }
    .card { border: 1px solid #ccc; border-radius: 12px; padding: 1rem; margin-bottom: 1rem; }
    .row { display: flex; gap: 0.5rem; align-items: center; flex-wrap: wrap; }
    button { padding: 0.5rem 0.8rem; border-radius: 8px; border: 1px solid #aaa; cursor: pointer; }
    input[type=range] { width: 200px; }
    .small { font-size: 0.9rem; color: #444; }
  </style>
</head>
<body>
  <h1>Aquarium Lamp</h1>

  <div class="card">
    <h2>Regular Strip (PWM @ GPIO 4)</h2>
    <div class="row">
      <button onclick="fetch('/api/dim/on')">On</button>
      <button onclick="fetch('/api/dim/off')">Off</button>
      <label>Brightness <input id="dimB" type="range" min="0" max="255" value="255" oninput="setDim()"></label>
    </div>
    <div class="small">MOSFET low-side, LEDC PWM.</div>
  </div>

  <div class="card">
    <h2>WS2812 Strip #1 (GPIO 17, 15 LEDs)</h2>
    <div class="row">
      <button onclick="fetch('/api/ws1/on')">On</button>
      <button onclick="fetch('/api/ws1/off')">Off</button>
      <label>Brightness <input id="ws1B" type="range" min="0" max="255" value="128" oninput="setWS1()"></label>
    </div>
    <div class="row">
      <label>Color <input id="ws1C" type="color" value="#ffffff" oninput="setWS1()"></label>
    </div>
  </div>

  <div class="card">
    <h2>WS2812 Strip #2 (GPIO 18, 15 LEDs)</h2>
    <div class="row">
      <button onclick="fetch('/api/ws2/on')">On</button>
      <button onclick="fetch('/api/ws2/off')">Off</button>
      <label>Brightness <input id="ws2B" type="range" min="0" max="255" value="128" oninput="setWS2()"></label>
    </div>
    <div class="row">
      <label>Color <input id="ws2C" type="color" value="#ffffff" oninput="setWS2()"></label>
    </div>
  </div>

  <div class="card">
    <div class="row">
      <button onclick="fetch('/api/onall')">All On</button>
      <button onclick="fetch('/api/offall')">All Off</button>
    </div>
  </div>

<script>
function hexToRgb(hex) {
  const m = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
  return m ? { r: parseInt(m[1],16), g: parseInt(m[2],16), b: parseInt(m[3],16) } : {r:255,g:255,b:255};
}
function setDim(){
  const b = document.getElementById('dimB').value;
  fetch('/api/dim/brightness?b=' + b);
}
function setWS1(){
  const b = document.getElementById('ws1B').value;
  const c = hexToRgb(document.getElementById('ws1C').value);
  fetch(`/api/ws1/set?b=${b}&r=${c.r}&g=${c.g}&b2=${c.b}`);
}
function setWS2(){
  const b = document.getElementById('ws2B').value;
  const c = hexToRgb(document.getElementById('ws2C').value);
  fetch(`/api/ws2/set?b=${b}&r=${c.r}&g=${c.g}&b2=${c.b}`);
}
</script>
</body>
</html>
)HTML";
}

// ------------------- API HANDLERS -------------------
void handleOkJson(AsyncWebServerRequest* request, const String& body)
{
  request->send(200, "application/json", body);
}

uint8_t getQueryU8(AsyncWebServerRequest* req, const char* name, uint8_t def)
{
  if (req->hasParam(name)) {
    int v = req->getParam(name)->value().toInt();
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
  }
  return def;
}

void registerRoutes()
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* res = req->beginResponse(200, "text/html", htmlIndex());
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  // -------- DIM STRIP (PWM) --------
  server.on("/api/dim/on", HTTP_GET, [](AsyncWebServerRequest* req){
    dimState.on = true;
    applyDimPwm();
    handleOkJson(req, "{\"ok\":true}");
  });
  server.on("/api/dim/off", HTTP_GET, [](AsyncWebServerRequest* req){
    dimState.on = false;
    applyDimPwm();
    handleOkJson(req, "{\"ok\":true}");
  });
  server.on("/api/dim/brightness", HTTP_GET, [](AsyncWebServerRequest* req){
    uint8_t b = getQueryU8(req, "b", dimState.brightness);
    dimState.brightness = b;
    applyDimPwm();
    handleOkJson(req, String("{\"ok\":true,\"brightness\":") + b + "}");
  });

  // -------- WS1 --------
  server.on("/api/ws1/on", HTTP_GET, [](AsyncWebServerRequest* req){
    ws1State.on = true; markDirty(ws1Dirty);
    handleOkJson(req, "{\"ok\":true}");
  });
  server.on("/api/ws1/off", HTTP_GET, [](AsyncWebServerRequest* req){
    ws1State.on = false; markDirty(ws1Dirty);
    handleOkJson(req, "{\"ok\":true}");
  });
  server.on("/api/ws1/set", HTTP_GET, [](AsyncWebServerRequest* req){
    ws1State.brightness = getQueryU8(req, "b", ws1State.brightness);
    ws1State.r = getQueryU8(req, "r", ws1State.r);
    ws1State.g = getQueryU8(req, "g", ws1State.g);
    ws1State.b = getQueryU8(req, "b2", ws1State.b);
    markDirty(ws1Dirty);
    handleOkJson(req, "{\"ok\":true}");
  });

  // -------- WS2 --------
  server.on("/api/ws2/on", HTTP_GET, [](AsyncWebServerRequest* req){
    ws2State.on = true; markDirty(ws2Dirty);
    handleOkJson(req, "{\"ok\":true}");
  });
  server.on("/api/ws2/off", HTTP_GET, [](AsyncWebServerRequest* req){
    ws2State.on = false; markDirty(ws2Dirty);
    handleOkJson(req, "{\"ok\":true}");
  });
  server.on("/api/ws2/set", HTTP_GET, [](AsyncWebServerRequest* req){
    ws2State.brightness = getQueryU8(req, "b", ws2State.brightness);
    ws2State.r = getQueryU8(req, "r", ws2State.r);
    ws2State.g = getQueryU8(req, "g", ws2State.g);
    ws2State.b = getQueryU8(req, "b2", ws2State.b);
    markDirty(ws2Dirty);
    handleOkJson(req, "{\"ok\":true}");
  });

  // -------- ALL --------
  server.on("/api/onall", HTTP_GET, [](AsyncWebServerRequest* req){
    dimState.on = ws1State.on = ws2State.on = true;
    applyDimPwm();
    markDirty();
    handleOkJson(req, "{\"ok\":true}");
  });
  server.on("/api/offall", HTTP_GET, [](AsyncWebServerRequest* req){
    dimState.on = ws1State.on = ws2State.on = false;
    applyDimPwm();
    markDirty();
    handleOkJson(req, "{\"ok\":true}");
  });
}

// ------------------- WIFI / OTA -------------------
void setupWifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - start > 20000) { // 20s timeout -> continue anyway
      break;
    }
  }
  Serial.println();
  Serial.printf("WiFi status: %d, IP: %s\n", WiFi.status(), WiFi.localIP().toString().c_str());

  if (!MDNS.begin(HOSTNAME)) {
    Serial.println("Error starting mDNS");
  } else {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("arduino", "tcp", 3232); // OTA
  }

  ArduinoOTA.setHostname(HOSTNAME);
  // Optional password:
  // ArduinoOTA.setPassword("CHANGE_ME");
  ArduinoOTA
    .onStart([](){ Serial.println("OTA Start"); })
    .onEnd([](){ Serial.println("\nOTA End"); })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("OTA Error[%u]\n", error);
    });
  ArduinoOTA.begin();
}

// ------------------- SETUP/LOOP -------------------
void setup()
{
  Serial.begin(115200);
  delay(200);

  // PWM for dimmable strip
  ledcSetup(DIM_CH, DIM_FREQ, DIM_RES);
  ledcAttachPin(DIM_STRIP_PIN, DIM_CH);
  applyDimPwm();

  // Addressable strips
  strip1.begin();
  strip2.begin();
  strip1.clear(); strip2.clear();
  strip1.setBrightness(ws1State.brightness);
  strip2.setBrightness(ws2State.brightness);
  // default to white
  strip1.fill(strip1.Color(ws1State.r, ws1State.g, ws1State.b));
  strip2.fill(strip2.Color(ws2State.r, ws2State.g, ws2State.b));
  strip1.show();
  strip2.show();

  setupWifi();

  registerRoutes();
  server.begin();

  Serial.printf("HTTP server started at http://%s.local/ or http://%s/\n",
                HOSTNAME, WiFi.localIP().toString().c_str());
}

void loop()
{
  // OTA runs here, non-blocking
  ArduinoOTA.handle();

  // If a strip is marked dirty, push a refresh (solid color only)
  if (ws1Dirty) {
    ws1Dirty = false;
    applyStripSolid(strip1, ws1State);
  }
  if (ws2Dirty) {
    ws2Dirty = false;
    applyStripSolid(strip2, ws2State);
  }

  // You can add lightweight periodic tasks here (no delay(…); use vTaskDelay if needed)
  // vTaskDelay(1); // optional yield
}
