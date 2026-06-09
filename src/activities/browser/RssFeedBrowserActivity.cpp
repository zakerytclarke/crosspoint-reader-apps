#include "RssFeedBrowserActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HtmlArticleExtractor.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cctype>
#include <cstring>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/RssArticleActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
constexpr int LIST_FONT_ID = UI_12_FONT_ID;
constexpr int LIST_TOP = 60;
constexpr int ROW_HEIGHT = 38;
constexpr int PAGE_ITEMS = 18;
constexpr size_t MAX_ARTICLE_HTML_BYTES = 40 * 1024;
constexpr size_t MAX_ARTICLE_SCAN_BYTES = 256 * 1024;
constexpr size_t ARTICLE_MARKER_CONTEXT_BYTES = 1024;
constexpr size_t ARTICLE_EXTRACT_HEAP_RESERVE_BYTES = 12 * 1024;
constexpr char ARTICLE_TMP_DIR[] = "/.crosspoint";
constexpr char ARTICLE_TMP_FILE[] = "/.crosspoint/rss_article.tmp";

struct ArticleFetchResult {
  std::string text;
  std::string ampUrl;
};

bool startsWithAtCaseInsensitive(const std::string& text, size_t pos, const char* prefix) {
  const size_t len = strlen(prefix);
  if (pos + len > text.size()) return false;
  for (size_t i = 0; i < len; i++) {
    const char lhs = static_cast<char>(tolower(static_cast<unsigned char>(text[pos + i])));
    const char rhs = static_cast<char>(tolower(static_cast<unsigned char>(prefix[i])));
    if (lhs != rhs) return false;
  }
  return true;
}

size_t findCaseInsensitive(const std::string& text, const char* needle, size_t start = 0) {
  const size_t needleLen = strlen(needle);
  if (needleLen == 0) return start <= text.size() ? start : std::string::npos;
  if (needleLen > text.size() || start > text.size() - needleLen) return std::string::npos;
  for (size_t pos = start; pos <= text.size() - needleLen; pos++) {
    if (startsWithAtCaseInsensitive(text, pos, needle)) return pos;
  }
  return std::string::npos;
}

std::string resolveArticleUrl(const std::string& baseUrl, const std::string& href) {
  if (href.empty()) return {};
  if (href.starts_with("http://") || href.starts_with("https://")) return href;
  const size_t schemeEnd = baseUrl.find("://");
  if (schemeEnd == std::string::npos) return {};
  if (href.starts_with("//")) return baseUrl.substr(0, schemeEnd) + ":" + href;

  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = baseUrl.find('/', hostStart);
  const std::string origin = pathStart == std::string::npos ? baseUrl : baseUrl.substr(0, pathStart);
  if (href[0] == '/') return origin + href;

  size_t queryStart = baseUrl.find_first_of("?#", hostStart);
  if (queryStart == std::string::npos) queryStart = baseUrl.size();
  size_t dirEnd = baseUrl.rfind('/', queryStart);
  if (dirEnd == std::string::npos || dirEnd < hostStart) return origin + "/" + href;
  return baseUrl.substr(0, dirEnd + 1) + href;
}

std::string extractAmpHtmlUrl(const std::string& html, const std::string& baseUrl) {
  size_t pos = 0;
  while ((pos = findCaseInsensitive(html, "<link", pos)) != std::string::npos) {
    const size_t tagEnd = html.find('>', pos + 5);
    if (tagEnd == std::string::npos) break;
    const size_t tagLen = tagEnd - pos + 1;
    if (findCaseInsensitive(html.substr(pos, tagLen), "amphtml") == std::string::npos) {
      pos = tagEnd + 1;
      continue;
    }

    const size_t hrefPos = findCaseInsensitive(html, "href", pos);
    if (hrefPos == std::string::npos || hrefPos > tagEnd) {
      pos = tagEnd + 1;
      continue;
    }
    size_t valueStart = html.find('=', hrefPos + 4);
    if (valueStart == std::string::npos || valueStart > tagEnd) {
      pos = tagEnd + 1;
      continue;
    }
    valueStart++;
    while (valueStart < tagEnd && isspace(static_cast<unsigned char>(html[valueStart]))) valueStart++;
    if (valueStart >= tagEnd) {
      pos = tagEnd + 1;
      continue;
    }

    const char quote = (html[valueStart] == '"' || html[valueStart] == '\'') ? html[valueStart++] : '\0';
    size_t valueEnd = valueStart;
    while (valueEnd < tagEnd) {
      if ((quote && html[valueEnd] == quote) ||
          (!quote && (isspace(static_cast<unsigned char>(html[valueEnd])) || html[valueEnd] == '>'))) {
        break;
      }
      valueEnd++;
    }
    return resolveArticleUrl(baseUrl, html.substr(valueStart, valueEnd - valueStart));
  }
  return {};
}

// Blank out <style> and <script> block content in `s` (in-place, same length).
// `inBlock` persists across HTTP chunks so blocks that span a boundary are handled correctly.
// Positions within `s` are preserved (blanked bytes become spaces), so markerPos values
// found in the stripped string map 1:1 to positions in the original data buffer.
void stripBlocksForSearch(std::string& s, const char* openTag, const char* closeTag, bool& inBlock) {
  const size_t openTagLen = strlen(openTag);
  const size_t closeTagLen = strlen(closeTag);
  size_t pos = 0;

  if (inBlock) {
    const size_t close = findCaseInsensitive(s, closeTag, 0);
    if (close != std::string::npos) {
      std::fill(s.begin(), s.begin() + close + closeTagLen, ' ');
      pos = close + closeTagLen;
      inBlock = false;
    } else {
      std::fill(s.begin(), s.end(), ' ');
      return;
    }
  }

  while (true) {
    const size_t open = findCaseInsensitive(s, openTag, pos);
    if (open == std::string::npos) break;
    const size_t nameEnd = open + openTagLen;
    if (nameEnd < s.size() && s[nameEnd] != '>' && !std::isspace(static_cast<unsigned char>(s[nameEnd]))) {
      pos = nameEnd;
      continue;
    }
    const size_t close = findCaseInsensitive(s, closeTag, open);
    if (close != std::string::npos) {
      std::fill(s.begin() + open, s.begin() + close + closeTagLen, ' ');
      pos = close + closeTagLen;
    } else {
      std::fill(s.begin() + open, s.end(), ' ');
      inBlock = true;
      break;
    }
  }
}

// Search for marker string but reject matches in CSS context (preceded by '.' or '#').
// Without this, `.entry-content { ... }` in a <style> block fires before the real HTML element,
// causing the marker to point at stylesheet bytes which then appear as garbled CSS in output.
size_t findMarkerNotInCss(const std::string& html, const char* marker, size_t start = 0) {
  const size_t markerLen = strlen(marker);
  size_t pos = start;
  while ((pos = findCaseInsensitive(html, marker, pos)) != std::string::npos) {
    if (pos > 0 && (html[pos - 1] == '.' || html[pos - 1] == '#')) {
      pos += markerLen;
      continue;
    }
    return pos;
  }
  return std::string::npos;
}

bool findArticleMarker(const std::string& html, size_t& markerPos) {
  // Structural markers: safe to match anywhere — these strings never appear as CSS selectors.
  const char* structural[] = {
      "articlebody",       // JSON-LD / generic JSON key
      "<article",          // HTML5 article element (includes the '<')
      "mw-parser-output",  // MediaWiki
      "__NEXT_DATA__",     // Next.js SSR JSON payload
      "w-richtext",        // Webflow (not a CSS name pattern)
  };

  // Class / id name markers: only match when NOT preceded by '.' or '#'
  // (i.e. must appear in an HTML attribute, not a CSS rule).
  const char* classNames[] = {
      // WordPress core
      "entry-content",
      "post-content",
      "post-body",
      "the-content",
      "hentry",
      "single-content",
      "td-post-content",
      // Ghost CMS
      "gh-content",
      "post-full-content",
      // Medium
      "section-inner",
      // Vox Media
      "c-entry-content",
      // BEM patterns
      "article__body",
      "article__content",
      "post__body",
      "post__content",
      // Generic news / blog
      "article-content",
      "article-body",
      "story-body",
      "body-content",
      "main-content",
      "content-body",
      "caas-body",
      "article-text",
      "entry__body",
      // Drupal
      "field-item",
  };

  bool found = false;
  markerPos = std::string::npos;

  auto consider = [&](size_t pos) {
    if (pos != std::string::npos && (!found || pos < markerPos)) {
      markerPos = pos;
      found = true;
    }
  };

  for (const char* m : structural) consider(findCaseInsensitive(html, m));
  for (const char* m : classNames) consider(findMarkerNotInCss(html, m));

  return found;
}

std::string wikipediaRenderUrl(const std::string& url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return {};
  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  if (pathStart == std::string::npos) return {};

  const std::string host = url.substr(hostStart, pathStart - hostStart);
  if (host.find("wikipedia.org") == std::string::npos) return {};
  if (url.compare(pathStart, 6, "/wiki/") != 0) return {};

  size_t titleEnd = url.find_first_of("?#", pathStart + 6);
  if (titleEnd == std::string::npos) titleEnd = url.size();
  if (titleEnd <= pathStart + 6) return {};

  const std::string title = url.substr(pathStart + 6, titleEnd - pathStart - 6);
  return url.substr(0, schemeEnd + 3) + host + "/w/index.php?title=" + title + "&action=render";
}
}  // namespace

void RssFeedBrowserActivity::onEnter() {
  Activity::onEnter();

  state = BrowserState::CHECK_WIFI;
  items.clear();
  feedTitle.clear();
  selectorIndex = 0;
  consumeBack = false;
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  checkAndConnectWifi();
}

void RssFeedBrowserActivity::onExit() {
  Activity::onExit();
  items.clear();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void RssFeedBrowserActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION) return;

  if (consumeBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    consumeBack = false;
    return;
  }

  if (state == BrowserState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = BrowserState::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchFeed();
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome(HomeMenuItem::RSS_READER);
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING || state == BrowserState::ARTICLE_LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) onGoHome(HomeMenuItem::RSS_READER);
    return;
  }

  if (state == BrowserState::BROWSING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!items.empty()) openItem(items[selectorIndex]);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome(HomeMenuItem::RSS_READER);
    }

    if (!items.empty()) {
      buttonNavigator.onNextRelease([this] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, items.size());
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, items.size());
        requestUpdate();
      });
      buttonNavigator.onNextContinuous([this] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, items.size(), PAGE_ITEMS);
        requestUpdate();
      });
      buttonNavigator.onPreviousContinuous([this] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, items.size(), PAGE_ITEMS);
        requestUpdate();
      });
    }
  }
}

void RssFeedBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const char* headerTitle =
      !feed.name.empty() ? feed.name.c_str() : (!feedTitle.empty() ? feedTitle.c_str() : tr(STR_RSS_READER));
  renderer.drawCenteredText(UI_12_FONT_ID, 15, headerTitle, true, EpdFontFamily::BOLD);

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING || state == BrowserState::ARTICLE_LOADING) {
    if (state == BrowserState::ARTICLE_LOADING) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_LOADING));
      auto row = renderer.truncatedText(UI_10_FONT_ID, statusMessage.c_str(), pageWidth - 40);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, row.c_str());
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (items.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_ENTRIES));
  } else {
    const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, LIST_TOP + (selectorIndex % PAGE_ITEMS) * ROW_HEIGHT - 3, pageWidth - 1, ROW_HEIGHT);

    for (size_t i = pageStartIndex; i < items.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
      const auto& item = items[i];
      std::string displayText = item.title;
      if (!item.published.empty()) displayText += " - " + item.published;
      auto row = renderer.truncatedText(LIST_FONT_ID, displayText.c_str(), pageWidth - 40);
      renderer.drawText(LIST_FONT_ID, 20, LIST_TOP + (i % PAGE_ITEMS) * ROW_HEIGHT, row.c_str(),
                        i != static_cast<size_t>(selectorIndex));
    }
  }
  renderer.displayBuffer();
}

void RssFeedBrowserActivity::fetchFeed() {
  if (feed.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_FEED_URL);
    requestUpdate();
    return;
  }

  LOG_DBG("RSS", "Fetching: %s", feed.url.c_str());
  RssParser parser;
  if (!HttpDownloader::fetchUrl(
          feed.url, [&parser](const uint8_t* data, size_t len) { return parser.write(data, len) == len; },
          feed.username, feed.password)) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }
  parser.flush();

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  feedTitle = parser.getFeedTitle();
  items = std::move(parser).getItems();
  selectorIndex = 0;
  state = items.empty() ? BrowserState::ERROR : BrowserState::BROWSING;
  if (items.empty()) errorMessage = tr(STR_NO_ENTRIES);
  requestUpdate();
}

void RssFeedBrowserActivity::openItem(const RssItem& item) {
  consumeBack = true;
  state = BrowserState::ARTICLE_LOADING;
  statusMessage = item.title;
  // Force the loading screen to paint before the blocking article fetch starts.
  // fetchArticleText() clears font caches afterward so TLS still starts with
  // the largest heap block we can give it.
  requestUpdateAndWait();

  RssItem article = item;
  std::string extracted = fetchArticleText(item);
  if (!extracted.empty() && extracted.size() > article.content.size()) {
    LOG_DBG("RSS", "Using extracted article text (%zu bytes): %s", extracted.size(), item.link.c_str());
    article.content = std::move(extracted);
  } else if (!extracted.empty()) {
    LOG_DBG("RSS", "Keeping longer feed content (%zu bytes, extracted %zu bytes): %s", article.content.size(),
            extracted.size(), item.link.c_str());
  } else {
    LOG_DBG("RSS", "Using feed summary fallback (%zu bytes): %s", item.content.size(), item.link.c_str());
  }

  state = BrowserState::BROWSING;
  startActivityForResult(std::make_unique<RssArticleActivity>(renderer, mappedInput, article),
                         [this](const ActivityResult&) { requestUpdate(); });
}

std::string RssFeedBrowserActivity::fetchArticleText(const RssItem& item) {
  if (!item.link.starts_with("http://") && !item.link.starts_with("https://")) {
    LOG_DBG("RSS", "Article link is not fetchable: %s", item.link.c_str());
    return {};
  }

  auto* fontCache = renderer.getFontCacheManager();
  if (fontCache) fontCache->clearCache();
  LOG_DBG("RSS", "Article heap before fetch: free=%u max=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  if (!Storage.exists(ARTICLE_TMP_DIR)) Storage.ensureDirectoryExists(ARTICLE_TMP_DIR);

  auto fetchHtmlToFile = [&](const std::string& url) {
    Storage.remove(ARTICLE_TMP_FILE);
    HalFile file;
    if (!Storage.openFileForWrite("RSS", ARTICLE_TMP_FILE, file)) {
      LOG_ERR("RSS", "Failed to open article temp file");
      return false;
    }

    size_t scannedBytes = 0;
    size_t storedBytes = 0;
    bool reachedLimit = false;
    bool focusedOnMarker = false;
    bool inStyleBlock = false;
    bool inScriptBlock = false;

    auto reopenTempFile = [&]() {
      file.close();
      Storage.remove(ARTICLE_TMP_FILE);
      if (!Storage.openFileForWrite("RSS", ARTICLE_TMP_FILE, file)) {
        LOG_ERR("RSS", "Failed to reopen article temp file");
        return false;
      }
      storedBytes = 0;
      return true;
    };

    auto writeToTemp = [&](const uint8_t* data, size_t len) {
      if (storedBytes >= MAX_ARTICLE_HTML_BYTES) return true;
      const size_t copyLen = std::min(MAX_ARTICLE_HTML_BYTES - storedBytes, len);
      if (copyLen == 0) return true;
      if (file.write(data, copyLen) != copyLen) return false;
      storedBytes += copyLen;
      return true;
    };

    const bool fetched = HttpDownloader::fetchUrl(
        url,
        [&](const uint8_t* data, size_t len) {
          if (!focusedOnMarker) {
            // Strip <style> and <script> block content before marker search.
            // This prevents CSS class selectors (e.g. `.entry-content { }`) and JS strings
            // from matching article markers. Positions map 1:1 to original data.
            std::string chunkStr(reinterpret_cast<const char*>(data), len);
            stripBlocksForSearch(chunkStr, "<style", "</style>", inStyleBlock);
            stripBlocksForSearch(chunkStr, "<script", "</script>", inScriptBlock);
            size_t markerPos = std::string::npos;
            if (findArticleMarker(chunkStr, markerPos)) {
              LOG_DBG("RSS", "Article marker found after %zu bytes: %s", scannedBytes, url.c_str());
              focusedOnMarker = true;
              if (!reopenTempFile()) return false;
              const size_t sliceStart =
                  markerPos > ARTICLE_MARKER_CONTEXT_BYTES ? markerPos - ARTICLE_MARKER_CONTEXT_BYTES : 0;
              if (!writeToTemp(data + sliceStart, len - sliceStart)) return false;
            } else {
              if (!writeToTemp(data, len)) return false;
            }
          } else {
            if (!writeToTemp(data, len)) return false;
          }

          scannedBytes += len;
          if ((focusedOnMarker && storedBytes >= MAX_ARTICLE_HTML_BYTES) || scannedBytes >= MAX_ARTICLE_SCAN_BYTES) {
            reachedLimit = true;
            return false;
          }
          return true;
        },
        feed.username, feed.password);
    file.close();
    if (reachedLimit) LOG_DBG("RSS", "Article HTML truncated: %s", url.c_str());
    return fetched || storedBytes > 0;
  };

  auto fetchAndExtract = [&](const std::string& url) {
    bool fetched = fetchHtmlToFile(url);
    size_t htmlBytes = Storage.exists(ARTICLE_TMP_FILE) ? Storage.open(ARTICLE_TMP_FILE).size() : 0;
    if (!fetched && htmlBytes == 0) {
      LOG_DBG("RSS", "Article empty fetch failed; retrying after cache clear: free=%u max=%u", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap());
      if (fontCache) fontCache->clearCache();
      delay(50);
      fetched = fetchHtmlToFile(url);
      htmlBytes = Storage.exists(ARTICLE_TMP_FILE) ? Storage.open(ARTICLE_TMP_FILE).size() : 0;
    }

    LOG_DBG("RSS", "Article fetch %s, html bytes=%zu: %s", fetched ? "ok" : "partial/failed", htmlBytes, url.c_str());
    if (!fetched && htmlBytes == 0) {
      Storage.remove(ARTICLE_TMP_FILE);
      return ArticleFetchResult{};
    }

    const size_t maxAlloc = ESP.getMaxAllocHeap();
    const size_t readBytes = std::min(
        htmlBytes, maxAlloc > ARTICLE_EXTRACT_HEAP_RESERVE_BYTES ? maxAlloc - ARTICLE_EXTRACT_HEAP_RESERVE_BYTES : 0);
    if (readBytes < 4096) {
      LOG_ERR("RSS", "Not enough heap to load article HTML: bytes=%zu max=%zu", htmlBytes, maxAlloc);
      Storage.remove(ARTICLE_TMP_FILE);
      return ArticleFetchResult{};
    }

    std::string html;
    html.resize(readBytes);
    HalFile file = Storage.open(ARTICLE_TMP_FILE);
    const int bytesRead = file.read(html.data(), readBytes);
    file.close();
    Storage.remove(ARTICLE_TMP_FILE);
    if (bytesRead <= 0) return ArticleFetchResult{};
    html.resize(static_cast<size_t>(bytesRead));

    LOG_DBG("RSS", "Article heap before extract: free=%u max=%u read=%zu", ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
            html.size());
    std::string extracted = HtmlArticleExtractor::extractReadableText(html);
    LOG_DBG("RSS", "Article extracted bytes=%zu", extracted.size());
    std::string ampUrl = extracted.empty() ? extractAmpHtmlUrl(html, url) : std::string{};
    return ArticleFetchResult{std::move(extracted), std::move(ampUrl)};
  };

  ArticleFetchResult result = fetchAndExtract(item.link);
  std::string extracted = std::move(result.text);
  if (extracted.empty() && !result.ampUrl.empty() && result.ampUrl != item.link) {
    LOG_DBG("RSS", "Retrying AMP article URL: %s", result.ampUrl.c_str());
    if (fontCache) fontCache->clearCache();
    result = fetchAndExtract(result.ampUrl);
    extracted = std::move(result.text);
  }

  const std::string wikiUrl = wikipediaRenderUrl(item.link);
  if (extracted.empty() && !wikiUrl.empty()) {
    LOG_DBG("RSS", "Retrying Wikipedia render endpoint: %s", wikiUrl.c_str());
    if (fontCache) fontCache->clearCache();
    result = fetchAndExtract(wikiUrl);
    extracted = std::move(result.text);
  }
  return extracted;
}

void RssFeedBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchFeed();
    return;
  }
  launchWifiSelection();
}

void RssFeedBrowserActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void RssFeedBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchFeed();
  } else {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
