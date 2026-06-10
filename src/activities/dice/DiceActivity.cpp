#include "DiceActivity.h"

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

void DiceActivity::onEnter() {
  Activity::onEnter();
  srand(millis());
  roll();
  requestUpdate();
}

void DiceActivity::onExit() { Activity::onExit(); }

void DiceActivity::roll() {
  switch (currentMode) {
    case DiceMode::D6:
      lastRollD6 = 1 + (rand() % 6);
      break;
    case DiceMode::Arrow:
      lastRollArrowAngle = rand() % 360;
      break;
    case DiceMode::D20:
      lastRollD20 = 1 + (rand() % 20);
      break;
    case DiceMode::Magic8:
      lastRollMagic8 = rand() % 20;  // 20 standard responses
      break;
  }
}

void DiceActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    currentMode = static_cast<DiceMode>((static_cast<int>(currentMode) - 1 + 4) % 4);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    currentMode = static_cast<DiceMode>((static_cast<int>(currentMode) + 1) % 4);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    {
      RenderLock lock;
      GUI.drawPopup(renderer, "Rolling...");
      renderer.displayBuffer();
    }
    delay(400);  // Wait 0.4s with the overlay visible
    roll();
    requestUpdate();
  }
}

void DiceActivity::drawD6(int x, int y, int size, int value) {
  const char* dieStr = "";
  switch (value) {
    case 1:
      dieStr = "\u2680";
      break;
    case 2:
      dieStr = "\u2681";
      break;
    case 3:
      dieStr = "\u2682";
      break;
    case 4:
      dieStr = "\u2683";
      break;
    case 5:
      dieStr = "\u2684";
      break;
    case 6:
      dieStr = "\u2685";
      break;
  }
  int tW = renderer.getTextWidth(NOTOSANS_48_EMOJI_FONT_ID, dieStr);
  int tH = renderer.getTextHeight(NOTOSANS_48_EMOJI_FONT_ID);
  renderer.drawText(NOTOSANS_48_EMOJI_FONT_ID, x - tW / 2, y - tH / 2, dieStr, true);
}

void DiceActivity::drawArrow(int x, int y, int size, int angle) {
  float rad = angle * (float)M_PI / 180.0f;
  int radius = size / 2;

  int endX = x + (int)(cosf(rad) * radius);
  int endY = y + (int)(sinf(rad) * radius);

  // Arrow shaft
  renderer.drawLine(x, y, endX, endY, 6, true);

  // Arrow head
  float headLen = 20.0f;
  float arrowAngle1 = rad + (float)M_PI * 0.85f;
  float arrowAngle2 = rad - (float)M_PI * 0.85f;

  int h1X = endX + (int)(cosf(arrowAngle1) * headLen);
  int h1Y = endY + (int)(sinf(arrowAngle1) * headLen);
  int h2X = endX + (int)(cosf(arrowAngle2) * headLen);
  int h2Y = endY + (int)(sinf(arrowAngle2) * headLen);

  renderer.drawLine(endX, endY, h1X, h1Y, 4, true);
  renderer.drawLine(endX, endY, h2X, h2Y, 4, true);
}

void DiceActivity::drawD20(int x, int y, int size, int value) {
  int radius = size / 2;

  // Draw 20-sided icosahedron outline (Hexagon shape) using optimized single-precision float math
  int px[6];
  int py[6];
  for (int i = 0; i < 6; i++) {
    float angle = (i * 60.0f - 90.0f) * (float)M_PI / 180.0f;
    px[i] = x + (int)(cosf(angle) * radius);
    py[i] = y + (int)(sinf(angle) * radius);
  }

  // Draw outer hexagon
  for (int i = 0; i < 6; i++) {
    renderer.drawLine(px[i], py[i], px[(i + 1) % 6], py[(i + 1) % 6], 2, true);
  }

  // Draw inner icosahedron lines representing triangles
  renderer.drawLine(px[0], py[0], px[3], py[3], 1, true);  // vertical line
  renderer.drawLine(px[1], py[1], px[4], py[4], 1, true);
  renderer.drawLine(px[2], py[2], px[5], py[5], 1, true);

  // Draw center shield for number (larger radius)
  drawCircle(renderer, x, y, 28, 1, false);
  fillCircle(renderer, x, y, 27, true);

  std::string txt = std::to_string(value);
  int tW = renderer.getTextWidth(NOTOSANS_18_FONT_ID, txt.c_str());
  int tH = renderer.getTextHeight(NOTOSANS_18_FONT_ID);
  // Adjusted Y offset for visual centering
  renderer.drawText(NOTOSANS_18_FONT_ID, x - tW / 2, y - tH / 2 - 2, txt.c_str(), false, EpdFontFamily::BOLD);
}

void DiceActivity::drawMagic8(int x, int y, int size, int responseIndex) {
  int radius = size / 2;
  fillCircle(renderer, x, y, radius, true);
  fillCircle(renderer, x, y, radius - 6, true);

  // Draw inner white triangle/circle for text
  fillCircle(renderer, x, y, radius - 30, false);
  drawCircle(renderer, x, y, radius - 30, 2, true);

  const char* responses[] = {"It is\ncertain",
                             "It is\ndecidedly\nso",
                             "Without a\ndoubt",
                             "Yes\ndefinitely",
                             "You may\nrely on it",
                             "As I see it,\nyes",
                             "Most\nlikely",
                             "Outlook\ngood",
                             "Yes",
                             "Signs point\nto yes",
                             "Reply hazy,\ntry again",
                             "Ask again\nlater",
                             "Better not\ntell you\nnow",
                             "Cannot\npredict\nnow",
                             "Concentrate\nand ask\nagain",
                             "Don't\ncount on it",
                             "My reply\nis no",
                             "My sources\nsay no",
                             "Outlook\nnot so good",
                             "Very\ndoubtful"};

  std::string txt = responses[responseIndex];

  // Basic line breaking rendering
  int yOffset = y - 20;  // Start roughly near the top of the inner circle
  size_t start = 0;
  size_t end = txt.find('\n');
  while (end != std::string::npos) {
    std::string line = txt.substr(start, end - start);
    int tW = renderer.getTextWidth(SMALL_FONT_ID, line.c_str());
    renderer.drawText(SMALL_FONT_ID, x - tW / 2, yOffset, line.c_str(), true);
    yOffset += renderer.getTextHeight(SMALL_FONT_ID) + 2;
    start = end + 1;
    end = txt.find('\n', start);
  }
  std::string line = txt.substr(start);
  int tW = renderer.getTextWidth(SMALL_FONT_ID, line.c_str());
  renderer.drawText(SMALL_FONT_ID, x - tW / 2, yOffset, line.c_str(), true);
}

void DiceActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Dice & Tools");

  // Draw tabs bar
  const int tabsY = metrics.topPadding + metrics.headerHeight + 20;
  const int tabW = (pageWidth - 40) / 4;
  const std::string tabNames[4] = {"D6", "Arrow", "D20", "8-Ball"};

  for (int i = 0; i < 4; i++) {
    bool active = (currentMode == static_cast<DiceMode>(i));
    int tx = 20 + i * tabW;
    renderer.drawRoundedRect(tx + 2, tabsY, tabW - 4, 30, 1, 5, true);
    if (active) {
      renderer.fillRoundedRect(tx + 2, tabsY, tabW - 4, 30, 5, Color::Black);
    }
    renderer.drawText(SMALL_FONT_ID, tx + (tabW - renderer.getTextWidth(SMALL_FONT_ID, tabNames[i].c_str())) / 2,
                      tabsY + 7, tabNames[i].c_str(), !active);
  }

  // Draw main card
  const int cardW = pageWidth - 40;
  const int cardH = pageHeight - tabsY - metrics.buttonHintsHeight - 60;
  const int cardX = 20;
  const int cardY = tabsY + 45;

  renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 1, 10, true);

  int cx = cardX + cardW / 2;
  int cy = cardY + cardH / 2 - 10;

  switch (currentMode) {
    case DiceMode::D6:
      drawD6(cx, cy, 120, lastRollD6);
      renderer.drawCenteredText(UI_12_FONT_ID, cardY + cardH - 45, "Press Confirm to Roll", true,
                                EpdFontFamily::REGULAR);
      break;
    case DiceMode::Arrow:
      drawArrow(cx, cy, 150, lastRollArrowAngle);
      renderer.drawCenteredText(UI_12_FONT_ID, cardY + cardH - 45, "Press Confirm to Spin Arrow", true,
                                EpdFontFamily::REGULAR);
      break;
    case DiceMode::D20:
      drawD20(cx, cy, 150, lastRollD20);
      renderer.drawCenteredText(UI_12_FONT_ID, cardY + cardH - 45, "Press Confirm to Roll D20", true,
                                EpdFontFamily::REGULAR);
      break;
    case DiceMode::Magic8:
      drawMagic8(cx, cy, 150, lastRollMagic8);
      renderer.drawCenteredText(UI_12_FONT_ID, cardY + cardH - 45, "Press Confirm to Shake 8-Ball", true,
                                EpdFontFamily::REGULAR);
      break;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
