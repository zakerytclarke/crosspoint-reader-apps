#include "SudokuActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdlib>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void SudokuActivity::onEnter() {
  Activity::onEnter();
  srand(millis());
  generateNewPuzzle();
}

void SudokuActivity::onExit() { Activity::onExit(); }

void SudokuActivity::generateNewPuzzle() {
  // Start with a valid base grid
  const int baseGrid[9][9] = {{1, 2, 3, 4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9, 1, 2, 3}, {7, 8, 9, 1, 2, 3, 4, 5, 6},
                              {2, 3, 1, 5, 6, 4, 8, 9, 7}, {5, 6, 4, 8, 9, 7, 2, 3, 1}, {8, 9, 7, 2, 3, 1, 5, 6, 4},
                              {3, 1, 2, 6, 4, 5, 9, 7, 8}, {6, 4, 5, 9, 7, 8, 3, 1, 2}, {9, 7, 8, 3, 1, 2, 6, 4, 5}};

  // Copy base grid to board
  for (int i = 0; i < 9; i++) {
    for (int j = 0; j < 9; j++) {
      board[i][j] = baseGrid[i][j];
    }
  }

  // 1. Permute digits (1-9)
  int mapDigits[10];  // 1-indexed
  for (int i = 1; i <= 9; i++) mapDigits[i] = i;
  for (int i = 9; i > 1; i--) {
    int j = 1 + (rand() % i);
    int temp = mapDigits[i];
    mapDigits[i] = mapDigits[j];
    mapDigits[j] = temp;
  }
  for (int i = 0; i < 9; i++) {
    for (int j = 0; j < 9; j++) {
      board[i][j] = mapDigits[board[i][j]];
    }
  }

  // 2. Permute rows within blocks
  for (int block = 0; block < 3; block++) {
    int r1 = block * 3 + (rand() % 3);
    int r2 = block * 3 + (rand() % 3);
    if (r1 != r2) {
      for (int c = 0; c < 9; c++) {
        int temp = board[r1][c];
        board[r1][c] = board[r2][c];
        board[r2][c] = temp;
      }
    }
  }

  // 3. Permute columns within blocks
  for (int block = 0; block < 3; block++) {
    int c1 = block * 3 + (rand() % 3);
    int c2 = block * 3 + (rand() % 3);
    if (c1 != c2) {
      for (int r = 0; r < 9; r++) {
        int temp = board[r][c1];
        board[r][c1] = board[r][c2];
        board[r][c2] = temp;
      }
    }
  }

  // Save the solved state to solution grid
  for (int i = 0; i < 9; i++) {
    for (int j = 0; j < 9; j++) {
      solution[i][j] = board[i][j];
    }
  }

  // Remove exactly 45 cells at random to create the puzzle
  int countToRemove = 45;
  while (countToRemove > 0) {
    int idx = rand() % 81;
    int r = idx / 9;
    int c = idx % 9;
    if (board[r][c] != 0) {
      board[r][c] = 0;
      countToRemove--;
    }
  }

  // Mark initial clues
  for (int i = 0; i < 9; i++) {
    for (int j = 0; j < 9; j++) {
      initial[i][j] = (board[i][j] != 0);
    }
  }

  cursorRow = 4;
  cursorCol = 4;
  isEditingValue = false;
  isChecked = false;
  hasWon = false;
}

bool SudokuActivity::checkWinState() {
  for (int i = 0; i < 9; i++) {
    for (int j = 0; j < 9; j++) {
      if (board[i][j] != solution[i][j]) {
        return false;
      }
    }
  }
  return true;
}

void SudokuActivity::loop() {
  if (isEditingValue) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      isEditingValue = false;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      selectedValIndex = (selectedValIndex - 1 + 10) % 10;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectedValIndex = (selectedValIndex + 1) % 10;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      board[cursorRow][cursorCol] = selectedValIndex;
      isEditingValue = false;
      isChecked = false;
      hasWon = checkWinState();
      requestUpdate();
    }
  } else {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (cursorRow == 0) {
        cursorRow = -1;
        cursorCol = 0;  // Highlight "New Game"
      } else if (cursorRow == -1) {
        cursorRow = 8;
      } else {
        cursorRow--;
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (cursorRow == 8) {
        cursorRow = -1;
        cursorCol = 0;  // Highlight "New Game"
      } else if (cursorRow == -1) {
        cursorRow = 0;
      } else {
        cursorRow++;
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (cursorRow == -1) {
        cursorCol = (cursorCol - 1 + 2) % 2;  // Toggles between 0 (New Game) and 1 (Check)
      } else {
        cursorCol = (cursorCol - 1 + 9) % 9;
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (cursorRow == -1) {
        cursorCol = (cursorCol + 1) % 2;  // Toggles between 0 (New Game) and 1 (Check)
      } else {
        cursorCol = (cursorCol + 1) % 9;
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (cursorRow == -1) {
        if (cursorCol == 0) {
          generateNewPuzzle();
        } else if (cursorCol == 1) {
          isChecked = true;
          hasWon = checkWinState();
        }
        requestUpdate();
      } else {
        if (!initial[cursorRow][cursorCol] && !hasWon) {
          isEditingValue = true;
          selectedValIndex = board[cursorRow][cursorCol];
          requestUpdate();
        }
      }
    }
  }
}

void SudokuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Sudoku");

  const int gridWidth = 324;
  const int gridHeight = 324;
  const int gridX = (pageWidth - gridWidth) / 2;
  const int gridY = metrics.topPadding + metrics.headerHeight + 60;

  // Draw Top Toolbar (New Game, Check)
  const int toolbarY = gridY - 45;
  const int toolbarBtnW = 100;
  const int toolbarBtnH = 26;
  const int toolbarBtnX1 = (pageWidth - (2 * toolbarBtnW + 20)) / 2;
  const int toolbarBtnX2 = toolbarBtnX1 + toolbarBtnW + 20;

  // New Game Button
  bool newGameSelected = (cursorRow == -1 && cursorCol == 0);
  renderer.drawRoundedRect(toolbarBtnX1, toolbarY, toolbarBtnW, toolbarBtnH, 1, 6, true);
  if (newGameSelected) {
    renderer.fillRoundedRect(toolbarBtnX1, toolbarY, toolbarBtnW, toolbarBtnH, 6, Color::Black);
  }
  int ngTextW = renderer.getTextWidth(SMALL_FONT_ID, "New Game");
  int ngTextX = toolbarBtnX1 + (toolbarBtnW - ngTextW) / 2;
  int ngTextY = toolbarY + (toolbarBtnH - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
  renderer.drawText(SMALL_FONT_ID, ngTextX, ngTextY, "New Game", !newGameSelected);

  // Check Button
  bool checkSelected = (cursorRow == -1 && cursorCol == 1);
  renderer.drawRoundedRect(toolbarBtnX2, toolbarY, toolbarBtnW, toolbarBtnH, 1, 6, true);
  if (checkSelected) {
    renderer.fillRoundedRect(toolbarBtnX2, toolbarY, toolbarBtnW, toolbarBtnH, 6, Color::Black);
  }
  int checkTextW = renderer.getTextWidth(SMALL_FONT_ID, "Check");
  int checkTextX = toolbarBtnX2 + (toolbarBtnW - checkTextW) / 2;
  int checkTextY = toolbarY + (toolbarBtnH - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
  renderer.drawText(SMALL_FONT_ID, checkTextX, checkTextY, "Check", !checkSelected);

  // Draw grid cells and digits
  for (int r = 0; r < 9; r++) {
    for (int c = 0; c < 9; c++) {
      int cellX = gridX + c * 36;
      int cellY = gridY + r * 36;

      // Draw cursor: double line inside the highlighted cell
      if (r == cursorRow && c == cursorCol && !isEditingValue && !hasWon) {
        renderer.drawRect(cellX + 2, cellY + 2, 32, 32, 2, true);
      }

      if (board[r][c] != 0) {
        char buf[2] = {static_cast<char>('0' + board[r][c]), '\0'};
        EpdFontFamily::Style style = initial[r][c] ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
        int textWidth = renderer.getTextWidth(UI_12_FONT_ID, buf, style);
        int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
        int textX = cellX + (36 - textWidth) / 2;
        int textY = cellY + (36 - textHeight) / 2;
        renderer.drawText(UI_12_FONT_ID, textX, textY, buf, true, style);

        // Highlight incorrect values if Checked
        if (isChecked && !initial[r][c] && board[r][c] != solution[r][c]) {
          // Draw diagonal crossed lines inside cell background to mark error
          renderer.drawLine(cellX + 4, cellY + 4, cellX + 32, cellY + 32, 1, true);
          renderer.drawLine(cellX + 32, cellY + 4, cellX + 4, cellY + 32, 1, true);
        }
      }
    }
  }

  // Draw vertical grid lines
  for (int i = 0; i <= 9; i++) {
    int x = gridX + i * 36;
    int lineWidth = (i % 3 == 0) ? 3 : 1;
    renderer.drawLine(x, gridY, x, gridY + gridHeight, lineWidth, true);
  }

  // Draw horizontal grid lines
  for (int i = 0; i <= 9; i++) {
    int y = gridY + i * 36;
    int lineWidth = (i % 3 == 0) ? 3 : 1;
    renderer.drawLine(gridX, y, gridX + gridWidth, y, lineWidth, true);
  }

  if (hasWon) {
    // Show success dialog
    const int cardW = 300;
    const int cardH = 100;
    const int cardX = (pageWidth - cardW) / 2;
    const int cardY = gridY + gridHeight + 20;

    renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 2, 12, true);
    int curY = cardY + 25;
    renderer.drawCenteredText(UI_12_FONT_ID, curY, "CONGRATULATIONS!", true, EpdFontFamily::BOLD);
    curY += renderer.getLineHeight(UI_12_FONT_ID) + 15;
    renderer.drawCenteredText(SMALL_FONT_ID, curY, "You solved the puzzle!", true, EpdFontFamily::REGULAR);
  } else if (isEditingValue) {
    // Value selector bar
    const int barY = pageHeight - metrics.buttonHintsHeight - 65;
    const int barH = 40;
    const int itemW = 32;
    const int spacing = 6;
    const int totalW = 10 * itemW + 9 * spacing;
    const int barX = (pageWidth - totalW) / 2;

    for (int i = 0; i < 10; i++) {
      int itemX = barX + i * (itemW + spacing);
      bool selected = (i == selectedValIndex);

      renderer.drawRoundedRect(itemX, barY, itemW, barH, 1, 6, true);
      if (selected) {
        renderer.fillRoundedRect(itemX, barY, itemW, barH, 6, Color::Black);
      }

      char buf[2] = {'\0', '\0'};
      if (i == 0) {
        buf[0] = 'X';  // Clear option
      } else {
        buf[0] = '0' + i;
      }

      int textWidth = renderer.getTextWidth(UI_12_FONT_ID, buf, EpdFontFamily::BOLD);
      int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
      int textX = itemX + (itemW - textWidth) / 2;
      int textY = barY + (barH - textHeight) / 2;

      renderer.drawText(UI_12_FONT_ID, textX, textY, buf, !selected, EpdFontFamily::BOLD);
    }
  }

  // Draw Button Hints
  if (isEditingValue) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
