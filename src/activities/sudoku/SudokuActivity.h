#pragma once

#include <string>

#include "activities/Activity.h"

class SudokuActivity final : public Activity {
 private:
  int board[9][9] = {0};
  int solution[9][9] = {0};
  bool initial[9][9] = {false};

  int cursorRow = 0;
  int cursorCol = 0;

  bool isEditingValue = false;
  int selectedValIndex = 0;  // 0 = Clear, 1-9 = values

  bool isChecked = false;
  bool hasWon = false;

  void generateNewPuzzle();
  bool checkWinState();

 public:
  explicit SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sudoku", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
