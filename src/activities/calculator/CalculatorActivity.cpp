#include "CalculatorActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdlib>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* const CALC_GRID[5][4] = {
    {"C", "Del", "", "/"}, {"7", "8", "9", "*"}, {"4", "5", "6", "-"}, {"1", "2", "3", "+"}, {"0", ".", "", "="}};

// Simple recursive descent parser for basic math operations
double parseExpression(const char*& expr);

double parsePrimary(const char*& expr) {
  while (*expr == ' ') expr++;
  if (*expr == '-') {
    expr++;
    return -parsePrimary(expr);
  }
  if (*expr == '(') {
    expr++;
    double val = parseExpression(expr);
    if (*expr == ')') expr++;
    return val;
  }
  char* end;
  double val = strtod(expr, &end);
  if (expr == end) {
    return 0.0;
  }
  expr = end;
  return val;
}

double parseTerm(const char*& expr) {
  double val = parsePrimary(expr);
  while (true) {
    while (*expr == ' ') expr++;
    if (*expr == '*') {
      expr++;
      val *= parsePrimary(expr);
    } else if (*expr == '/') {
      expr++;
      double denom = parsePrimary(expr);
      if (denom != 0.0) {
        val /= denom;
      } else {
        val = 0.0;  // Avoid divide by zero crash
      }
    } else {
      break;
    }
  }
  return val;
}

double parseExpression(const char*& expr) {
  double val = parseTerm(expr);
  while (true) {
    while (*expr == ' ') expr++;
    if (*expr == '+') {
      expr++;
      val += parseTerm(expr);
    } else if (*expr == '-') {
      expr++;
      val -= parseTerm(expr);
    } else {
      break;
    }
  }
  return val;
}

std::string evaluate(const std::string& str) {
  if (str.empty()) return "";
  const char* expr = str.c_str();
  double result = parseExpression(expr);
  char buf[64];
  if (result == static_cast<long long>(result)) {
    snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(result));
  } else {
    snprintf(buf, sizeof(buf), "%.4g", result);
  }
  return std::string(buf);
}
}  // namespace

void CalculatorActivity::onEnter() {
  Activity::onEnter();
  selRow = 1;
  selCol = 0;
  inputExpr.clear();
  resultText.clear();
  requestUpdate();
}

void CalculatorActivity::onExit() { Activity::onExit(); }

void CalculatorActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    do {
      selCol = (selCol - 1 + 4) % 4;
    } while (CALC_GRID[selRow][selCol][0] == '\0');
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    do {
      selCol = (selCol + 1) % 4;
    } while (CALC_GRID[selRow][selCol][0] == '\0');
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    do {
      selRow = (selRow - 1 + 5) % 5;
    } while (CALC_GRID[selRow][selCol][0] == '\0');
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    do {
      selRow = (selRow + 1) % 5;
    } while (CALC_GRID[selRow][selCol][0] == '\0');
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleConfirm();
    requestUpdate();
  }
}

void CalculatorActivity::handleConfirm() {
  const char* label = CALC_GRID[selRow][selCol];

  if (strcmp(label, "C") == 0) {
    inputExpr.clear();
    resultText.clear();
    return;
  }

  if (strcmp(label, "Del") == 0) {
    if (!resultText.empty()) {
      resultText.clear();
    } else if (!inputExpr.empty()) {
      inputExpr.pop_back();
    }
    return;
  }

  if (strcmp(label, "=") == 0) {
    resultText = evaluate(inputExpr);
    return;
  }

  bool isOp =
      (strcmp(label, "+") == 0 || strcmp(label, "-") == 0 || strcmp(label, "*") == 0 || strcmp(label, "/") == 0);

  if (!resultText.empty()) {
    if (isOp) {
      inputExpr = resultText;
      resultText.clear();
    } else {
      inputExpr.clear();
      resultText.clear();
    }
  }

  if (isOp) {
    if (inputExpr.empty()) {
      if (strcmp(label, "-") == 0) {
        inputExpr += label;
      }
      return;
    }
    char lastChar = inputExpr.back();
    if (lastChar == '+' || lastChar == '-' || lastChar == '*' || lastChar == '/') {
      inputExpr.pop_back();
    }
  }

  inputExpr += label;
}

void CalculatorActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Draw header
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Calculator");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // 1. Draw Display Card
  const int displayHeight = 70;
  const int displayCardY = contentTop;
  const int displayCardX = metrics.contentSidePadding;
  const int displayCardWidth = pageWidth - 2 * metrics.contentSidePadding;
  renderer.drawRoundedRect(displayCardX, displayCardY, displayCardWidth, displayHeight, 1, 8, true);

  // Expression text (top-right of display)
  if (!inputExpr.empty()) {
    int exprW = renderer.getTextWidth(UI_10_FONT_ID, inputExpr.c_str());
    int exprX = displayCardX + displayCardWidth - 12 - exprW;
    renderer.drawText(UI_10_FONT_ID, exprX, displayCardY + 10, inputExpr.c_str(), true);
  }

  // Result text (bottom-right of display)
  if (!resultText.empty()) {
    int resW = renderer.getTextWidth(UI_12_FONT_ID, resultText.c_str());
    int resX = displayCardX + displayCardWidth - 12 - resW;
    renderer.drawText(UI_12_FONT_ID, resX, displayCardY + 40, resultText.c_str(), true, EpdFontFamily::BOLD);
  }

  // 2. Draw Keyboard Grid
  const int gridTop = displayCardY + displayHeight + metrics.verticalSpacing;
  const int gridHeight = contentBottom - gridTop;
  const int rowStep = gridHeight / 5;
  const int colStep = (pageWidth - 2 * metrics.contentSidePadding) / 4;

  for (int r = 0; r < 5; ++r) {
    for (int c = 0; c < 4; ++c) {
      const char* label = CALC_GRID[r][c];
      if (label[0] == '\0') continue;

      int keyX = metrics.contentSidePadding + c * colStep + 4;
      int keyY = gridTop + r * rowStep + 4;
      int keyW = colStep - 8;
      int keyH = rowStep - 8;

      const bool isSelected = (selRow == r && selCol == c);

      if (isSelected) {
        renderer.fillRoundedRect(keyX, keyY, keyW, keyH, 6, Color::Black);
      } else {
        renderer.drawRoundedRect(keyX, keyY, keyW, keyH, 1, 6, true);
      }

      int lblW = renderer.getTextWidth(UI_12_FONT_ID, label);
      int lblH = renderer.getLineHeight(UI_12_FONT_ID);
      int lblX = keyX + (keyW - lblW) / 2;
      int lblY = keyY + (keyH - lblH) / 2;

      renderer.drawText(UI_12_FONT_ID, lblX, lblY, label, !isSelected, EpdFontFamily::BOLD);
    }
  }

  // 3. Draw Button Hints (standard Back button layout)
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), nullptr, nullptr);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
