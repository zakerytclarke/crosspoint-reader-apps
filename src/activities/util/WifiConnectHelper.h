#pragma once
#include <Logging.h>
#include <WiFi.h>

#include "WifiCredentialStore.h"

namespace WifiConnectHelper {
inline bool connectToDefaultWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  LOG_DBG("WIFI_HELP", "Powering on WiFi antenna...");
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(150);

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String hostname = "CrossPoint-Reader-" + mac;
  WiFi.setHostname(hostname.c_str());

  // Check if there is a last connected SSID in WIFI_STORE
  WIFI_STORE.loadFromFile();
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) {
    const auto* cred = WIFI_STORE.findCredential(lastSsid);
    if (cred) {
      LOG_DBG("WIFI_HELP", "Auto-connecting to saved SSID: %s", lastSsid.c_str());
      WiFi.persistent(false);
      if (!cred->password.empty()) {
        WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
      } else {
        WiFi.begin(cred->ssid.c_str());
      }

      int retries = 0;
      while (WiFi.status() != WL_CONNECTED && retries < 100) {  // 10 seconds timeout
        delay(100);
        retries++;
      }
      if (WiFi.status() == WL_CONNECTED) {
        LOG_DBG("WIFI_HELP", "Successfully connected to %s", lastSsid.c_str());
        return true;
      }
    }
  }

  // Loop through all other credentials saved in WIFI_STORE
  for (const auto& cred : WIFI_STORE.getCredentials()) {
    if (cred.ssid == lastSsid) {
      continue;
    }
    LOG_DBG("WIFI_HELP", "Auto-connecting to other saved SSID: %s", cred.ssid.c_str());
    WiFi.persistent(false);
    if (!cred.password.empty()) {
      WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
    } else {
      WiFi.begin(cred.ssid.c_str());
    }

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 80) {  // 8 seconds timeout
      delay(100);
      retries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      LOG_DBG("WIFI_HELP", "Successfully connected to %s", cred.ssid.c_str());
      WIFI_STORE.setLastConnectedSsid(cred.ssid);
      WIFI_STORE.saveToFile();
      return true;
    }
  }

  // Otherwise try standard begin (fallback to NVS/SDK credentials)
  LOG_DBG("WIFI_HELP", "Fallback begin connecting...");
  WiFi.begin();
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 80) {  // 8 seconds timeout
    delay(100);
    retries++;
  }
  return WiFi.status() == WL_CONNECTED;
}

inline bool ensureWifiConnected() { return connectToDefaultWifi(); }

inline bool waitForTimeSync(int timeoutMs = 5000) {
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  int elapsed = 0;
  while (timeinfo.tm_year < (2020 - 1900) && elapsed < timeoutMs) {
    delay(100);
    elapsed += 100;
    now = time(nullptr);
    gmtime_r(&now, &timeinfo);
  }
  if (timeinfo.tm_year >= (2020 - 1900)) {
    return true;
  }
  LOG_ERR("WIFI_HELP", "NTP time sync timed out");
  return false;
}
}  // namespace WifiConnectHelper
