#include <Arduino.h>
#include "secrets.h"
#include <Adafruit_NeoPixel.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "WiFiManager.h"
#include "OTAHandler.h"
#include "LEDController.h"
#include "ApiServer.h"
#include "TimeService.h"

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
// Use StripState from include/LEDController.h
StripState dimState   {255, 255, 255, 255, true}; // brightness used as PWM duty; rgb unused
StripState ws1State   {128, 255, 255, 255, true};
StripState ws2State   {128, 255, 255, 255, true};

// ------------------- HELPERS -------------------
// helper for small functions moved to LEDController/ApiServer

// WiFi and OTA handled by WifiMgr and OTAHandler

// ------------------- SETUP/LOOP -------------------
void setup()
{
  Serial.begin(115200);
  delay(200);
  // Initialize PWM via LEDController
  LEDController::initPwm(DIM_STRIP_PIN, DIM_CH, DIM_FREQ, DIM_RES, dimState.brightness);

  // Register and initialize addressable strips
  LEDController::registerStrips(strip1, strip2);
  // Ensure initial colors are shown
  LEDController::markDirty(1); LEDController::markDirty(2);

  // Start WiFi (best effort). OTA should still be initialized even if WiFi fails.
  bool wifiOk = WifiMgr::begin(HOSTNAME, WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("WiFi connected: %d, IP: %s\n", wifiOk ? 1 : 0, WifiMgr::ipString().c_str());

  // Initialize time from NTP only if WiFi connected. Best-effort; will not block OTA.
  bool timeOk = false;
  if (wifiOk) {
    timeOk = TimeService::begin("UTC", 10000);
  }
  Serial.printf("Time synced: %d, now=%s\n", timeOk ? 1 : 0, TimeService::nowIso().c_str());

  // Initialize OTA (kept independent). Return value not critical.
  OTAHandler::begin(HOSTNAME);

  // Start web server and routes. If server fails, OTA still runs.
  ApiServer::init(server);
  ApiServer::registerRoutes(dimState, ws1State, ws2State, strip1, strip2);
  server.begin();

  Serial.printf("HTTP server started (hostname=%s)\n", HOSTNAME);
}

void loop()
{
  // Keep OTA handling running; OTAHandler is isolated and safe.
  OTAHandler::handle();

  // Let LEDController handle pending updates
  LEDController::loop(ws1State, ws2State);

  // If time wasn't synced at startup, try once after WiFi gets an IP.
  static bool s_timeSyncedHere = false;
  if (!s_timeSyncedHere) {
    String ip = WifiMgr::ipString();
    if (ip != "0.0.0.0" && ip.length() > 0) {
      bool ok = TimeService::begin("UTC", 10000);
      Serial.printf("Deferred time sync: %d, now=%s\n", ok ? 1 : 0, TimeService::nowIso().c_str());
      s_timeSyncedHere = true; // only attempt once here
    }
  }

  // You can add lightweight periodic tasks here (no delay(…); use vTaskDelay if needed)
  // vTaskDelay(1); // optional yield
}
