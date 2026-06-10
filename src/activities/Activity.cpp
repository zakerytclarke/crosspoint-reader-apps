#include "Activity.h"

#include "ActivityManager.h"

void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }

#include <HalClock.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "network/WifiSelectionActivity.h"
#include "util/WifiConnectHelper.h"

void Activity::ensureWifiConnected(std::function<void()> onConnected, std::function<void()> onCancelled) {
  auto syncClockIfNeeded = []() {
    if (halClock.isAvailable() && !SETTINGS.clockHasBeenSynced) {
      if (halClock.syncFromNTP()) {
        SETTINGS.clockHasBeenSynced = 1;
        SETTINGS.saveToFile();
      }
    }
  };

  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    syncClockIfNeeded();
    if (onConnected) onConnected();
    return;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this, onConnected, onCancelled, syncClockIfNeeded](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             syncClockIfNeeded();
                             if (onConnected) onConnected();
                           } else {
                             if (onCancelled)
                               onCancelled();
                             else
                               requestUpdate();
                           }
                         });
}
