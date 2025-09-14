#pragma once
#include <Arduino.h>

namespace OTAHandler {
  // Initialize ArduinoOTA. Return true if initialization attempted; false on fatal error.
  bool begin(const char* hostname);
  // Call regularly from loop()
  void handle();
}
