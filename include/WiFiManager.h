#pragma once
#include <Arduino.h>

namespace WifiMgr {
  // Returns true if connected within timeout, false otherwise. Non-fatal failures allowed.
  bool begin(const char* hostname, const char* ssid, const char* password);
  // Human-readable IP (may be 0.0.0.0 if not connected)
  String ipString();
}
