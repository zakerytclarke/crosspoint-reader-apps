#pragma once

#include <string>

#include "activities/Activity.h"

class CalculatorActivity final : public Activity {
 private:
  int selRow = 1;  // start on digit '7'
  int selCol = 0;
  std::string inputExpr;
  std::string resultText;

  void handleConfirm();

 public:
  explicit CalculatorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Calculator", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
