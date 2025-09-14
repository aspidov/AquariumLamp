#include "TimeService.h"
#include <ctime>
#include <time.h>

namespace TimeService {

bool begin(const char* tz, unsigned long timeoutMs)
{
  // Configure timezone env and call tzset
  setenv("TZ", tz, 1);
  tzset();
  // Configure SNTP servers and start time sync. Use a public pool with fallback.
  // configTime will initialize the lwIP SNTP subsystem on ESP32.
  configTime(0, 0, "pool.ntp.org", "time.google.com", "1.pool.ntp.org");

  // Wait until we have a reasonable epoch. Use a higher threshold to ensure
  // the date is not 1970 (use 1e9 ~ 2001-09-09). This avoids false positives.
  unsigned long start = millis();
  time_t t = 0;
  const time_t GOOD_THRESHOLD = 1000000000; // ~2001-09-09
  while ((millis() - start) < timeoutMs) {
    t = time(nullptr);
    if (t > GOOD_THRESHOLD) {
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
