#include "WiFiManager.h"
#include <WiFi.h>
#include <ESPmDNS.h>

namespace WifiMgr {

bool begin(const char* hostname, const char* ssid, const char* password)
{
  bool ok = false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    if (millis() - start > 20000) { // timeout, continue anyway
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) ok = true;

  if (!MDNS.begin(hostname)) {
    // mDNS failed; not fatal
    return ok;
  }
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("arduino", "tcp", 3232);
  return ok;
}

String ipString()
{
  return WiFi.localIP().toString();
}

} // namespace WifiMgr
