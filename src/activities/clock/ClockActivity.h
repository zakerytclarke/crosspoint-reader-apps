#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"

enum class ClockMode { Analog, Digital, Flip };

class ClockActivity final : public Activity {
 public:
  ClockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ClockMode mode;
  bool use12Hour;
  uint8_t lastHour;
  uint8_t lastMinute;
  unsigned long lastTimeCheck = 0;

  void getLocalTime(uint8_t& hour, uint8_t& minute);
  void drawAnalogClock(const ThemeMetrics& metrics, int contentTop, int contentHeight, int pageWidth, uint8_t hour,
                       uint8_t minute);
  void drawDigitalClock(const ThemeMetrics& metrics, int contentTop, int contentHeight, int pageWidth, uint8_t hour,
                        uint8_t minute);
  void drawFlipClock(const ThemeMetrics& metrics, int contentTop, int contentHeight, int pageWidth, uint8_t hour,
                     uint8_t minute);

  void drawDigit(int x, int y, int digit, int blockSize, Color color);
  void drawColon(int x, int y, int blockSize, Color color);
};
