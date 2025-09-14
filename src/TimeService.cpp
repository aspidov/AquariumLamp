#include "TimeService.h"
#include <ctime>
#include <time.h>

namespace TimeService {

bool begin(const char* tz, unsigned long timeoutMs)
{
  // Configure timezone env and call tzset
  setenv("TZ", tz, 1);
  tzset();

  // Start SNTP (Arduino core for ESP32 wires this up when time.h funcs used)
  // We'll try to wait until time is non-zero
  unsigned long start = millis();
  time_t t = 0;
  while ((millis() - start) < timeoutMs) {
    t = time(nullptr);
    if (t > 1000000) { // arbitrary threshold (past 1970)
      return true;
    }
    delay(200);
  }
  // not synced within timeout
  return false;
}

time_t now()
{
  return time(nullptr);
}

String nowIso()
{
  time_t t = now();
  if (t <= 0) return String("");
  struct tm tm;
  gmtime_r(&t, &tm);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
           tm.tm_hour, tm.tm_min, tm.tm_sec);
  return String(buf);
}

} // namespace TimeService
