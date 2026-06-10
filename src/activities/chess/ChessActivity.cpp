#include "ChessActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Correct Midpoint Circle helper (Bresenham's)
static void drawCircle(const GfxRenderer& renderer, int x0, int y0, int radius, int thickness, bool state) {
  for (int t = 0; t < thickness; t++) {
    int r = radius - t;
    int x = r;
    int y = 0;
    int err = 3 - 2 * r;
    while (x >= y) {
      renderer.drawPixel(x0 + x, y0 + y, state);
      renderer.drawPixel(x0 + y, y0 + x, state);
      renderer.drawPixel(x0 - y, y0 + x, state);
      renderer.drawPixel(x0 - x, y0 + y, state);
      renderer.drawPixel(x0 - x, y0 - y, state);
      renderer.drawPixel(x0 - y, y0 - x, state);
      renderer.drawPixel(x0 + y, y0 - x, state);
      renderer.drawPixel(x0 + x, y0 - y, state);

      if (err >= 0) {
        x -= 1;
        err += 4 * (y - x) + 10;
      } else {
        err += 4 * y + 6;
      }
      y += 1;
    }
  }
}

static void fillCircle(const GfxRenderer& renderer, int x0, int y0, int radius, bool state) {
  int x = radius;
  int y = 0;
  int err = 3 - 2 * radius;
  while (x >= y) {
    renderer.drawLine(x0 - x, y0 + y, x0 + x, y0 + y, state);
    renderer.drawLine(x0 - y, y0 + x, x0 + y, y0 + x, state);
    renderer.drawLine(x0 - x, y0 - y, x0 + x, y0 - y, state);
    renderer.drawLine(x0 - y, y0 - x, x0 + y, y0 - x, state);
    if (err >= 0) {
      x -= 1;
      err += 4 * (y - x) + 10;
    } else {
      err += 4 * y + 6;
    }
    y += 1;
  }
}

void ChessActivity::onEnter() {
  Activity::onEnter();
  bool isBoardEmpty = true;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (board[r][c] != 0) {
        isBoardEmpty = false;
        break;
      }
    }
  }
  if (isBoardEmpty) {
    setupInitialBoard();
    cursorRow = 7;
    cursorCol = 4;
    selectedRow = -1;
    selectedCol = -1;
    whiteTurn = true;
    flippedView = false;
  }
  requestUpdate();
}

void ChessActivity::onExit() { Activity::onExit(); }

void ChessActivity::setupInitialBoard() {
  // Empty board
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      board[r][c] = 0;
    }
  }

  // Pawns
  for (int c = 0; c < 8; c++) {
    board[1][c] = -1;  // Black Pawn
    board[6][c] = 1;   // White Pawn
  }

  // Rooks
  board[0][0] = board[0][7] = -4;  // Black
  board[7][0] = board[7][7] = 4;   // White

  // Knights
  board[0][1] = board[0][6] = -2;  // Black
  board[7][1] = board[7][6] = 2;   // White

  // Bishops
  board[0][2] = board[0][5] = -3;  // Black
  board[7][2] = board[7][5] = 3;   // White

  // Queens
  board[0][3] = -5;  // Black Queen
  board[7][3] = 5;   // White Queen

  // Kings
  board[0][4] = -6;  // Black King
  board[7][4] = 6;   // White King
}

bool ChessActivity::isPathClear(int fromRow, int fromCol, int toRow, int toCol) const {
  int dr = toRow - fromRow;
  int dc = toCol - fromCol;
  int stepR = (dr == 0) ? 0 : (dr > 0 ? 1 : -1);
  int stepC = (dc == 0) ? 0 : (dc > 0 ? 1 : -1);

  int r = fromRow + stepR;
  int c = fromCol + stepC;
  while (r != toRow || c != toCol) {
    if (board[r][c] != 0) {
      return false;
    }
    r += stepR;
    c += stepC;
  }
  return true;
}

bool ChessActivity::isValidMove(int fromRow, int fromCol, int toRow, int toCol) const {
  // Bounds check
  if (fromRow < 0 || fromRow >= 8 || fromCol < 0 || fromCol >= 8) return false;
  if (toRow < 0 || toRow >= 8 || toCol < 0 || toCol >= 8) return false;

  // Destination cannot contain player's own piece
  int piece = board[fromRow][fromCol];
  int target = board[toRow][toCol];
  if (piece == 0) return false;
  if (target != 0 && (piece * target > 0)) return false;

  int dr = toRow - fromRow;
  int dc = toCol - fromCol;
  int absPiece = abs(piece);

  switch (absPiece) {
    case 1: {  // Pawn
      int dir = (piece > 0) ? -1 : 1;
      // Single step forward
      if (dr == dir && dc == 0 && target == 0) {
        return true;
      }
      // Double step forward
      int startRow = (piece > 0) ? 6 : 1;
      if (fromRow == startRow && dr == 2 * dir && dc == 0 && target == 0 && board[fromRow + dir][fromCol] == 0) {
        return true;
      }
      // Diagonal Capture
      if (dr == dir && abs(dc) == 1 && target != 0 && (piece * target < 0)) {
        return true;
      }
      return false;
    }
    case 2:  // Knight
      return (abs(dr) * abs(dc) == 2);

    case 3:  // Bishop
      if (abs(dr) == abs(dc)) {
        return isPathClear(fromRow, fromCol, toRow, toCol);
      }
      return false;

    case 4:  // Rook
      if (dr == 0 || dc == 0) {
        return isPathClear(fromRow, fromCol, toRow, toCol);
      }
      return false;

    case 5:  // Queen
      if (abs(dr) == abs(dc) || dr == 0 || dc == 0) {
        return isPathClear(fromRow, fromCol, toRow, toCol);
      }
      return false;

    case 6:  // King
      return (std::max(abs(dr), abs(dc)) == 1);

    default:
      return false;
  }
}

void ChessActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (selectedRow != -1) {
      selectedRow = -1;
      selectedCol = -1;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (cursorRow == -1) {
    // Toolbar navigation
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      cursorCol = (cursorCol - 1 + 2) % 2;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      cursorCol = (cursorCol + 1) % 2;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      cursorRow = flippedView ? 0 : 7;
      cursorCol = 4;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (cursorCol == 0) {
        // Reset everything for a new game
        for (int r = 0; r < 8; r++) {
          for (int c = 0; c < 8; c++) {
            board[r][c] = 0;
          }
        }
        setupInitialBoard();
        cursorRow = 7;
        cursorCol = 4;
        selectedRow = -1;
        selectedCol = -1;
        whiteTurn = true;
        flippedView = false;
        requestUpdate();
      } else {
        flippedView = !flippedView;
        requestUpdate();
      }
    }
  } else {
    // Board navigation
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (flippedView) {
        if (cursorRow == 7) {
          cursorRow = -1;
          cursorCol = 0;
        } else {
          cursorRow++;
        }
      } else {
        if (cursorRow == 0) {
          cursorRow = -1;
          cursorCol = 0;
        } else {
          cursorRow--;
        }
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (flippedView) {
        if (cursorRow == 0) {
          cursorRow = -1;
          cursorCol = 0;
        } else {
          cursorRow--;
        }
      } else {
        if (cursorRow == 7) {
          cursorRow = -1;
          cursorCol = 0;
        } else {
          cursorRow++;
        }
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (flippedView) {
        cursorCol = (cursorCol + 1) % 8;
      } else {
        cursorCol = (cursorCol - 1 + 8) % 8;
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (flippedView) {
        cursorCol = (cursorCol - 1 + 8) % 8;
      } else {
        cursorCol = (cursorCol + 1) % 8;
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      int clickedPiece = board[cursorRow][cursorCol];

      if (selectedRow == -1) {
        // First selection click
        if (clickedPiece != 0 && ((whiteTurn && clickedPiece > 0) || (!whiteTurn && clickedPiece < 0))) {
          selectedRow = cursorRow;
          selectedCol = cursorCol;
          requestUpdate();
        }
      } else {
        // Destination select click
        if (cursorRow == selectedRow && cursorCol == selectedCol) {
          // Deselect
          selectedRow = -1;
          selectedCol = -1;
          requestUpdate();
        } else if (clickedPiece != 0 && ((whiteTurn && clickedPiece > 0) || (!whiteTurn && clickedPiece < 0))) {
          // Select alternative own piece
          selectedRow = cursorRow;
          selectedCol = cursorCol;
          requestUpdate();
        } else if (isValidMove(selectedRow, selectedCol, cursorRow, cursorCol)) {
          // Perform move
          int movingPiece = board[selectedRow][selectedCol];

          // Check Pawn Promotion
          if (abs(movingPiece) == 1 && (cursorRow == 0 || cursorRow == 7)) {
            movingPiece = (movingPiece > 0) ? 5 : -5;  // Auto-promote to Queen
          }

          board[cursorRow][cursorCol] = movingPiece;
          board[selectedRow][selectedCol] = 0;

          selectedRow = -1;
          selectedCol = -1;
          whiteTurn = !whiteTurn;
          requestUpdate();
        }
      }
    }
  }
}

void ChessActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Chess");

  // Draw Toolbar
  const int toolbarY = metrics.topPadding + metrics.headerHeight + 20;

  bool newGameSel = (cursorRow == -1 && cursorCol == 0);
  renderer.drawRoundedRect(20, toolbarY, 120, 30, 1, 5, true);
  if (newGameSel) {
    renderer.fillRoundedRect(20, toolbarY, 120, 30, 5, Color::Black);
  }
  renderer.drawText(SMALL_FONT_ID, 30, toolbarY + 7, "New Game", !newGameSel);

  bool flipSel = (cursorRow == -1 && cursorCol == 1);
  renderer.drawRoundedRect(160, toolbarY, 120, 30, 1, 5, true);
  if (flipSel) {
    renderer.fillRoundedRect(160, toolbarY, 120, 30, 5, Color::Black);
  }
  renderer.drawText(SMALL_FONT_ID, 170, toolbarY + 7, "Flip Board", !flipSel);

  // Chess Board dimensions
  const int cellS = 48;  // 48x48 squares to fit 48pt emoji font
  const int boardW = 8 * cellS;
  const int gridX = (pageWidth - boardW) / 2;
  const int gridY = toolbarY + 50;

  // Grid background outline
  renderer.drawRect(gridX - 2, gridY - 2, boardW + 4, boardW + 4, 2, true);

  // Draw Board Squares
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      int displayR = flippedView ? 7 - r : r;
      int displayC = flippedView ? 7 - c : c;

      int cx = gridX + displayC * cellS;
      int cy = gridY + displayR * cellS;

      // Checkerboard colors
      bool isDarkSquare = ((r + c) % 2 == 1);
      renderer.drawRect(cx, cy, cellS, cellS, 1, true);
      if (isDarkSquare) {
        // Draw diagonal pattern of black lines inside dark squares
        for (int i = 4; i < cellS; i += 8) {
          renderer.drawLine(cx + i, cy + 1, cx + 1, cy + i, 1, true);
          renderer.drawLine(cx + cellS - 2, cy + i, cx + i, cy + cellS - 2, 1, true);
        }
      }

      // Draw piece if present
      int piece = board[r][c];
      if (piece != 0) {
        const char* pieceStr = "";
        switch (piece) {
          case 1:
            pieceStr = "\u2659";
            break;  // White Pawn
          case 2:
            pieceStr = "\u2658";
            break;  // White Knight
          case 3:
            pieceStr = "\u2657";
            break;  // White Bishop
          case 4:
            pieceStr = "\u2656";
            break;  // White Rook
          case 5:
            pieceStr = "\u2655";
            break;  // White Queen
          case 6:
            pieceStr = "\u2654";
            break;  // White King
          case -1:
            pieceStr = "\u265F";
            break;  // Black Pawn
          case -2:
            pieceStr = "\u265E";
            break;  // Black Knight
          case -3:
            pieceStr = "\u265D";
            break;  // Black Bishop
          case -4:
            pieceStr = "\u265C";
            break;  // Black Rook
          case -5:
            pieceStr = "\u265B";
            break;  // Black Queen
          case -6:
            pieceStr = "\u265A";
            break;  // Black King
        }

        int tW = renderer.getTextWidth(NOTOSANS_16_EMOJI_FONT_ID, pieceStr);
        int tH = renderer.getTextHeight(NOTOSANS_16_EMOJI_FONT_ID);
        int px = cx + (cellS - tW) / 2;
        int py = cy + (cellS - tH) / 2;
        renderer.drawText(NOTOSANS_16_EMOJI_FONT_ID, px, py, pieceStr, true);
      }

      // Highlight selected piece
      if (selectedRow == r && selectedCol == c) {
        renderer.drawRect(cx + 2, cy + 2, cellS - 4, cellS - 4, 2, true);
      }

      // Draw dot if valid move destination for selected piece
      if (selectedRow != -1 && isValidMove(selectedRow, selectedCol, r, c)) {
        fillCircle(renderer, cx + cellS / 2, cy + cellS / 2, 4, true);
      }

      // Draw cursor cell outline
      if (cursorRow == r && cursorCol == c) {
        renderer.drawRect(cx, cy, cellS, cellS, 3, true);
      }
    }
  }

  // Footer status indicator
  const int footerY = gridY + boardW + 20;
  std::string status = whiteTurn ? "White's Turn" : "Black's Turn";
  if (selectedRow != -1) {
    status += " - Select destination";
  }
  renderer.drawCenteredText(UI_12_FONT_ID, footerY, status.c_str(), true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
