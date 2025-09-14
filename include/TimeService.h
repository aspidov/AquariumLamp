#pragma once
#include <Arduino.h>

namespace TimeService {
  // Initialize time via SNTP/NTP. Returns true if sync seemed successful within timeout.
  bool begin(const char* tz = "UTC", unsigned long timeoutMs = 10000);

  // Return epoch seconds (UTC). 0 if not yet synced.
  time_t now();

  // Human readable UTC ISO string (YYYY-MM-DDTHH:MM:SSZ) or empty if not synced.
  String nowIso();
}
