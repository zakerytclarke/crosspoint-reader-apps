#include "DownloadWatchdog.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace DownloadWatchdog {
bool gotTimeout = false;
static TaskHandle_t watchdogTaskHandle = nullptr;
static unsigned long startTime = 0;
static unsigned long timeoutDuration = 15000;
static bool running = false;

static void watchdogTaskFunc(void* param) {
  while (running) {
    if (millis() - startTime > timeoutDuration) {
      LOG_ERR("WATCHDOG", "Timeout reached! Disconnecting WiFi.");
      gotTimeout = true;
      WiFi.disconnect();
      running = false;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  watchdogTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void start(unsigned long timeoutMs) {
  gotTimeout = false;
  startTime = millis();
  timeoutDuration = timeoutMs;
  if (running) {
    running = false;
    // Wait for previous task to delete itself
    while (watchdogTaskHandle != nullptr) {
      delay(10);
    }
  }
  running = true;
  xTaskCreate(watchdogTaskFunc, "dl_watchdog", 2048, nullptr, 5, &watchdogTaskHandle);
}

void stop() { running = false; }
}  // namespace DownloadWatchdog
