#include "DuckDuckGoActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/TxtReaderActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/DownloadWatchdog.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/WifiConnectHelper.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {

std::string sanitizeFilename(const std::string& title) {
  std::string filename = "";
  for (char c : title) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') {
      filename += c;
    }
  }
  return filename;
}

std::string urlEncode(const std::string& value) {
  std::string escaped = "";
  for (char c : value) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped += c;
    } else if (c == ' ') {
      escaped += "+";
    } else {
      char hex[4];
      sprintf(hex, "%%%02X", static_cast<unsigned char>(c));
      escaped += hex;
    }
  }
  return escaped;
}

std::string urlDecode(const std::string& SRC) {
  std::string ret;
  char ch;
  int ii;
  for (size_t i = 0; i < SRC.length(); i++) {
    if (SRC[i] == '%') {
      if (i + 2 < SRC.length()) {
        sscanf(SRC.substr(i + 1, 2).c_str(), "%x", &ii);
        ch = static_cast<char>(ii);
        ret += ch;
        i += 2;
      }
    } else if (SRC[i] == '+') {
      ret += ' ';
    } else {
      ret += SRC[i];
    }
  }
  return ret;
}

std::string decodeHtmlEntities(const std::string& input) {
  std::string output = "";
  output.reserve(input.length());
  for (size_t i = 0; i < input.length();) {
    if (input[i] == '&') {
      size_t semi = input.find(';', i);
      if (semi != std::string::npos && semi - i < 10) {
        std::string entity = input.substr(i + 1, semi - i - 1);
        if (entity == "amp") {
          output += '&';
          i = semi + 1;
          continue;
        }
        if (entity == "quot") {
          output += '"';
          i = semi + 1;
          continue;
        }
        if (entity == "lt") {
          output += '<';
          i = semi + 1;
          continue;
        }
        if (entity == "gt") {
          output += '>';
          i = semi + 1;
          continue;
        }
        if (entity == "apos") {
          output += '\'';
          i = semi + 1;
          continue;
        }
        if (entity == "nbsp") {
          output += ' ';
          i = semi + 1;
          continue;
        }
        if (entity == "#x27" || entity == "#39") {
          output += '\'';
          i = semi + 1;
          continue;
        }
      }
    }
    output += input[i];
    i++;
  }
  return output;
}

std::string cleanHtmlText(const std::string& input) {
  std::string decoded = decodeHtmlEntities(input);
  std::string output = "";
  bool lastWasSpace = true;
  for (char c : decoded) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!lastWasSpace) {
        output += ' ';
        lastWasSpace = true;
      }
    } else {
      output += c;
      lastWasSpace = false;
    }
  }
  if (!output.empty() && output.back() == ' ') {
    output.pop_back();
  }
  return output;
}

std::string extractHref(const std::string& attribs) {
  size_t pos = attribs.find("href");
  if (pos == std::string::npos) {
    pos = attribs.find("HREF");
  }
  if (pos == std::string::npos) {
    return "";
  }

  size_t eqPos = attribs.find('=', pos);
  if (eqPos == std::string::npos) {
    return "";
  }

  size_t valPos = eqPos + 1;
  while (valPos < attribs.length() && (attribs[valPos] == ' ' || attribs[valPos] == '\t')) {
    valPos++;
  }

  if (valPos >= attribs.length()) {
    return "";
  }

  char quote = attribs[valPos];
  if (quote == '"' || quote == '\'') {
    size_t endQuote = attribs.find(quote, valPos + 1);
    if (endQuote != std::string::npos) {
      return attribs.substr(valPos + 1, endQuote - (valPos + 1));
    }
    return attribs.substr(valPos + 1);
  } else {
    size_t space = attribs.find_first_of(" \t", valPos);
    if (space != std::string::npos) {
      return attribs.substr(valPos, space - valPos);
    }
    return attribs.substr(valPos);
  }
}

bool parseDuckDuckGoResults(const std::string& htmlPath, std::vector<DuckLink>& links) {
  HalFile file;
  if (!Storage.openFileForRead("DDG", htmlPath.c_str(), file)) {
    return false;
  }

  links.clear();

  enum class ParserState { Scanning, InTag, InTagAttribs, InAnchorContent };

  ParserState state = ParserState::Scanning;
  std::string currentTagName = "";
  std::string currentTagAttribs = "";
  std::string currentText = "";
  bool isAnchor = false;

  int c;
  while ((c = file.read()) != -1) {
    char ch = static_cast<char>(c);

    switch (state) {
      case ParserState::Scanning:
        if (ch == '<') {
          currentTagName.clear();
          currentTagAttribs.clear();
          isAnchor = false;
          state = ParserState::InTag;
        }
        break;

      case ParserState::InTag:
        if (ch == '>') {
          std::string lowerTag = currentTagName;
          std::transform(lowerTag.begin(), lowerTag.end(), lowerTag.begin(), ::tolower);
          if (lowerTag == "a") {
            isAnchor = true;
            currentText.clear();
            state = ParserState::InAnchorContent;
          } else {
            state = ParserState::Scanning;
          }
        } else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
          std::string lowerTag = currentTagName;
          std::transform(lowerTag.begin(), lowerTag.end(), lowerTag.begin(), ::tolower);
          if (lowerTag == "a") {
            isAnchor = true;
            currentTagAttribs.clear();
            state = ParserState::InTagAttribs;
          } else {
            state = ParserState::Scanning;
          }
        } else {
          currentTagName += ch;
        }
        break;

      case ParserState::InTagAttribs:
        if (ch == '>') {
          currentText.clear();
          state = ParserState::InAnchorContent;
        } else {
          currentTagAttribs += ch;
        }
        break;

      case ParserState::InAnchorContent:
        if (ch == '<') {
          int nextC = file.read();
          if (nextC == -1) break;
          char nextCh = static_cast<char>(nextC);
          if (nextCh == '/') {
            std::string closeTag = "";
            int tc;
            while ((tc = file.read()) != -1) {
              char tch = static_cast<char>(tc);
              if (tch == '>') {
                break;
              }
              closeTag += tch;
            }
            std::transform(closeTag.begin(), closeTag.end(), closeTag.begin(), ::tolower);
            if (closeTag == "a") {
              std::string href = extractHref(currentTagAttribs);
              std::string cleanText = cleanHtmlText(currentText);

              if (!href.empty() && !cleanText.empty()) {
                std::string targetUrl = href;
                size_t uddgPos = targetUrl.find("uddg=");
                if (uddgPos != std::string::npos) {
                  size_t start = uddgPos + 5;
                  size_t end = targetUrl.find('&', start);
                  std::string encoded =
                      (end == std::string::npos) ? targetUrl.substr(start) : targetUrl.substr(start, end - start);
                  targetUrl = urlDecode(encoded);
                }

                if (targetUrl.rfind("http", 0) == 0) {
                  bool dup = false;
                  for (const auto& l : links) {
                    if (l.url == targetUrl) {
                      dup = true;
                      break;
                    }
                  }
                  if (!dup) {
                    links.push_back({cleanText, targetUrl});
                  }
                }
              }
              state = ParserState::Scanning;
            } else {
              currentText += '<';
              currentText += '/';
              currentText += closeTag;
              currentText += '>';
            }
          } else {
            // Nested tag opening, skip until >
            int tc;
            while ((tc = file.read()) != -1) {
              char tch = static_cast<char>(tc);
              if (tch == '>') {
                break;
              }
            }
          }
        } else {
          currentText += ch;
        }
        break;
    }
  }

  file.close();
  return !links.empty();
}

std::string getWebsiteFilePath(const std::string& title) {
  std::string htmlPath = "/websites/" + sanitizeFilename(title) + ".html";
  if (Storage.exists(htmlPath.c_str())) {
    return htmlPath;
  }
  return "/websites/" + sanitizeFilename(title) + ".txt";
}

static void ddgFetchTaskFunc(void* param) {
  DuckDuckGoActivity* activity = static_cast<DuckDuckGoActivity*>(param);
  activity->runBackgroundFetch();
  vTaskDelete(nullptr);
}

}  // namespace

void DuckDuckGoActivity::onEnter() {
  Activity::onEnter();
  Storage.ensureDirectoryExists("/websites");
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/duckduckgo");

  errorMessage.clear();
  loadOfflineWebsitesList();
  state = DDGState::OfflineList;
  selectedIndex = 0;
  listScrollOffset = 0;

  fetchTaskHandle = nullptr;
  pendingUpdateSearch = false;
  backgroundFetchFailed = false;

  requestUpdate();
}

void DuckDuckGoActivity::onExit() {
  Activity::onExit();
  if (fetchTaskHandle != nullptr) {
    cancelFetch = true;
    int waitCount = 0;
    while (fetchTaskHandle != nullptr && waitCount < 500) {
      delay(10);
      waitCount++;
    }
    if (fetchTaskHandle != nullptr) {
      LOG_ERR("DDG", "Task failed to cancel! Forcing kill.");
      TaskHandle_t tempHandle = static_cast<TaskHandle_t>(fetchTaskHandle);
      fetchTaskHandle = nullptr;
      vTaskDelete(tempHandle);
    }
  }
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
  }
  if (wifiWasUsed) {
    silentRestart();
  }
}

void DuckDuckGoActivity::loadOfflineWebsitesList() {
  offlineWebsites.clear();
  std::vector<String> files = Storage.listFiles("/websites");
  for (const auto& file : files) {
    std::string filename = file.c_str();
    if (filename.length() > 5 && filename.substr(filename.length() - 5) == ".html") {
      offlineWebsites.push_back(filename.substr(0, filename.length() - 5));
    } else if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".txt") {
      offlineWebsites.push_back(filename.substr(0, filename.length() - 4));
    }
  }
  std::sort(offlineWebsites.begin(), offlineWebsites.end());
}

void DuckDuckGoActivity::performBackgroundSearch() {
  if (fetchTaskHandle != nullptr) return;
  state = DDGState::Loading;
  backgroundFetchFailed = false;
  pendingUpdateSearch = false;
  cancelFetch = false;
  requestUpdate();

  xTaskCreate(ddgFetchTaskFunc, "ddg_fetch", 8192, this, 5, (TaskHandle_t*)&fetchTaskHandle);
}

void DuckDuckGoActivity::runBackgroundFetch() {
  DownloadWatchdog::start(35000);
  errorMessage.clear();

  bool success = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (!WifiConnectHelper::waitForTimeSync()) {
      errorMessage = "Clock sync failed. HTTPS requests may fail.";
    }
    int retries = 3;
    while (retries-- > 0 && !cancelFetch) {
      success = fetchSearchData();
      if (success) {
        break;
      }
      if (retries > 0 && !cancelFetch) {
        delay(1500);
      }
    }
  } else {
    errorMessage = "WiFi disconnected during fetch.";
  }

  DownloadWatchdog::stop();

  if (DownloadWatchdog::gotTimeout) {
    LOG_ERR("DDG", "Background fetch timed out!");
    backgroundFetchFailed = true;
    errorMessage = "Request timed out.";
  } else if (!success) {
    backgroundFetchFailed = true;
    if (errorMessage.empty()) {
      errorMessage = "Search request failed.";
    }
  } else {
    backgroundFetchFailed = false;
    errorMessage.clear();
  }

  if (cancelFetch) {
    fetchTaskHandle = nullptr;
    return;
  }

  pendingUpdateSearch = true;
}

bool DuckDuckGoActivity::fetchSearchData() {
  std::string url = "https://html.duckduckgo.com/html/?q=" + urlEncode(searchQuery);
  const char* tempPath = "/apps/duckduckgo/search.tmp";

  std::string errorDetail;
  auto result = HttpDownloader::downloadToFile(url.c_str(), tempPath, nullptr, &cancelFetch, "", "", nullptr, nullptr,
                                               &errorDetail);
  if (result != HttpDownloader::OK) {
    errorMessage = "HTTP Error " + std::to_string(result) + ": " + (errorDetail.empty() ? "Unknown" : errorDetail);
    Storage.remove(tempPath);
    return false;
  }

  if (cancelFetch) {
    Storage.remove(tempPath);
    return false;
  }

  bool parseRes = parseDuckDuckGoResults(tempPath, bgSearchResults);
  Storage.remove(tempPath);
  return parseRes;
}

void DuckDuckGoActivity::downloadActivePost() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(searchResults.size())) {
    return;
  }

  std::string downloadUrl = searchResults[selectedIndex].url;
  std::string downloadTitle = searchResults[selectedIndex].title;

  GUI.drawPopup(renderer, "Downloading...");
  if (WifiConnectHelper::ensureWifiConnected()) {
    wifiWasUsed = true;
    std::string sanitized = sanitizeFilename(downloadTitle);
    if (sanitized.length() > 30) {
      sanitized = sanitized.substr(0, 30);
    }

    std::string tempPath = "/websites/temp_download.tmp";
    bool success = false;
    int retries = 3;

    while (retries > 0) {
      GUI.drawPopup(renderer, "Downloading...");
      auto result = HttpDownloader::downloadToFile(downloadUrl.c_str(), tempPath.c_str(), nullptr, nullptr, "", "");
      if (result == HttpDownloader::OK) {
        success = true;
        break;
      }
      retries--;
      if (retries > 0) {
        delay(1000);
      }
    }

    if (success) {
      std::string ext = ".html";
      std::string urlToCheck = downloadUrl;
      size_t queryPos = urlToCheck.find('?');
      if (queryPos != std::string::npos) {
        urlToCheck = urlToCheck.substr(0, queryPos);
      }
      if (urlToCheck.length() >= 4) {
        std::string urlExt = urlToCheck.substr(urlToCheck.length() - 4);
        for (char& c : urlExt) c = tolower(c);
        if (urlExt == ".txt") {
          ext = ".txt";
        }
      }

      std::string destPath = "/websites/" + sanitized + ext;
      {
        RenderLock lock;
        if (Storage.exists(destPath.c_str())) {
          Storage.remove(destPath.c_str());
        }
        Storage.rename(tempPath.c_str(), destPath.c_str());
      }

      activityManager.pushReader(destPath);
    } else {
      GUI.drawPopup(renderer, "Download failed!");
      delay(2000);
      requestUpdate();
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  } else {
    requestUpdate();
  }
}

void DuckDuckGoActivity::loop() {
  if (pendingUpdateSearch) {
    pendingUpdateSearch = false;
    fetchTaskHandle = nullptr;
    if (DownloadWatchdog::gotTimeout) {
      LOG_ERR("DDG", "Watchdog timeout! Crashing to home screen.");
      activityManager.goHome();
      return;
    }
    if (!backgroundFetchFailed) {
      searchResults = std::move(bgSearchResults);
      errorMessage.clear();
    } else {
      searchResults.clear();
    }
    selectedIndex = 0;
    listScrollOffset = 0;
    state = DDGState::SearchResults;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == DDGState::Loading) {
      if (fetchTaskHandle != nullptr) {
        cancelFetch = true;
        int waitCount = 0;
        while (fetchTaskHandle != nullptr && waitCount < 500) {
          delay(10);
          waitCount++;
        }
        if (fetchTaskHandle != nullptr) {
          LOG_ERR("DDG", "Task failed to cancel! Forcing kill.");
          TaskHandle_t tempHandle = static_cast<TaskHandle_t>(fetchTaskHandle);
          fetchTaskHandle = nullptr;
          vTaskDelete(tempHandle);
        }
      }
      if (!searchResults.empty()) {
        state = DDGState::SearchResults;
      } else {
        state = DDGState::OfflineList;
        loadOfflineWebsitesList();
      }
      requestUpdate();
    } else if (state == DDGState::SearchResults) {
      state = DDGState::OfflineList;
      loadOfflineWebsitesList();
      selectedIndex = 0;
      listScrollOffset = 0;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (state == DDGState::OfflineList) {
    int totalItems = static_cast<int>(offlineWebsites.size()) + 1;
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectedIndex = (selectedIndex - 1 + totalItems) % totalItems;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectedIndex = (selectedIndex + 1) % totalItems;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedIndex == 0) {
        auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "DuckDuckGo Search", "", 40);
        startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            auto keyboardResult = std::get_if<KeyboardResult>(&result.data);
            if (keyboardResult && !keyboardResult->text.empty()) {
              searchQuery = keyboardResult->text;
              state = DDGState::Loading;
              requestUpdate();
              ensureWifiConnected(
                  [this]() {
                    wifiWasUsed = true;
                    pendingUpdateSearch = false;
                    performBackgroundSearch();
                  },
                  [this]() {
                    state = DDGState::OfflineList;
                    requestUpdate();
                  });
            }
          } else {
            requestUpdate();
          }
        });
      } else {
        std::string title = offlineWebsites[selectedIndex - 1];
        std::string filepath = getWebsiteFilePath(title);
        auto txt = std::unique_ptr<Txt>(new Txt(filepath, "/.crosspoint"));
        if (txt && txt->load()) {
          activityManager.pushActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
        }
      }
    }
    return;
  }

  if (state == DDGState::SearchResults) {
    if (!errorMessage.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        state = DDGState::Loading;
        errorMessage.clear();
        requestUpdate();
        ensureWifiConnected(
            [this]() {
              wifiWasUsed = true;
              performBackgroundSearch();
            },
            [this]() {
              state = DDGState::SearchResults;
              requestUpdate();
            });
        return;
      }
    } else if (searchResults.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "DuckDuckGo Search", "", 40);
        startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            auto keyboardResult = std::get_if<KeyboardResult>(&result.data);
            if (keyboardResult && !keyboardResult->text.empty()) {
              searchQuery = keyboardResult->text;
              state = DDGState::Loading;
              requestUpdate();
              ensureWifiConnected(
                  [this]() {
                    wifiWasUsed = true;
                    performBackgroundSearch();
                  },
                  [this]() {
                    state = DDGState::SearchResults;
                    requestUpdate();
                  });
            }
          } else {
            requestUpdate();
          }
        });
      }
    } else {
      int totalItems = static_cast<int>(searchResults.size());
      if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        selectedIndex = (selectedIndex - 1 + totalItems) % totalItems;
        requestUpdate();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        selectedIndex = (selectedIndex + 1) % totalItems;
        requestUpdate();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        std::string title = searchResults[selectedIndex].title;
        std::string filepath = getWebsiteFilePath(title);
        if (Storage.exists(filepath.c_str())) {
          auto txt = std::unique_ptr<Txt>(new Txt(filepath, "/.crosspoint"));
          if (txt && txt->load()) {
            activityManager.pushActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
          }
        } else {
          downloadActivePost();
        }
      }
    }
    return;
  }
}

void DuckDuckGoActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "DuckDuckGo");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentHeight = contentBottom - contentTop;

  if (state == DDGState::Loading) {
    int textY = contentTop + contentHeight / 2 - 20;
    renderer.drawCenteredText(UI_12_FONT_ID, textY, "Searching DuckDuckGo...");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == DDGState::OfflineList) {
    GUI.drawButtonMenu(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, offlineWebsites.size() + 1, selectedIndex,
        [this](int index) {
          if (index == 0) return std::string("[+ Search DuckDuckGo]");
          return offlineWebsites[index - 1];
        },
        [this](int index) {
          if (index == 0) return UIIcon::DuckDuckGo;
          return UIIcon::Book;
        },
        9);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == DDGState::SearchResults) {
    if (!errorMessage.empty()) {
      int textY = contentTop + contentHeight / 2 - 40;
      renderer.drawCenteredText(UI_12_FONT_ID, textY, errorMessage.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(SMALL_FONT_ID, textY + 30, "Configure your network or retry search.");
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, "Retry");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else if (searchResults.empty()) {
      int textY = contentTop + contentHeight / 2 - 20;
      renderer.drawCenteredText(UI_12_FONT_ID, textY, "No results found.");
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Search", nullptr, nullptr);
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
      GUI.drawButtonMenu(
          renderer, Rect{0, contentTop, pageWidth, contentHeight}, searchResults.size(), selectedIndex,
          [this](int index) { return searchResults[index].title; },
          [this](int index) {
            std::string title = searchResults[index].title;
            std::string filepath = getWebsiteFilePath(title);
            if (Storage.exists(filepath.c_str())) {
              return UIIcon::Book;  // Cached offline
            }
            return UIIcon::Book;
          },
          9);

      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  }

  renderer.displayBuffer();
}
