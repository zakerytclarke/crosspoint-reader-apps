#pragma once

#include <string>

#include "activities/Activity.h"

class ChessActivity final : public Activity {
 private:
  // Positive values: White pieces. Negative values: Black pieces.
  // 1=Pawn, 2=Knight, 3=Bishop, 4=Rook, 5=Queen, 6=King, 0=Empty.
  int board[8][8] = {0};

  int cursorRow = 7;
  int cursorCol = 4;

  int selectedRow = -1;
  int selectedCol = -1;

  bool whiteTurn = true;
  bool flippedView = false;

  void setupInitialBoard();
  bool isValidMove(int fromRow, int fromCol, int toRow, int toCol) const;
  bool isPathClear(int fromRow, int fromCol, int toRow, int toCol) const;

 public:
  explicit ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Chess", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
