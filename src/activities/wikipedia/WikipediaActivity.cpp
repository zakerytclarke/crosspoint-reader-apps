#include "WikipediaActivity.h"

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

std::string cleanUnicode(const std::string& input) {
  std::string output = "";
  output.reserve(input.size());
  for (size_t i = 0; i < input.size();) {
    unsigned char c = input[i];
    if (c < 0x80) {
      output += c;
      i++;
    } else if ((c & 0xE0) == 0xC0) {  // 2 bytes
      if (i + 1 < input.size()) {
        unsigned char c2 = input[i + 1];
        uint16_t codepoint = ((c & 0x1F) << 6) | (c2 & 0x3F);
        if (codepoint == 0x00A0) {  // Non-breaking space
          output += ' ';
        } else {
          output += input.substr(i, 2);
        }
      }
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {  // 3 bytes
      if (i + 2 < input.size()) {
        unsigned char c2 = input[i + 1];
        unsigned char c3 = input[i + 2];
        uint16_t codepoint = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        if (codepoint == 0x2018 || codepoint == 0x2019) {  // Single quotes
          output += '\'';
        } else if (codepoint == 0x201C || codepoint == 0x201D) {  // Double quotes
          output += '"';
        } else if (codepoint == 0x2013 || codepoint == 0x2014) {  // En/em dash
          output += '-';
        } else if (codepoint == 0x200B) {  // Zero-width space
          // skip it
        } else {
          output += input.substr(i, 3);
        }
      }
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {  // 4 bytes
      if (i + 3 < input.size()) {
        output += input.substr(i, 4);
      }
      i += 4;
    } else {
      i++;
    }
  }
  return output;
}

std::string convertToMarkdown(const std::string& title, const std::string& text) {
  std::string md = "# " + title + "\n\n";
  std::string line;
  std::istringstream stream(text);
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    size_t startEquals = 0;
    while (startEquals < line.size() && line[startEquals] == '=') {
      startEquals++;
    }
    size_t endEquals = 0;
    while (endEquals < line.size() && line[line.size() - 1 - endEquals] == '=') {
      endEquals++;
    }
    if (startEquals >= 2 && startEquals == endEquals && startEquals < line.size()) {
      std::string headingText = line.substr(startEquals, line.size() - 2 * startEquals);
      size_t first = headingText.find_first_not_of(" ");
      size_t last = headingText.find_last_not_of(" ");
      if (first != std::string::npos && last != std::string::npos) {
        headingText = headingText.substr(first, (last - first + 1));
      }
      std::string mdHeading(startEquals, '#');
      md += mdHeading + " " + headingText + "\n\n";
    } else {
      md += line + "\n";
    }
  }
  return md;
}

std::string getArticleFilePath(const std::string& title) {
  std::string mdPath = "/apps/wikipedia/" + sanitizeFilename(title) + ".md";
  if (Storage.exists(mdPath.c_str())) {
    return mdPath;
  }
  return "/apps/wikipedia/" + sanitizeFilename(title) + ".txt";
}

std::string urlEncode(const std::string& value) {
  std::string escaped = "";
  for (char c : value) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped += c;
    } else if (c == ' ') {
      escaped += "%20";
    } else {
      char hex[4];
      sprintf(hex, "%%%02X", static_cast<unsigned char>(c));
      escaped += hex;
    }
  }
  return escaped;
}

bool parseAndSaveWikipediaArticle(const std::string& tempJsonPath, std::string& outTitle) {
  HalFile jsonFile;
  if (!Storage.openFileForRead("WIKI", tempJsonPath.c_str(), jsonFile)) {
    return false;
  }

  std::string tempMdPath = "/apps/wikipedia/md.tmp";
  HalFile mdFile;
  if (!Storage.openFileForWrite("WIKI", tempMdPath.c_str(), mdFile)) {
    jsonFile.close();
    return false;
  }

  enum class ParserState { Scanning, InString, AfterString, ExpectingColon, ExpectingValue, InValueString };

  ParserState state = ParserState::Scanning;
  std::string currentKey = "";
  std::string currentValue = "";
  std::string currentLine = "";
  bool inEscape = false;
  outTitle.clear();

  auto writeLineToMd = [&](const std::string& rawLine) {
    std::string line = cleanUnicode(rawLine);
    if (line.empty()) {
      mdFile.write("\n", 1);
      return;
    }

    size_t startEquals = 0;
    while (startEquals < line.size() && line[startEquals] == '=') {
      startEquals++;
    }
    size_t endEquals = 0;
    while (endEquals < line.size() && line[line.size() - 1 - endEquals] == '=') {
      endEquals++;
    }

    if (startEquals >= 2 && startEquals == endEquals && startEquals < line.size()) {
      std::string headingText = line.substr(startEquals, line.size() - 2 * startEquals);
      size_t first = headingText.find_first_not_of(" ");
      size_t last = headingText.find_last_not_of(" ");
      if (first != std::string::npos && last != std::string::npos) {
        headingText = headingText.substr(first, (last - first + 1));
      }
      std::string mdHeading(startEquals, '#');
      std::string formatted = mdHeading + " " + headingText + "\n\n";
      mdFile.write(formatted.data(), formatted.size());
    } else {
      std::string formatted = line + "\n";
      mdFile.write(formatted.data(), formatted.size());
    }
  };

  int c;
  while ((c = jsonFile.read()) != -1) {
    char ch = static_cast<char>(c);

    switch (state) {
      case ParserState::Scanning:
        if (ch == '"') {
          currentKey.clear();
          state = ParserState::InString;
        }
        break;

      case ParserState::InString:
        if (inEscape) {
          currentKey += ch;
          inEscape = false;
        } else if (ch == '\\') {
          inEscape = true;
        } else if (ch == '"') {
          state = ParserState::AfterString;
        } else {
          currentKey += ch;
        }
        break;

      case ParserState::AfterString:
        if (std::isspace(static_cast<unsigned char>(ch))) {
          // ignore
        } else if (ch == ':') {
          state = ParserState::ExpectingColon;
        } else {
          state = ParserState::Scanning;
        }
        break;

      case ParserState::ExpectingColon:
        if (std::isspace(static_cast<unsigned char>(ch))) {
          // ignore
        } else if (ch == '"') {
          currentValue.clear();
          currentLine.clear();
          inEscape = false;
          if (currentKey == "title" || currentKey == "extract") {
            state = ParserState::InValueString;
          } else {
            state = ParserState::ExpectingValue;
          }
        } else {
          state = ParserState::Scanning;
        }
        break;

      case ParserState::ExpectingValue:
        if (inEscape) {
          inEscape = false;
        } else if (ch == '\\') {
          inEscape = true;
        } else if (ch == '"') {
          state = ParserState::Scanning;
        }
        break;

      case ParserState::InValueString:
        if (inEscape) {
          char escChar = 0;
          if (ch == 'n')
            escChar = '\n';
          else if (ch == 'r')
            escChar = '\r';
          else if (ch == 't')
            escChar = '\t';
          else if (ch == '"')
            escChar = '"';
          else if (ch == '\\')
            escChar = '\\';
          else if (ch == '/')
            escChar = '/';
          else if (ch == 'u') {
            uint16_t codepoint = 0;
            bool ok = true;
            for (int k = 0; k < 4; k++) {
              int hexDigit = jsonFile.read();
              if (hexDigit == -1) {
                ok = false;
                break;
              }
              char h = static_cast<char>(hexDigit);
              codepoint <<= 4;
              if (h >= '0' && h <= '9')
                codepoint |= (h - '0');
              else if (h >= 'a' && h <= 'f')
                codepoint |= (h - 'a' + 10);
              else if (h >= 'A' && h <= 'F')
                codepoint |= (h - 'A' + 10);
              else {
                ok = false;
                break;
              }
            }
            if (ok) {
              std::string utf8Str = "";
              if (codepoint < 0x80) {
                utf8Str += static_cast<char>(codepoint);
              } else if (codepoint < 0x800) {
                utf8Str += static_cast<char>(0xC0 | (codepoint >> 6));
                utf8Str += static_cast<char>(0x80 | (codepoint & 0x3F));
              } else {
                utf8Str += static_cast<char>(0xE0 | (codepoint >> 12));
                utf8Str += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                utf8Str += static_cast<char>(0x80 | (codepoint & 0x3F));
              }

              if (currentKey == "title") {
                currentValue += utf8Str;
              } else if (currentKey == "extract") {
                currentLine += utf8Str;
              }
            }
          } else {
            escChar = ch;
          }

          if (escChar != 0) {
            if (currentKey == "title") {
              currentValue += escChar;
            } else if (currentKey == "extract") {
              if (escChar == '\n') {
                writeLineToMd(currentLine);
                currentLine.clear();
              } else {
                currentLine += escChar;
              }
            }
          }
          inEscape = false;
        } else if (ch == '\\') {
          inEscape = true;
        } else if (ch == '"') {
          if (currentKey == "title") {
            outTitle = currentValue;
          } else if (currentKey == "extract") {
            if (!currentLine.empty()) {
              writeLineToMd(currentLine);
              currentLine.clear();
            }
          }
          state = ParserState::Scanning;
        } else {
          if (currentKey == "title") {
            currentValue += ch;
          } else if (currentKey == "extract") {
            if (ch == '\n') {
              writeLineToMd(currentLine);
              currentLine.clear();
            } else {
              currentLine += ch;
            }
          }
        }
        break;
    }
  }

  jsonFile.close();
  mdFile.close();

  if (outTitle.empty()) {
    Storage.remove(tempMdPath.c_str());
    return false;
  }

  std::string finalPath = "/apps/wikipedia/" + sanitizeFilename(outTitle) + ".md";
  HalFile finalFile;
  if (!Storage.openFileForWrite("WIKI", finalPath.c_str(), finalFile)) {
    Storage.remove(tempMdPath.c_str());
    return false;
  }

  std::string titleHeader = "# " + outTitle + "\n\n";
  finalFile.write(titleHeader.data(), titleHeader.size());

  HalFile tempMdFile;
  if (Storage.openFileForRead("WIKI", tempMdPath.c_str(), tempMdFile)) {
    char copyBuf[512];
    int bytesRead;
    while ((bytesRead = tempMdFile.read(reinterpret_cast<uint8_t*>(copyBuf), sizeof(copyBuf))) > 0) {
      finalFile.write(copyBuf, bytesRead);
    }
    tempMdFile.close();
  }
  finalFile.close();
  Storage.remove(tempMdPath.c_str());

  return true;
}

static void wikiFetchTaskFunc(void* param) {
  WikipediaActivity* activity = static_cast<WikipediaActivity*>(param);
  activity->runBackgroundFetch();
  // Activity cleans up tasks via cancelFetch and runBackgroundFetch
  vTaskDelete(nullptr);
}
}  // namespace

void WikipediaActivity::onEnter() {
  Activity::onEnter();
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/wikipedia");

  currentArticleText.clear();
  articleLines.clear();
  errorMessage.clear();
  loadOfflineArticlesList();
  state = WikiState::OfflineList;
  selectedIndex = 0;
  listScrollOffset = 0;
  wifiConnecting = false;

  fetchTaskHandle = nullptr;
  pendingUpdateSearch = false;
  pendingUpdateArticle = false;
  backgroundFetchFailed = false;
  isSearchTask = false;

  requestUpdate();
}

void WikipediaActivity::onExit() {
  Activity::onExit();
  if (fetchTaskHandle != nullptr) {
    TaskHandle_t tempHandle = static_cast<TaskHandle_t>(fetchTaskHandle);
    fetchTaskHandle = nullptr;
    vTaskDelete(tempHandle);
  }
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
  }
  if (wifiWasUsed) {
    silentRestart();
  }
}

void WikipediaActivity::loadOfflineArticlesList() {
  offlineArticles.clear();
  std::vector<String> files = Storage.listFiles("/apps/wikipedia");
  for (const auto& file : files) {
    std::string filename = file.c_str();
    if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".txt") {
      offlineArticles.push_back(filename.substr(0, filename.length() - 4));
    } else if (filename.length() > 3 && filename.substr(filename.length() - 3) == ".md") {
      offlineArticles.push_back(filename.substr(0, filename.length() - 3));
    }
  }
  std::sort(offlineArticles.begin(), offlineArticles.end());
}

void WikipediaActivity::performBackgroundSearch() {
  if (fetchTaskHandle != nullptr) return;
  state = WikiState::Loading;
  backgroundFetchFailed = false;
  pendingUpdateSearch = false;
  pendingSearch = false;
  isSearchTask = true;
  requestUpdate();

  xTaskCreate(wikiFetchTaskFunc, "wiki_fetch", 8192, this, 5, (TaskHandle_t*)&fetchTaskHandle);
}

void WikipediaActivity::performBackgroundFetchArticle() {
  if (fetchTaskHandle != nullptr) return;
  state = WikiState::Loading;
  backgroundFetchFailed = false;
  pendingUpdateArticle = false;
  pendingArticle = false;
  isSearchTask = false;
  requestUpdate();

  xTaskCreate(wikiFetchTaskFunc, "wiki_fetch", 8192, this, 5, (TaskHandle_t*)&fetchTaskHandle);
}

void WikipediaActivity::runBackgroundFetch() {
  DownloadWatchdog::start(35000);
  errorMessage.clear();

  bool success = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (!WifiConnectHelper::waitForTimeSync()) {
      errorMessage = "Clock sync failed. HTTPS requests may fail.";
    }
    int retries = 3;
    while (retries-- > 0) {
      if (isSearchTask) {
        success = fetchSearchData();
      } else {
        success = fetchArticleData();
      }
      if (success) {
        break;
      }
      if (retries > 0) {
        delay(1500);
      }
    }
  } else {
    errorMessage = "WiFi disconnected during fetch.";
  }

  DownloadWatchdog::stop();

  if (DownloadWatchdog::gotTimeout) {
    LOG_ERR("WIKI", "Background fetch timed out!");
    backgroundFetchFailed = true;
    errorMessage = "Request timed out.";
  } else if (!success) {
    backgroundFetchFailed = true;
    if (errorMessage.empty()) {
      errorMessage = isSearchTask ? "Search request failed." : "Failed to download article.";
    }
  } else {
    backgroundFetchFailed = false;
    errorMessage.clear();
  }

  if (isSearchTask) {
    pendingUpdateSearch = true;
  } else {
    pendingUpdateArticle = true;
  }
}

bool WikipediaActivity::fetchSearchData() {
  std::string url = "https://en.wikipedia.org/w/api.php?action=opensearch&search=" + urlEncode(searchQuery) +
                    "&limit=10&namespace=0&format=json";
  const char* tempPath = "/apps/wikipedia/search.tmp";

  std::string errorDetail;
  auto result =
      HttpDownloader::downloadToFile(url.c_str(), tempPath, nullptr, nullptr, "", "", nullptr, nullptr, &errorDetail);
  if (result != HttpDownloader::OK) {
    errorMessage =
        "Search HTTP Error " + std::to_string(result) + ": " + (errorDetail.empty() ? "Unknown" : errorDetail);
    Storage.remove(tempPath);
    return false;
  }

  HalFile file;
  if (Storage.openFileForRead("WIKI", tempPath, file)) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    Storage.remove(tempPath);

    if (!err && doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      if (arr.size() >= 2) {
        JsonArray titles = arr[1].as<JsonArray>();
        bgSearchResults.clear();
        for (JsonVariant val : titles) {
          bgSearchResults.push_back(val.as<std::string>());
        }
        return true;
      }
    }
  } else {
    Storage.remove(tempPath);
  }
  return false;
}

bool WikipediaActivity::fetchArticleData() {
  std::string url =
      "https://en.wikipedia.org/w/"
      "api.php?action=query&prop=extracts&explaintext=&titles=" +
      urlEncode(articleToFetch) + "&format=json&redirects=1";
  const char* tempPath = "/apps/wikipedia/article.tmp";

  std::string errorDetail;
  auto result =
      HttpDownloader::downloadToFile(url.c_str(), tempPath, nullptr, nullptr, "", "", nullptr, nullptr, &errorDetail);
  if (result != HttpDownloader::OK) {
    errorMessage =
        "Article HTTP Error " + std::to_string(result) + ": " + (errorDetail.empty() ? "Unknown" : errorDetail);
    Storage.remove(tempPath);
    return false;
  }

  std::string title;
  if (parseAndSaveWikipediaArticle(tempPath, title)) {
    Storage.remove(tempPath);
    if (!title.empty()) {
      currentArticleTitle = title;
      currentArticleText.clear();
      articleLines.clear();
      return true;
    }
  } else {
    Storage.remove(tempPath);
  }
  return false;
}

void WikipediaActivity::loop() {
  if (pendingSearch) {
    performBackgroundSearch();
    return;
  }

  if (pendingArticle) {
    performBackgroundFetchArticle();
    return;
  }

  if (pendingUpdateSearch) {
    pendingUpdateSearch = false;
    fetchTaskHandle = nullptr;
    if (DownloadWatchdog::gotTimeout) {
      LOG_ERR("WIKI", "Watchdog timeout! Crashing to home screen.");
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
    state = WikiState::SearchResults;
    requestUpdate();
    return;
  }

  if (pendingUpdateArticle) {
    pendingUpdateArticle = false;
    fetchTaskHandle = nullptr;
    if (DownloadWatchdog::gotTimeout) {
      LOG_ERR("WIKI", "Watchdog timeout! Crashing to home screen.");
      activityManager.goHome();
      return;
    }
    if (!backgroundFetchFailed && errorMessage.empty()) {
      state = WikiState::SearchResults;
      std::string filepath = getArticleFilePath(currentArticleTitle);
      auto txt = std::unique_ptr<Txt>(new Txt(filepath, "/.crosspoint"));
      if (txt && txt->load()) {
        activityManager.pushActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
      }
    } else {
      state = WikiState::SearchResults;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == WikiState::Loading) {
      if (fetchTaskHandle != nullptr) {
        TaskHandle_t tempHandle = static_cast<TaskHandle_t>(fetchTaskHandle);
        fetchTaskHandle = nullptr;
        vTaskDelete(tempHandle);
      }
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      if (!searchResults.empty()) {
        state = WikiState::SearchResults;
      } else {
        state = WikiState::OfflineList;
        loadOfflineArticlesList();
      }
      requestUpdate();
    } else if (state == WikiState::ArticleView) {
      if (searchResults.empty()) {
        state = WikiState::OfflineList;
        loadOfflineArticlesList();
      } else {
        state = WikiState::SearchResults;
      }
      requestUpdate();
    } else if (state == WikiState::SearchResults) {
      state = WikiState::OfflineList;
      loadOfflineArticlesList();
      selectedIndex = 0;
      listScrollOffset = 0;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (state == WikiState::OfflineList) {
    int totalItems = static_cast<int>(offlineArticles.size()) + 1;
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectedIndex = (selectedIndex - 1 + totalItems) % totalItems;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectedIndex = (selectedIndex + 1) % totalItems;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedIndex == 0) {
        auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Wikipedia Search", "", 40);
        startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            auto keyboardResult = std::get_if<KeyboardResult>(&result.data);
            if (keyboardResult && !keyboardResult->text.empty()) {
              searchQuery = keyboardResult->text;
              state = WikiState::Loading;
              isSearchTask = true;
              requestUpdate();
              ensureWifiConnected(
                  [this]() {
                    wifiWasUsed = true;
                    pendingSearch = true;
                    requestUpdate();
                  },
                  [this]() {
                    state = WikiState::OfflineList;
                    requestUpdate();
                  });
            }
          } else {
            requestUpdate();
          }
        });
      } else {
        std::string title = offlineArticles[selectedIndex - 1];
        std::string filepath = getArticleFilePath(title);
        auto txt = std::unique_ptr<Txt>(new Txt(filepath, "/.crosspoint"));
        if (txt && txt->load()) {
          activityManager.pushActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
        }
      }
    }
    return;
  }

  if (state == WikiState::SearchResults) {
    if (!errorMessage.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
          mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        state = WikiState::Loading;
        isSearchTask = true;
        errorMessage.clear();
        requestUpdate();
        ensureWifiConnected(
            [this]() {
              wifiWasUsed = true;
              pendingSearch = true;
              requestUpdate();
            },
            [this]() {
              state = WikiState::SearchResults;
              requestUpdate();
            });
        return;
      }
    } else if (searchResults.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Wikipedia Search", "", 40);
        startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            auto keyboardResult = std::get_if<KeyboardResult>(&result.data);
            if (keyboardResult && !keyboardResult->text.empty()) {
              searchQuery = keyboardResult->text;
              state = WikiState::Loading;
              isSearchTask = true;
              requestUpdate();
              ensureWifiConnected(
                  [this]() {
                    wifiWasUsed = true;
                    pendingSearch = true;
                    requestUpdate();
                  },
                  [this]() {
                    state = WikiState::SearchResults;
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
        std::string title = searchResults[selectedIndex];
        std::string filepath = getArticleFilePath(title);
        if (Storage.exists(filepath.c_str())) {
          auto txt = std::unique_ptr<Txt>(new Txt(filepath, "/.crosspoint"));
          if (txt && txt->load()) {
            activityManager.pushActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
          }
        } else {
          articleToFetch = title;
          state = WikiState::Loading;
          isSearchTask = false;
          GUI.drawPopup(renderer, "Downloading...");
          if (WifiConnectHelper::ensureWifiConnected()) {
            wifiWasUsed = true;
            pendingArticle = true;
            requestUpdate();
          } else {
            GUI.drawPopup(renderer, "Download failed!");
            delay(2000);
            state = WikiState::SearchResults;
            requestUpdate();
          }
        }
      }
    }
    return;
  }

  if (state == WikiState::ArticleView) {
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int cardH = contentBottom - contentTop;
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int linesPerPage = (cardH - 60) / lineHeight;

    int maxOffset = std::max(0, static_cast<int>(articleLines.size()) - linesPerPage);

    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      articleScrollOffset = std::max(0, articleScrollOffset - 1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      articleScrollOffset = std::min(maxOffset, articleScrollOffset + 1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      articleScrollOffset = std::max(0, articleScrollOffset - linesPerPage);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      articleScrollOffset = std::min(maxOffset, articleScrollOffset + linesPerPage);
      requestUpdate();
    }
    return;
  }
}

void WikipediaActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Wikipedia");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentHeight = contentBottom - contentTop;

  if (state == WikiState::Loading) {
    int textY = contentTop + contentHeight / 2 - 20;
    if (isSearchTask) {
      renderer.drawCenteredText(UI_12_FONT_ID, textY, "Loading Wikipedia...");
    } else {
      renderer.drawCenteredText(UI_12_FONT_ID, textY, "Downloading...");
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == WikiState::OfflineList) {
    GUI.drawButtonMenu(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, offlineArticles.size() + 1, selectedIndex,
        [this](int index) {
          if (index == 0) return std::string("[+ Search Wikipedia]");
          return offlineArticles[index - 1];
        },
        [this](int index) {
          if (index == 0) return UIIcon::File;
          return UIIcon::Book;
        },
        9);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == WikiState::SearchResults) {
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
          [this](int index) { return searchResults[index]; },
          [this](int index) {
            std::string title = searchResults[index];
            std::string filepath = "/apps/wikipedia/" + sanitizeFilename(title) + ".txt";
            if (Storage.exists(filepath.c_str())) {
              return UIIcon::Book;  // Cached offline
            }
            return UIIcon::Book;  // Remote online
          },
          9);

      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else if (state == WikiState::ArticleView) {
    const int cardX = metrics.contentSidePadding;
    const int cardY = contentTop;
    const int cardW = pageWidth - 2 * metrics.contentSidePadding;
    const int cardH = contentHeight;

    renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 1, 10, true);

    // Header Title Card
    renderer.drawText(SMALL_FONT_ID, cardX + 20, cardY + 20, currentArticleTitle.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawLine(cardX + 20, cardY + 40, cardX + cardW - 20, cardY + 40, 1, true);

    // Compute lines
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int linesPerPage = (cardH - 60) / lineHeight;

    articleLines =
        renderer.wrappedText(SMALL_FONT_ID, currentArticleText.c_str(), cardW - 40, 2000, EpdFontFamily::REGULAR);

    for (int i = 0; i < linesPerPage; i++) {
      int idx = articleScrollOffset + i;
      if (idx >= static_cast<int>(articleLines.size())) break;
      renderer.drawText(SMALL_FONT_ID, cardX + 20, cardY + 50 + i * lineHeight, articleLines[idx].c_str(), true,
                        EpdFontFamily::REGULAR);
    }

    // Paging Indicator
    if (!articleLines.empty()) {
      int currentLineEnd = std::min(static_cast<int>(articleLines.size()), articleScrollOffset + linesPerPage);
      std::string pageStr = std::to_string(currentLineEnd) + " / " + std::to_string(articleLines.size()) + " lines";
      int pageW = renderer.getTextWidth(SMALL_FONT_ID, pageStr.c_str());
      renderer.drawText(SMALL_FONT_ID, cardX + cardW - 20 - pageW, cardY + 20, pageStr.c_str(), true,
                        EpdFontFamily::REGULAR);
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), nullptr, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
