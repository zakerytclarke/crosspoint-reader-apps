#pragma once

#include <string>

#include "activities/Activity.h"

enum class DiceMode { D6, Arrow, D20, Magic8 };

class DiceActivity final : public Activity {
 private:
  DiceMode currentMode = DiceMode::D6;

  int lastRollD6 = 1;
  int lastRollArrowAngle = 0;  // 0 to 359
  int lastRollD20 = 20;
  int lastRollMagic8 = 0;

  void roll();
  void drawD6(int x, int y, int size, int value);
  void drawArrow(int x, int y, int size, int angle);
  void drawD20(int x, int y, int size, int value);
  void drawMagic8(int x, int y, int size, int responseIndex);

 public:
  explicit DiceActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Dice", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
