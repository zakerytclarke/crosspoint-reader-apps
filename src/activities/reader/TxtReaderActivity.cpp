#include "TxtReaderActivity.h"

#include <algorithm>
#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Utf8.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "SdCardFontSystem.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 3;          // Increment when cache format changes
}  // namespace

void TxtReaderActivity::onEnter() {
  Activity::onEnter();

  if (!txt) {
    return;
  }

  sdFontSystem.ensureLoaded(renderer);
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  txt->setupCacheDir();

  // Save current txt as last opened file and add to recent books
  auto filePath = txt->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "");

  // Trigger first update
  requestUpdate();
}

void TxtReaderActivity::onExit() {
  Activity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  pageOffsets.clear();
  currentPageLines.clear();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  txt.reset();
}

void TxtReaderActivity::loop() {
  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    activityManager.goToFileBrowser(txt ? txt->getPath() : "");
    return;
  }

  // Short press BACK goes directly to home (or pops to caller if it's a Wikipedia/RSS downloaded link)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    if (txt && (txt->getPath().rfind("/apps/wikipedia/", 0) == 0 ||
                txt->getPath().rfind("/apps/websites/", 0) == 0 ||
                txt->getPath().rfind("/websites/", 0) == 0 ||
                txt->getPath().rfind("/apps/webbrowser/", 0) == 0 ||
                txt->getPath().rfind("/apps/rss/", 0) == 0)) {
      activityManager.popActivity();
    } else {
      onGoHome();
    }
    return;
  }

  const auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (prevTriggered && currentPage > 0) {
    currentPage--;
    requestUpdate();
  } else if (nextTriggered) {
    if (currentPage < static_cast<int>(pageOffsets.size()) - 1) {
      currentPage++;
      requestUpdate();
    }
  }
}

void TxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int ascender = renderer.getFontAscenderSize(cachedFontId);
  const int descender = std::abs(renderer.getFontDescenderSize(cachedFontId));
  const int lineNeed = ascender + descender;

  linesPerPage = viewportHeight / lineHeight;
  while (linesPerPage > 1 && (linesPerPage - 1) * lineHeight + lineNeed > viewportHeight) {
    linesPerPage--;
  }
  if (linesPerPage < 1) linesPerPage = 1;

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  if (!loadIndex()) {
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    renderer.displayBuffer(); // Actually push the popup to the screen
    buildIndex();
    saveIndex();
  }

  // Load saved progress
  loadProgress();

  initialized = true;
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  bool isHtml = FsHelpers::checkFileExtension(txt->getPath(), ".html") ||
                FsHelpers::checkFileExtension(txt->getPath(), ".htm");

  if (isHtml) {
    size_t currentOffset = offset;
    size_t bufferPos = 0;
    size_t bytesReadInChunk = 0;
    auto* buffer = static_cast<uint8_t*>(malloc(CHUNK_SIZE + 1));
    if (!buffer) {
      LOG_ERR("TRS", "Failed to allocate %zu bytes", CHUNK_SIZE);
      return false;
    }

    auto getChar = [&]() -> int {
      if (bufferPos >= bytesReadInChunk) {
        if (currentOffset >= fileSize) return -1;
        size_t toRead = std::min(CHUNK_SIZE, fileSize - currentOffset);
        if (!txt->readContent(buffer, currentOffset, toRead)) return -1;
        buffer[toRead] = '\0';
        
        if (renderer.isSdCardFont(cachedFontId)) {
          renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), 0x01);
        }
        
        bytesReadInChunk = toRead;
        currentOffset += toRead;
        bufferPos = 0;
      }
      return buffer[bufferPos++];
    };

    auto getFilePos = [&]() -> size_t {
      return currentOffset - bytesReadInChunk + bufferPos - 1;
    };

    std::string cleanLine = "";
    std::vector<size_t> cleanLineOffsets;
    char marker = '\7';
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
    int indent = 0;
    bool lastWasSpace = true;
    bool insideBlockquote = false;

    int c;
    while (static_cast<int>(outLines.size()) < linesPerPage && (c = getChar()) != -1) {
      if (c == '<') {
        int c1 = getChar();
        if (c1 == -1) break;

        if (c1 == '!') {
          int c2 = getChar();
          int c3 = getChar();
          if (c2 == '-' && c3 == '-') {
            // HTML Comment
            int dashCount = 0;
            int cm;
            while ((cm = getChar()) != -1) {
              if (cm == '-') dashCount++;
              else if (cm == '>' && dashCount >= 2) break;
              else dashCount = 0;
            }
            continue;
          } else {
             // Not a comment, ignore DOCTYPE etc.
             int cm;
             while ((cm = getChar()) != -1 && cm != '>') {}
             continue;
          }
        }

        std::string tagContent;
        tagContent += static_cast<char>(c1);
        int tc;
        while ((tc = getChar()) != -1 && tc != '>') {
          if (tagContent.length() < 256) tagContent += static_cast<char>(tc);
        }

        std::string tagName = "";
        size_t firstSpace = tagContent.find_first_of(" \t\r\n/");
        if (firstSpace != std::string::npos) {
          tagName = tagContent.substr(0, firstSpace);
        } else {
          tagName = tagContent;
        }
        std::transform(tagName.begin(), tagName.end(), tagName.begin(), ::tolower);

        bool isClosing = (!tagContent.empty() && tagContent[0] == '/');
        if (isClosing && !tagName.empty() && tagName[0] == '/') {
          tagName = tagName.substr(1);
        }

        // Fast path for skipping content tags
        if (!isClosing && (tagName == "style" || tagName == "script" || tagName == "head" || tagName == "svg" || tagName == "nav" || tagName == "noscript" || tagName == "iframe")) {
          std::string closeTag = "</" + tagName + ">";
          size_t matchIdx = 0;
          int sc;
          while ((sc = getChar()) != -1) {
            if (tolower(sc) == closeTag[matchIdx]) {
              matchIdx++;
              if (matchIdx == closeTag.length()) break;
            } else {
              if (tolower(sc) == closeTag[0]) matchIdx = 1;
              else matchIdx = 0;
            }
          }
          continue;
        }

        // Structural tags
        if (tagName == "h1") {
          if (!cleanLine.empty()) { 
             size_t consumed = wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
             if (consumed < cleanLine.length()) { nextOffset = cleanLineOffsets[consumed]; free(buffer); return true; }
             cleanLine.clear(); cleanLineOffsets.clear();
          }
          marker = isClosing ? '\7' : '\1';
          style = isClosing ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD;
          indent = 0;
        } else if (tagName == "h2") {
          if (!cleanLine.empty()) { 
             size_t consumed = wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
             if (consumed < cleanLine.length()) { nextOffset = cleanLineOffsets[consumed]; free(buffer); return true; }
             cleanLine.clear(); cleanLineOffsets.clear();
          }
          marker = isClosing ? '\7' : '\2';
          style = isClosing ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD;
          indent = 0;
        } else if (tagName == "h3") {
          if (!cleanLine.empty()) { 
             size_t consumed = wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
             if (consumed < cleanLine.length()) { nextOffset = cleanLineOffsets[consumed]; free(buffer); return true; }
             cleanLine.clear(); cleanLineOffsets.clear();
          }
          marker = isClosing ? '\7' : '\3';
          style = isClosing ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD;
          indent = 0;
        } else if (tagName == "blockquote") {
          if (!cleanLine.empty()) { 
             size_t consumed = wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
             if (consumed < cleanLine.length()) { nextOffset = cleanLineOffsets[consumed]; free(buffer); return true; }
             cleanLine.clear(); cleanLineOffsets.clear();
          }
          insideBlockquote = !isClosing;
          marker = isClosing ? '\7' : '\4';
          style = isClosing ? EpdFontFamily::REGULAR : EpdFontFamily::ITALIC;
          indent = isClosing ? 0 : 15;
        } else if (tagName == "li") {
          if (!cleanLine.empty()) { 
             size_t consumed = wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
             if (consumed < cleanLine.length()) { nextOffset = cleanLineOffsets[consumed]; free(buffer); return true; }
             cleanLine.clear(); cleanLineOffsets.clear();
          }
          if (!isClosing) { marker = '\5'; indent = 15; cleanLine = "•  "; cleanLineOffsets.insert(cleanLineOffsets.end(), 3, getFilePos()); } 
          else { marker = '\7'; indent = 0; }
        } else if (tagName == "hr") {
          if (!cleanLine.empty()) { 
             size_t consumed = wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
             if (consumed < cleanLine.length()) { nextOffset = cleanLineOffsets[consumed]; free(buffer); return true; }
             cleanLine.clear(); cleanLineOffsets.clear();
          }
          std::string hrStr = ""; hrStr += '\6'; outLines.push_back(hrStr);
        } else if (tagName == "p" || tagName == "div" || tagName == "br") {
          if (!cleanLine.empty()) { 
             size_t consumed = wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
             if (consumed < cleanLine.length()) { nextOffset = cleanLineOffsets[consumed]; free(buffer); return true; }
             cleanLine.clear(); cleanLineOffsets.clear();
          }
          if (insideBlockquote) { marker = '\4'; style = EpdFontFamily::ITALIC; indent = 15; } 
          else { marker = '\7'; style = EpdFontFamily::REGULAR; indent = 0; }
        }

        lastWasSpace = true;
        continue;
      }

      // Handle Entities
      if (c == '&') {
        size_t entityStartPos = getFilePos();
        std::string entity;
        int ec;
        bool foundSemi = false;
        int ahead[10];
        int aheadCount = 0;
        while (aheadCount < 10 && (ec = getChar()) != -1) {
          ahead[aheadCount++] = ec;
          if (ec == ';') { foundSemi = true; break; }
        }

        if (foundSemi) {
          for (int k = 0; k < aheadCount - 1; k++) entity += static_cast<char>(ahead[k]);
          int code = 0;
          bool decoded = false;
          
          if (!entity.empty() && entity[0] == '#') {
            decoded = true;
            if (entity.length() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
              for (size_t j = 2; j < entity.length(); j++) {
                char ch = entity[j];
                if (ch >= '0' && ch <= '9') code = code * 16 + (ch - '0');
                else if (ch >= 'a' && ch <= 'f') code = code * 16 + (ch - 'a' + 10);
                else if (ch >= 'A' && ch <= 'F') code = code * 16 + (ch - 'A' + 10);
              }
            } else {
              for (size_t j = 1; j < entity.length(); j++) {
                char ch = entity[j];
                if (ch >= '0' && ch <= '9') code = code * 10 + (ch - '0');
              }
            }
          } else {
            if (entity == "nbsp") { code = 32; decoded = true; }
            else if (entity == "amp") { code = 38; decoded = true; }
            else if (entity == "lt") { code = 60; decoded = true; }
            else if (entity == "gt") { code = 62; decoded = true; }
            else if (entity == "quot") { code = 34; decoded = true; }
            else if (entity == "apos" || entity == "#39") { code = 39; decoded = true; }
            else if (entity == "ldquo") { code = 8220; decoded = true; }
            else if (entity == "rdquo") { code = 8221; decoded = true; }
            else if (entity == "lsquo") { code = 8216; decoded = true; }
            else if (entity == "rsquo") { code = 8217; decoded = true; }
            else if (entity == "ndash") { code = 8211; decoded = true; }
            else if (entity == "mdash") { code = 8212; decoded = true; }
            else if (entity == "hellip") { code = 8230; decoded = true; }
            else if (entity == "euro") { code = 8364; decoded = true; }
            else if (entity == "copy") { code = 169; decoded = true; }
            else if (entity == "reg") { code = 174; decoded = true; }
            else if (entity == "trade") { code = 8482; decoded = true; }
          }
          
          if (decoded && code > 0) {
            std::string utf8_char = "";
            if (code <= 0x7F) utf8_char += static_cast<char>(code);
            else if (code <= 0x7FF) { utf8_char += static_cast<char>(0xC0 | ((code >> 6) & 0x1F)); utf8_char += static_cast<char>(0x80 | (code & 0x3F)); }
            else if (code <= 0xFFFF) { utf8_char += static_cast<char>(0xE0 | ((code >> 12) & 0x0F)); utf8_char += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); utf8_char += static_cast<char>(0x80 | (code & 0x3F)); }
            else if (code <= 0x10FFFF) { utf8_char += static_cast<char>(0xF0 | ((code >> 18) & 0x07)); utf8_char += static_cast<char>(0x80 | ((code >> 12) & 0x3F)); utf8_char += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); utf8_char += static_cast<char>(0x80 | (code & 0x3F)); }
            
            for (char uc : utf8_char) {
              if (isspace(uc)) {
                if (!lastWasSpace) { cleanLine += ' '; cleanLineOffsets.push_back(entityStartPos); lastWasSpace = true; }
              } else {
                cleanLine += uc; cleanLineOffsets.push_back(entityStartPos); lastWasSpace = false;
              }
            }
            continue;
          }
        }
        
        cleanLine += '&'; cleanLineOffsets.push_back(entityStartPos); lastWasSpace = false;
        for (int k = 0; k < aheadCount; k++) {
           char ac = static_cast<char>(ahead[k]);
           size_t acPos = entityStartPos + 1 + k;
           if (isspace(ac)) {
             if (!lastWasSpace) { cleanLine += ' '; cleanLineOffsets.push_back(acPos); lastWasSpace = true; }
           } else {
             cleanLine += ac; cleanLineOffsets.push_back(acPos); lastWasSpace = false;
           }
        }
        continue;
      }

      if (isspace(c)) {
        if (!lastWasSpace) { cleanLine += ' '; cleanLineOffsets.push_back(getFilePos()); lastWasSpace = true; }
      } else {
        cleanLine += static_cast<char>(c); cleanLineOffsets.push_back(getFilePos()); lastWasSpace = false;
      }
    }

    if (!cleanLine.empty()) {
      if (static_cast<int>(outLines.size()) < linesPerPage) {
        size_t consumed = wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
        if (consumed < cleanLine.length()) {
          nextOffset = cleanLineOffsets[consumed];
          free(buffer);
          return true;
        }
      } else {
        nextOffset = cleanLineOffsets[0];
        free(buffer);
        return true;
      }
    }

    nextOffset = currentOffset - bytesReadInChunk + bufferPos;
    if (nextOffset > fileSize) nextOffset = fileSize;
    free(buffer);
    return !outLines.empty();
  }

  // Fallback for normal text / Markdown files
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), 0x01);
  }

  size_t pos = 0;
  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      break;
    }

    size_t lineContentLen = lineEnd - pos;
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);
    size_t lineBytePos = 0;
    bool isMarkdown = FsHelpers::hasMarkdownExtension(txt->getPath());
    char marker = '\7'; // Default: normal text
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
    int indent = 0;
    std::string cleanLine = line;

    bool isContinuation = false;
    if (isMarkdown && pos == 0 && offset > 0) {
      uint8_t prevChar;
      if (txt->readContent(&prevChar, offset - 1, 1) && prevChar != '\n' && prevChar != '\r') {
        isContinuation = true;
      }
    }

    if (isContinuation) {
      // Find start of current line by scanning backwards
      size_t lineStartOffset = offset;
      size_t scanPos = offset;
      bool foundNewline = false;
      uint8_t scanBuf[256];
      while (scanPos > 0 && !foundNewline) {
        size_t toRead = std::min(scanPos, sizeof(scanBuf));
        size_t readOffset = scanPos - toRead;
        if (!txt->readContent(scanBuf, readOffset, toRead)) {
          break;
        }
        for (size_t i = toRead; i > 0; i--) {
          if (scanBuf[i - 1] == '\n' || scanBuf[i - 1] == '\r') {
            lineStartOffset = readOffset + i;
            foundNewline = true;
            break;
          }
        }
        if (!foundNewline) {
          scanPos = readOffset;
        }
      }
      if (!foundNewline) {
        lineStartOffset = 0;
      }

      // Skip any leading newline characters
      uint8_t tempChar;
      while (lineStartOffset < offset) {
        if (txt->readContent(&tempChar, lineStartOffset, 1) && (tempChar == '\n' || tempChar == '\r')) {
          lineStartOffset++;
        } else {
          break;
        }
      }

      if (lineStartOffset == offset) {
        isContinuation = false;
      } else {
        // Read the prefix of this line to determine formatting style
        char prefixBuf[16];
        size_t prefixLen = std::min<size_t>(15, txt->getFileSize() - lineStartOffset);
        memset(prefixBuf, 0, sizeof(prefixBuf));
        if (txt->readContent(reinterpret_cast<uint8_t*>(prefixBuf), lineStartOffset, prefixLen)) {
          prefixBuf[prefixLen] = '\0';
          std::string prefixStr(prefixBuf);
          size_t newlinePos = prefixStr.find_first_of("\r\n");
          if (newlinePos != std::string::npos) {
            prefixStr = prefixStr.substr(0, newlinePos);
          }

          if (prefixStr.rfind("# ", 0) == 0) {
            marker = '\1';
            style = EpdFontFamily::BOLD;
            indent = 0;
          } else if (prefixStr.rfind("## ", 0) == 0) {
            marker = '\2';
            style = EpdFontFamily::BOLD;
            indent = 0;
          } else if (prefixStr.rfind("### ", 0) == 0) {
            marker = '\3';
            style = EpdFontFamily::BOLD;
            indent = 0;
          } else if (prefixStr.rfind("> ", 0) == 0) {
            marker = '\4';
            style = EpdFontFamily::ITALIC;
            indent = 15;
          } else if (prefixStr.rfind("- ", 0) == 0 || prefixStr.rfind("* ", 0) == 0) {
            marker = '\5';
            indent = 15;
          }
        }
      }
    }

    if (!isContinuation && isMarkdown) {
      if (line.rfind("# ", 0) == 0) {
        marker = '\1'; // H1
        style = EpdFontFamily::BOLD;
        cleanLine = line.substr(2);
      } else if (line.rfind("## ", 0) == 0) {
        marker = '\2'; // H2
        style = EpdFontFamily::BOLD;
        cleanLine = line.substr(3);
      } else if (line.rfind("### ", 0) == 0) {
        marker = '\3'; // H3
        style = EpdFontFamily::BOLD;
        cleanLine = line.substr(4);
      } else if (line.rfind("> ", 0) == 0) {
        marker = '\4'; // Blockquote
        style = EpdFontFamily::ITALIC;
        indent = 15;
        cleanLine = line.substr(2);
      } else if (line.rfind("- ", 0) == 0) {
        marker = '\5'; // Bullet point
        indent = 15;
        cleanLine = "•  " + line.substr(2);
      } else if (line.rfind("* ", 0) == 0) {
        marker = '\5'; // Bullet point
        indent = 15;
        cleanLine = "•  " + line.substr(2);
      } else if (line == "---" || line == "***" || line == "___") {
        marker = '\6'; // Horizontal rule
        cleanLine = "";
      }
    }

    bool firstSegment = !isContinuation;
    do {
      if (cleanLine.empty() && marker != '\6') {
        std::string wrapped = "";
        wrapped += marker;
        outLines.push_back(wrapped);
        break;
      }

      if (marker == '\6') {
        std::string wrapped = "";
        wrapped += marker;
        outLines.push_back(wrapped);
        break;
      }

      int currentIndent = firstSegment ? indent : (marker == '\5' ? 15 : indent);
      int maxW = viewportWidth - currentIndent;

      int lineWidth = renderer.getTextAdvanceX(cachedFontId, cleanLine.c_str(), style);

      if (lineWidth <= maxW) {
        std::string wrapped = "";
        wrapped += (firstSegment ? marker : (marker == '\5' ? '\4' : marker));
        wrapped += cleanLine;
        outLines.push_back(wrapped);
        lineBytePos = displayLen;
        cleanLine.clear();
        break;
      }

      size_t low = 0;
      size_t high = cleanLine.length();
      size_t breakPos = 0;

      while (low <= high) {
        size_t mid = low + (high - low) / 2;
        while (mid > low && (cleanLine[mid] & 0xC0) == 0x80) {
          mid--;
        }

        std::string testStr = cleanLine.substr(0, mid);
        int testWidth = renderer.getTextAdvanceX(cachedFontId, testStr.c_str(), style);

        if (testWidth <= maxW) {
          breakPos = mid;
          low = mid + 1;
          while (low <= high && low < cleanLine.length() && (cleanLine[low] & 0xC0) == 0x80) {
            low++;
          }
        } else {
          if (mid == 0) {
            breakPos = 0;
            break;
          }
          high = mid - 1;
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
        while (breakPos < cleanLine.length() && (cleanLine[breakPos] & 0xC0) == 0x80) {
          breakPos++;
        }
      }

      if (breakPos < cleanLine.length()) {
        size_t spacePos = cleanLine.rfind(' ', breakPos);
        if (spacePos != std::string::npos && spacePos > 0) {
          if (spacePos > breakPos - 20 || spacePos > cleanLine.length() / 2) {
            breakPos = spacePos;
          }
        }
      }

      std::string wrapped = "";
      wrapped += (firstSegment ? marker : (marker == '\5' ? '\4' : marker));
      wrapped += cleanLine.substr(0, breakPos);
      outLines.push_back(wrapped);

      size_t skipChars = breakPos;
      if (breakPos < cleanLine.length() && cleanLine[breakPos] == ' ') {
        skipChars++;
      }

      size_t sourceConsumed = skipChars;
      if (firstSegment) {
        if (marker == '\1') {
          sourceConsumed = skipChars + 2;
        } else if (marker == '\2') {
          sourceConsumed = skipChars + 3;
        } else if (marker == '\3') {
          sourceConsumed = skipChars + 4;
        } else if (marker == '\4') {
          sourceConsumed = skipChars + 2;
        } else if (marker == '\5') {
          if (skipChars <= 5) {
            sourceConsumed = 2;
          } else {
            sourceConsumed = 2 + (skipChars - 5);
          }
        }
      }
      lineBytePos += sourceConsumed;

      cleanLine = cleanLine.substr(skipChars);
      firstSegment = false;
    } while (!cleanLine.empty() && static_cast<int>(outLines.size()) < linesPerPage);

    if (cleanLine.empty()) {
      pos = lineEnd + 1;
    } else {
      pos = pos + lineBytePos;
      break;
    }
  }

  if (pos == 0 && !outLines.empty()) {
    pos = 1;
  }

  nextOffset = offset + pos;
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);
  return !outLines.empty();
}

void TxtReaderActivity::render(RenderLock&&) {
  if (!txt) {
    return;
  }

  // Check if settings changed since initialization
  if (initialized && (cachedFontId != SETTINGS.getReaderFontId() ||
                       cachedScreenMargin != SETTINGS.screenMargin ||
                       cachedParagraphAlignment != SETTINGS.paragraphAlignment)) {
    initialized = false;
  }

  // Initialize reader if not done
  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= static_cast<int>(pageOffsets.size())) currentPage = pageOffsets.size() - 1;

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage();

  // Save progress
  saveProgress();
}

void TxtReaderActivity::renderPage() {
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    bool isFormatted = FsHelpers::hasMarkdownExtension(txt->getPath()) ||
                       FsHelpers::checkFileExtension(txt->getPath(), ".html") ||
                       FsHelpers::checkFileExtension(txt->getPath(), ".htm");

    for (const auto& rawLine : currentPageLines) {
      if (rawLine.empty()) {
        y += lineHeight;
        continue;
      }

      std::string line = rawLine;
      EpdFontFamily::Style style = EpdFontFamily::REGULAR;
      int indent = 0;
      bool isH1 = false;
      bool isHR = false;
      bool isQuote = false;

      if (isFormatted) {
        char type = line[0];
        line = line.substr(1);

        if (type == '\1') { // H1
          style = EpdFontFamily::BOLD;
          isH1 = true;
        } else if (type == '\2' || type == '\3') { // H2, H3
          style = EpdFontFamily::BOLD;
        } else if (type == '\4') { // Quote
          style = EpdFontFamily::ITALIC;
          indent = 15;
          isQuote = true;
        } else if (type == '\5') { // Bullet
          indent = 15;
        } else if (type == '\6') { // HR
          isHR = true;
        }
      }

      if (isHR) {
        int startX = cachedOrientedMarginLeft + 10;
        int endX = cachedOrientedMarginLeft + viewportWidth - 10;
        int lineY = y + lineHeight / 2;
        renderer.drawLine(startX, lineY, endX, lineY, true);
      } else if (!line.empty()) {
        int x = cachedOrientedMarginLeft + indent;

        const bool lineIsRtl = BidiUtils::startsWithRtl(line.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }

        // Apply text alignment
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            // x already set
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            int textWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), style);
            x = cachedOrientedMarginLeft + indent + (contentWidth - indent - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            int textWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), style);
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            break;
        }

        if (isQuote) {
          int barX = cachedOrientedMarginLeft + 5;
          renderer.fillRect(barX, y, 2, lineHeight, true);
        }

        renderer.drawText(cachedFontId, x, y, line.c_str(), true, style);

        if (isH1) {
          int lineY = y + lineHeight - 2;
          renderer.drawLine(cachedOrientedMarginLeft, lineY, cachedOrientedMarginLeft + viewportWidth, lineY, true);
        }
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();  // scan pass — text accumulated, no drawing
  scope.endScanAndPrewarm();

  // BW rendering
  renderLines();
  renderStatusBar();

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  }
  // scope destructor clears font cache via FontCacheManager
}

void TxtReaderActivity::renderStatusBar() const {
  const size_t fileSize = txt->getFileSize();
  const float progress = fileSize > 0 ? (pageOffsets[currentPage] * 100.0f) / fileSize : 0;
  std::string title;
  if (SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE) {
    title = txt->getTitle();
  }

  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title);
}

bool TxtReaderActivity::loadIndex() {
  HalFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/index.bin", f)) {
    uint32_t magic;
    uint8_t version;
    int fontId;
    uint8_t screenMargin;
    int top, right, bottom, left;

    if (f.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic)) == sizeof(magic) &&
        f.read(&version, sizeof(version)) == sizeof(version) &&
        f.read(reinterpret_cast<uint8_t*>(&fontId), sizeof(fontId)) == sizeof(fontId) &&
        f.read(&screenMargin, sizeof(screenMargin)) == sizeof(screenMargin) &&
        f.read(reinterpret_cast<uint8_t*>(&top), sizeof(top)) == sizeof(top) &&
        f.read(reinterpret_cast<uint8_t*>(&right), sizeof(right)) == sizeof(right) &&
        f.read(reinterpret_cast<uint8_t*>(&bottom), sizeof(bottom)) == sizeof(bottom) &&
        f.read(reinterpret_cast<uint8_t*>(&left), sizeof(left)) == sizeof(left)) {

      if (magic == CACHE_MAGIC && version == CACHE_VERSION &&
          fontId == cachedFontId && screenMargin == cachedScreenMargin &&
          top == cachedOrientedMarginTop && right == cachedOrientedMarginRight &&
          bottom == cachedOrientedMarginBottom && left == cachedOrientedMarginLeft) {
        
        uint32_t count;
        if (f.read(reinterpret_cast<uint8_t*>(&count), sizeof(count)) == sizeof(count)) {
          pageOffsets.resize(count);
          if (f.read(reinterpret_cast<uint8_t*>(pageOffsets.data()), count * sizeof(size_t)) == count * sizeof(size_t)) {
            f.close();
            totalPages = count;
            return true;
          }
        }
      }
    }
    f.close();
  }
  return false;
}

void TxtReaderActivity::buildIndex() {
  pageOffsets.clear();
  size_t currentOffset = 0;
  size_t fileSize = txt->getFileSize();
  
  while (currentOffset < fileSize) {
    pageOffsets.push_back(currentOffset);
    std::vector<std::string> tempLines;
    size_t nextOffset = currentOffset;
    if (loadPageAtOffset(currentOffset, tempLines, nextOffset) && nextOffset > currentOffset) {
      currentOffset = nextOffset;
    } else {
      break;
    }
  }

  if (pageOffsets.empty()) {
    pageOffsets.push_back(0);
  }
  totalPages = pageOffsets.size();
}

void TxtReaderActivity::saveIndex() const {
  HalFile f;
  if (Storage.openFileForWrite("TRS", txt->getCachePath() + "/index.bin", f)) {
    uint32_t magic = CACHE_MAGIC;
    uint8_t version = CACHE_VERSION;
    f.write(reinterpret_cast<const uint8_t*>(&magic), sizeof(magic));
    f.write(&version, sizeof(version));
    f.write(reinterpret_cast<const uint8_t*>(&cachedFontId), sizeof(cachedFontId));
    f.write(&cachedScreenMargin, sizeof(cachedScreenMargin));
    f.write(reinterpret_cast<const uint8_t*>(&cachedOrientedMarginTop), sizeof(cachedOrientedMarginTop));
    f.write(reinterpret_cast<const uint8_t*>(&cachedOrientedMarginRight), sizeof(cachedOrientedMarginRight));
    f.write(reinterpret_cast<const uint8_t*>(&cachedOrientedMarginBottom), sizeof(cachedOrientedMarginBottom));
    f.write(reinterpret_cast<const uint8_t*>(&cachedOrientedMarginLeft), sizeof(cachedOrientedMarginLeft));
    
    uint32_t count = pageOffsets.size();
    f.write(reinterpret_cast<const uint8_t*>(&count), sizeof(count));
    f.write(reinterpret_cast<const uint8_t*>(pageOffsets.data()), count * sizeof(size_t));
    f.close();
  }
}

void TxtReaderActivity::saveProgress() const {
  HalFile f;
  if (Storage.openFileForWrite("TRS", txt->getCachePath() + "/progress.bin", f)) {
    size_t offset = pageOffsets[currentPage];
    uint8_t data[4];
    data[0] = offset & 0xFF;
    data[1] = (offset >> 8) & 0xFF;
    data[2] = (offset >> 16) & 0xFF;
    data[3] = (offset >> 24) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void TxtReaderActivity::loadProgress() {
  size_t savedOffset = 0;
  HalFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      savedOffset = data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24);
    }
    f.close();
  }

  currentPage = 0;
  if (savedOffset > 0 && !pageOffsets.empty()) {
    for (size_t i = 0; i < pageOffsets.size(); ++i) {
      if (pageOffsets[i] == savedOffset) {
        currentPage = i;
        break;
      } else if (pageOffsets[i] > savedOffset) {
        if (i > 0) currentPage = i - 1;
        break;
      }
    }
  }
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;

  const size_t fileSize = txt ? txt->getFileSize() : 0;
  int estimatedTotalPages = 1;
  if (currentPage < static_cast<int>(pageOffsets.size()) && pageOffsets[currentPage] > 0) {
    estimatedTotalPages = (fileSize * (currentPage + 1)) / pageOffsets[currentPage];
  } else {
    estimatedTotalPages = fileSize / 1500;
  }
  if (estimatedTotalPages < currentPage + 1) {
    estimatedTotalPages = currentPage + 1;
  }

  info.totalPages = estimatedTotalPages;
  info.progressPercent = estimatedTotalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / estimatedTotalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}

size_t TxtReaderActivity::wrapAndPushHtmlLine(const std::string& line, char marker, EpdFontFamily::Style style, int indent, std::vector<std::string>& outLines) {
  std::string cleanLine = line;
  bool firstSegment = true;
  size_t charsConsumed = 0;

  while (!cleanLine.empty() && static_cast<int>(outLines.size()) < linesPerPage) {
    int currentIndent = firstSegment ? indent : (marker == '\5' ? 15 : indent);
    int maxW = viewportWidth - currentIndent;

    int lineWidth = renderer.getTextAdvanceX(cachedFontId, cleanLine.c_str(), style);

    if (lineWidth <= maxW) {
      std::string wrapped = "";
      wrapped += (firstSegment ? marker : (marker == '\5' ? '\4' : marker));
      wrapped += cleanLine;
      outLines.push_back(wrapped);
      charsConsumed += cleanLine.length();
      break;
    }

    // Find break point using binary search
    size_t low = 0;
    size_t high = cleanLine.length();
    size_t breakPos = 0;

    while (low <= high) {
      size_t mid = low + (high - low) / 2;
      while (mid > low && (cleanLine[mid] & 0xC0) == 0x80) {
        mid--;
      }

      std::string testStr = cleanLine.substr(0, mid);
      int testWidth = renderer.getTextAdvanceX(cachedFontId, testStr.c_str(), style);

      if (testWidth <= maxW) {
        breakPos = mid;
        low = mid + 1;
        while (low <= high && low < cleanLine.length() && (cleanLine[low] & 0xC0) == 0x80) {
          low++;
        }
      } else {
        if (mid == 0) {
          breakPos = 0;
          break;
        }
        high = mid - 1;
      }
    }

    if (breakPos == 0) {
      breakPos = 1;
      while (breakPos < cleanLine.length() && (cleanLine[breakPos] & 0xC0) == 0x80) {
        breakPos++;
      }
    }

    if (breakPos < cleanLine.length()) {
      size_t spacePos = cleanLine.rfind(' ', breakPos);
      if (spacePos != std::string::npos && spacePos > 0) {
        if (spacePos > breakPos - 20 || spacePos > cleanLine.length() / 2) {
          breakPos = spacePos;
        }
      }
    }

    std::string wrapped = "";
    wrapped += (firstSegment ? marker : (marker == '\5' ? '\4' : marker));
    wrapped += cleanLine.substr(0, breakPos);
    outLines.push_back(wrapped);

    size_t skipChars = breakPos;
    if (breakPos < cleanLine.length() && cleanLine[breakPos] == ' ') {
      skipChars++;
    }

    charsConsumed += skipChars;
    cleanLine = cleanLine.substr(skipChars);
    firstSegment = false;
  }
  
  return charsConsumed;
}
