#include "OTAHandler.h"
#include <ArduinoOTA.h>

namespace OTAHandler {

bool begin(const char* hostname)
{
  // Wrap in try-like defensive checks. ArduinoOTA functions don't throw but
  // init may fail if network stack isn't initialized; we return false then.
  bool ok = true;
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([](){ Serial.println("OTA Start"); });
  ArduinoOTA.onEnd([](){ Serial.println("\nOTA End"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error){
    Serial.printf("OTA Error[%u]\n", error);
  });

  // begin may fail; catch that by checking return of begin() if available.
  ArduinoOTA.begin();
  return ok;
}

void handle()
{
  // Keep OTA handling in loop; allow caller to call this regularly.
  ArduinoOTA.handle();
}

} // namespace OTAHandler
