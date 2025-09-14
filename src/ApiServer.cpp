#include "ApiServer.h"
#include <ESPAsyncWebServer.h>
#include "LEDController.h"
#include "TimeService.h"
#include "Scheduler.h"

namespace ApiServer {

static AsyncWebServer* s_server = nullptr;

void init(AsyncWebServer& server)
{
  s_server = &server;
}

static String htmlIndex()
{
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
  <button onclick="startAnim('sunrise')">Sunrise</button>
  <button onclick="startAnim('sunset')">Sunset</button>
  <button onclick="startAnim('waves')">Waves</button>
  <button onclick="startTest('sunrise')">Test Sunrise (1m)</button>
  <button onclick="startTest('sunset')">Test Sunset (1m)</button>
  <button onclick="fetch('/api/anim/stop')">Stop Anim</button>
    </div>
  </div>

  <div class="card">
    <h2>Status</h2>
  <div id="status">Loading...</div>
  <div class="small">Time: <span id="time">--</span></div>
  <div class="small">Schedule:</div>
  <div id="schedule" class="small">(loading)</div>
  </div>

<script>
function hexToRgb(hex) {
  const m = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
  return m ? { r: parseInt(m[1],16), g: parseInt(m[2],16), b: parseInt(m[3],16) } : {r:255,g:255,b:255};
}
function rgbToHex(r, g, b) {
  return "#" + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1);
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
function startAnim(name){
  // duration optional in ms; default server side
  fetch(`/api/anim/start?name=${name}`);
}
function startTest(name){
  // start a 60s test animation for quick verification
  fetch(`/api/anim/start?name=${name}&dur=60000`);
}

function updateStatus() {
  fetch('/api/state')
    .then(res => res.json())
    .then(state => {
  document.getElementById('time').textContent = state.time || '--';
      document.getElementById('dimB').value = state.dim.brightness;
      document.getElementById('ws1B').value = state.ws1.brightness;
      document.getElementById('ws1C').value = rgbToHex(state.ws1.r, state.ws1.g, state.ws1.b);
      document.getElementById('ws2B').value = state.ws2.brightness;
      document.getElementById('ws2C').value = rgbToHex(state.ws2.r, state.ws2.g, state.ws2.b);

      let statusHtml = `<strong>Animation:</strong> ${state.animation}<br>`;
      statusHtml += `<strong>Dim Strip:</strong> ${state.dim.on ? 'On' : 'Off'}, Brightness: ${state.dim.brightness}<br>`;
      statusHtml += `<strong>WS1 Strip:</strong> ${state.ws1.on ? 'On' : 'Off'}, Brightness: ${state.ws1.brightness}, Color: ${rgbToHex(state.ws1.r, state.ws1.g, state.ws1.b)}<br>`;
      statusHtml += `<strong>WS2 Strip:</strong> ${state.ws2.on ? 'On' : 'Off'}, Brightness: ${state.ws2.brightness}, Color: ${rgbToHex(state.ws2.r, state.ws2.g, state.ws2.b)}<br>`;
      document.getElementById('status').innerHTML = statusHtml;
      // Render schedule
      const schedEl = document.getElementById('schedule');
      if (state.schedule && Array.isArray(state.schedule)) {
        if (state.schedule.length === 0) {
          schedEl.textContent = '(no entries)';
        } else {
          let out = '';
          state.schedule.forEach(e => {
            const dir = e.isUtc ? 'UTC' : 'local';
            out += `${String(e.hour).padStart(2,'0')}:${String(e.minute).padStart(2,'0')} (${dir}) ${e.anim} for ${Math.round(e.durationMs/60000)}m`;
            if (e.followUp) {
              out += ` → follow:${e.followUp}`;
            }
            out += '<br>';
          });
          schedEl.innerHTML = out;
        }
      } else {
        schedEl.textContent = '(none)';
      }
    });
}

window.onload = () => {
  updateStatus();
  setInterval(updateStatus, 2000);
};
</script>
</body>
</html>
)HTML";
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

void registerRoutes(StripState& dimState, StripState& ws1State, StripState& ws2State, Adafruit_NeoPixel& strip1, Adafruit_NeoPixel& strip2)
{
  if (!s_server) return;

  s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* res = req->beginResponse(200, "text/html", htmlIndex());
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  s_server->on("/api/dim/on", HTTP_GET, [&](AsyncWebServerRequest* req){
    dimState.on = true;
    LEDController::setPwmDuty(0, dimState.brightness);
    req->send(200, "application/json", "{\"ok\":true}");
  });
  s_server->on("/api/dim/off", HTTP_GET, [&](AsyncWebServerRequest* req){
    dimState.on = false;
    LEDController::setPwmDuty(0, 0);
    req->send(200, "application/json", "{\"ok\":true}");
  });
  s_server->on("/api/dim/brightness", HTTP_GET, [&](AsyncWebServerRequest* req){
    uint8_t b = getQueryU8(req, "b", dimState.brightness);
    dimState.brightness = b;
    LEDController::setPwmDuty(0, dimState.on ? b : 0);
    req->send(200, "application/json", String("{\"ok\":true,\"brightness\":") + b + "}");
  });

  // WS1
  s_server->on("/api/ws1/on", HTTP_GET, [&](AsyncWebServerRequest* req){ ws1State.on = true; LEDController::markDirty(1); req->send(200, "application/json", "{\"ok\":true}"); });
  s_server->on("/api/ws1/off", HTTP_GET, [&](AsyncWebServerRequest* req){ ws1State.on = false; LEDController::markDirty(1); req->send(200, "application/json", "{\"ok\":true}"); });
  s_server->on("/api/ws1/set", HTTP_GET, [&](AsyncWebServerRequest* req){
    ws1State.brightness = getQueryU8(req, "b", ws1State.brightness);
    ws1State.r = getQueryU8(req, "r", ws1State.r);
    ws1State.g = getQueryU8(req, "g", ws1State.g);
    ws1State.b = getQueryU8(req, "b2", ws1State.b);
    LEDController::markDirty(1);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // WS2
  s_server->on("/api/ws2/on", HTTP_GET, [&](AsyncWebServerRequest* req){ ws2State.on = true; LEDController::markDirty(2); req->send(200, "application/json", "{\"ok\":true}"); });
  s_server->on("/api/ws2/off", HTTP_GET, [&](AsyncWebServerRequest* req){ ws2State.on = false; LEDController::markDirty(2); req->send(200, "application/json", "{\"ok\":true}"); });
  s_server->on("/api/ws2/set", HTTP_GET, [&](AsyncWebServerRequest* req){
    ws2State.brightness = getQueryU8(req, "b", ws2State.brightness);
    ws2State.r = getQueryU8(req, "r", ws2State.r);
    ws2State.g = getQueryU8(req, "g", ws2State.g);
    ws2State.b = getQueryU8(req, "b2", ws2State.b);
    LEDController::markDirty(2);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  s_server->on("/api/onall", HTTP_GET, [&](AsyncWebServerRequest* req){
    dimState.on = ws1State.on = ws2State.on = true;
    LEDController::setPwmDuty(0, dimState.brightness);
    LEDController::markDirty(1); LEDController::markDirty(2);
    req->send(200, "application/json", "{\"ok\":true}");
  });
  s_server->on("/api/offall", HTTP_GET, [&](AsyncWebServerRequest* req){
    dimState.on = ws1State.on = ws2State.on = false;
    LEDController::setPwmDuty(0, 0);
    LEDController::markDirty(1); LEDController::markDirty(2);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // Animations
  s_server->on("/api/anim/start", HTTP_GET, [&](AsyncWebServerRequest* req){
    String name = req->hasParam("name") ? req->getParam("name")->value() : String("");
    unsigned long dur = 30000;
    if (req->hasParam("dur")) dur = (unsigned long)req->getParam("dur")->value().toInt();
    // If sunrise/sunset and no dur specified, default to 20 minutes
    if (!req->hasParam("dur") && (name == "sunrise" || name == "sunset")) {
      dur = 20UL * 60UL * 1000UL; // 20 minutes in ms
    }
    if (name == "sunrise") {
      LEDController::startAnimation(LEDController::Animation::Sunrise, dur);
    } else if (name == "sunset") {
      LEDController::startAnimation(LEDController::Animation::Sunset, dur);
    } else if (name == "waves") {
      LEDController::startAnimation(LEDController::Animation::Waves, dur);
    }
    req->send(200, "application/json", "{\"ok\":true}");
  });
  s_server->on("/api/anim/stop", HTTP_GET, [&](AsyncWebServerRequest* req){
    LEDController::stopAnimation();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  s_server->on("/api/state", HTTP_GET, [&](AsyncWebServerRequest* req) {
    String animName = "None";
    switch (LEDController::currentAnimation()) {
      case LEDController::Animation::Sunrise: animName = "Sunrise"; break;
      case LEDController::Animation::Sunset: animName = "Sunset"; break;
      case LEDController::Animation::Waves: animName = "Waves"; break;
      default: break;
    }

  String json = "{";
  // include current time if available
  String t = TimeService::nowIso();
  json += "\"time\":\"" + t + "\",";
  // include schedule info
  String sched = Scheduler::getScheduleJson();
  json += "\"schedule\":" + sched + ",";
    json += "\"animation\":\"" + animName + "\",";
    // Read actual PWM duty and strip hardware brightnesss where possible
    uint8_t hwPwm = LEDController::getPwmDuty(0);
    json += "\"dim\":{\"on\":" + String(dimState.on) + ",\"brightness\":" + String(hwPwm) + "},";
    StripState hw;
    if (LEDController::readStripHardware(1, hw)) {
      json += "\"ws1\":{\"on\":" + String(hw.on) + ",\"brightness\":" + String(hw.brightness) + ",\"r\":" + String(ws1State.r) + ",\"g\":" + String(ws1State.g) + ",\"b\":" + String(ws1State.b) + "},";
    } else {
      json += "\"ws1\":{\"on\":" + String(ws1State.on) + ",\"brightness\":" + String(ws1State.brightness) + ",\"r\":" + String(ws1State.r) + ",\"g\":" + String(ws1State.g) + ",\"b\":" + String(ws1State.b) + "},";
    }
    if (LEDController::readStripHardware(2, hw)) {
      json += "\"ws2\":{\"on\":" + String(hw.on) + ",\"brightness\":" + String(hw.brightness) + ",\"r\":" + String(ws2State.r) + ",\"g\":" + String(ws2State.g) + ",\"b\":" + String(ws2State.b) + "}";
    } else {
      json += "\"ws2\":{\"on\":" + String(ws2State.on) + ",\"brightness\":" + String(ws2State.brightness) + ",\"r\":" + String(ws2State.r) + ",\"g\":" + String(ws2State.g) + ",\"b\":" + String(ws2State.b) + "}";
    }
    json += "}";
    req->send(200, "application/json", json);
  });
}

} // namespace ApiServer
