#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include "LEDController.h"

namespace ApiServer {
  void init(AsyncWebServer& server);
  void registerRoutes(StripState& dimState, StripState& ws1State, StripState& ws2State, Adafruit_NeoPixel& strip1, Adafruit_NeoPixel& strip2);
}
