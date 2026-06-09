#include "HtmlArticleExtractor.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace {
constexpr size_t MAX_EXTRACTED_TEXT_BYTES = 4 * 1024;
constexpr size_t MAX_DECODE_BUFFER_BYTES = 12 * 1024;

char lowerAsciiChar(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string toLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool startsWithAt(const std::string& text, size_t pos, const char* prefix) {
  const size_t len = strlen(prefix);
  return pos + len <= text.size() && text.compare(pos, len, prefix) == 0;
}

bool startsWithAtCaseInsensitive(std::string_view text, size_t pos, const char* prefix) {
  const size_t len = strlen(prefix);
  if (pos + len > text.size()) return false;
  for (size_t i = 0; i < len; i++) {
    if (lowerAsciiChar(text[pos + i]) != lowerAsciiChar(prefix[i])) return false;
  }
  return true;
}

size_t findCaseInsensitive(std::string_view text, const char* needle, size_t start = 0) {
  const size_t needleLen = strlen(needle);
  if (needleLen == 0) return start <= text.size() ? start : std::string::npos;
  if (needleLen > text.size() || start > text.size() - needleLen) return std::string::npos;
  for (size_t pos = start; pos <= text.size() - needleLen; pos++) {
    if (startsWithAtCaseInsensitive(text, pos, needle)) return pos;
  }
  return std::string::npos;
}

size_t findChar(std::string_view text, char needle, size_t start = 0) {
  const size_t found = text.find(needle, start);
  return found == std::string_view::npos ? std::string::npos : found;
}

void trimInPlace(std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
  if (end < value.size()) value.erase(end);
  if (start > 0) value.erase(0, start);
}

bool tagNameBoundary(std::string_view text, size_t pos) {
  return pos >= text.size() || text[pos] == '>' || text[pos] == '/' ||
         std::isspace(static_cast<unsigned char>(text[pos]));
}

bool tagMatches(const std::string& tagLower, const char* name) {
  return startsWithAt(tagLower, 0, name) && tagNameBoundary(tagLower, strlen(name));
}

bool tagHasContentHint(const std::string& tagText) {
  const std::string lower = toLowerAscii(tagText);
  const char* hints[] = {"article", "content", "post", "story", "entry", "body", "main"};
  for (const char* hint : hints) {
    if (lower.find(hint) != std::string::npos) return true;
  }
  return false;
}

void appendUtf8(std::string& out, unsigned long cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string decodeJsonString(const std::string& value) {
  std::string out;
  out.reserve(std::min<size_t>(value.size(), 1024));
  for (size_t i = 0; i < value.size(); i++) {
    if (out.size() >= MAX_DECODE_BUFFER_BYTES) break;
    if (value[i] != '\\' || i + 1 >= value.size()) {
      out.push_back(value[i]);
      continue;
    }

    const char next = value[++i];
    switch (next) {
      case '"':
      case '\\':
      case '/':
        out.push_back(next);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\n');
        break;
      case 't':
        out.push_back(' ');
        break;
      case 'u': {
        if (i + 4 <= value.size()) {
          const std::string hex = value.substr(i + 1, 4);
          char* end = nullptr;
          const unsigned long cp = strtoul(hex.c_str(), &end, 16);
          if (end && *end == '\0') appendUtf8(out, cp);
          i += 4;
        }
        break;
      }
      default:
        out.push_back(next);
        break;
    }
  }
  return out;
}

std::string decodeEntities(const std::string& text) {
  std::string out;
  out.reserve(std::min<size_t>(text.size(), 1024));
  for (size_t i = 0; i < text.size(); i++) {
    if (out.size() >= MAX_EXTRACTED_TEXT_BYTES) break;
    if (text[i] != '&') {
      out.push_back(text[i]);
      continue;
    }

    const size_t semi = text.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 12) {
      out.push_back(text[i]);
      continue;
    }

    const std::string entity = text.substr(i + 1, semi - i - 1);
    const std::string lower = toLowerAscii(entity);
    if (lower == "amp") {
      out.push_back('&');
    } else if (lower == "lt") {
      out.push_back('<');
    } else if (lower == "gt") {
      out.push_back('>');
    } else if (lower == "quot") {
      out.push_back('"');
    } else if (lower == "apos" || lower == "#39") {
      out.push_back('\'');
    } else if (lower == "nbsp") {
      out.push_back(' ');
    } else if (!lower.empty() && lower[0] == '#') {
      char* end = nullptr;
      const int base = lower.size() > 2 && lower[1] == 'x' ? 16 : 10;
      const char* start = lower.c_str() + (base == 16 ? 2 : 1);
      const unsigned long cp = strtoul(start, &end, base);
      if (end && *end == '\0') {
        appendUtf8(out, cp);
      }
    } else {
      out.push_back('&');
      out += entity;
      out.push_back(';');
    }
    i = semi;
  }
  return out;
}

std::string extractJsonStringValue(const std::string& html, const char* key) {
  const std::string quotedKey = std::string("\"") + key + "\"";
  size_t pos = 0;
  while ((pos = findCaseInsensitive(html, quotedKey.c_str(), pos)) != std::string::npos) {
    const size_t colon = html.find(':', pos + quotedKey.size());
    if (colon == std::string::npos) return {};
    size_t valueStart = colon + 1;
    while (valueStart < html.size() && std::isspace(static_cast<unsigned char>(html[valueStart]))) valueStart++;
    if (valueStart >= html.size() || html[valueStart] != '"') {
      pos = colon + 1;
      continue;
    }

    std::string encoded;
    encoded.reserve(1024);
    bool escaped = false;
    for (size_t i = valueStart + 1; i < html.size(); i++) {
      if (encoded.size() >= MAX_DECODE_BUFFER_BYTES) return decodeJsonString(encoded);
      const char c = html[i];
      if (escaped) {
        encoded.push_back('\\');
        encoded.push_back(c);
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        return decodeJsonString(encoded);
      } else {
        encoded.push_back(c);
      }
    }
    return {};
  }
  return {};
}

bool hasOddTrailingBackslashes(const std::string& value) {
  size_t count = 0;
  for (size_t i = value.size(); i > 0 && value[i - 1] == '\\'; i--) count++;
  return count % 2 == 1;
}

std::string htmlToText(std::string_view html);

std::string extractEntityEncodedJsonStringValue(const std::string& html, const char* key) {
  const std::string quotedKey = std::string("&quot;") + key + "&quot;";
  size_t pos = 0;
  while ((pos = findCaseInsensitive(html, quotedKey.c_str(), pos)) != std::string::npos) {
    const size_t colon = html.find(':', pos + quotedKey.size());
    if (colon == std::string::npos) break;
    const size_t valueStart = findCaseInsensitive(html, "&quot;", colon + 1);
    if (valueStart == std::string::npos) break;

    std::string encoded;
    encoded.reserve(1024);
    size_t i = valueStart + 6;
    for (; i < html.size();) {
      if (encoded.size() >= MAX_DECODE_BUFFER_BYTES) {
        const std::string jsonString = decodeEntities(encoded);
        const std::string decoded = decodeJsonString(jsonString);
        if (htmlToText(decoded).size() >= 120) return decoded;
        break;
      }
      if (startsWithAtCaseInsensitive(html, i, "&quot;") && !hasOddTrailingBackslashes(encoded)) {
        const std::string jsonString = decodeEntities(encoded);
        const std::string decoded = decodeJsonString(jsonString);
        if (htmlToText(decoded).size() >= 120) return decoded;
        i += 6;
        break;
      }
      if (startsWithAtCaseInsensitive(html, i, "&quot;")) {
        encoded += "&quot;";
        i += 6;
      } else {
        encoded.push_back(html[i]);
        i++;
      }
    }

    if (i >= html.size() && encoded.size() >= 120) {
      const std::string jsonString = decodeEntities(encoded);
      const std::string decoded = decodeJsonString(jsonString);
      if (htmlToText(decoded).size() >= 120) return decoded;
    }
    pos = i;
  }
  return {};
}

std::string_view largestElementContent(std::string_view html, const char* tag, bool requireContentHint = false) {
  const std::string open = std::string("<") + tag;
  const std::string close = std::string("</") + tag + ">";
  std::string_view best;
  size_t pos = 0;
  while ((pos = findCaseInsensitive(html, open.c_str(), pos)) != std::string::npos) {
    const size_t nameEnd = pos + open.size();
    if (!tagNameBoundary(html, nameEnd)) {
      pos = nameEnd;
      continue;
    }
    const size_t startEnd = findChar(html, '>', pos);
    if (startEnd == std::string::npos) break;
    const std::string tagText(html.substr(pos, startEnd - pos + 1));
    if (requireContentHint && !tagHasContentHint(tagText)) {
      pos = startEnd + 1;
      continue;
    }

    const size_t end = findCaseInsensitive(html, close.c_str(), startEnd + 1);
    if (end == std::string::npos || end <= startEnd) {
      pos = startEnd + 1;
      continue;
    }
    const std::string_view content = html.substr(startEnd + 1, end - startEnd - 1);
    if (content.size() > best.size()) best = content;
    pos = end + close.size();
  }
  return best;
}

void appendSpace(std::string& out) {
  if (out.size() >= MAX_EXTRACTED_TEXT_BYTES) return;
  if (!out.empty() && out.back() != ' ' && out.back() != '\n') out.push_back(' ');
}

void appendBreak(std::string& out) {
  if (out.size() >= MAX_EXTRACTED_TEXT_BYTES) return;
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (out.empty()) return;
  if (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') return;
  if (out.back() != '\n') out.push_back('\n');
  out.push_back('\n');
}

void normalizeTextInto(std::string& normalized, std::string_view text) {
  int consecutiveNewlines = 0;
  bool spacePending = false;
  for (size_t i = 0; i < text.size() && normalized.size() < MAX_EXTRACTED_TEXT_BYTES; i++) {
    const char c = text[i];
    if (c == '<') {
      const size_t tagEnd = findChar(text, '>', i + 1);
      if (tagEnd != std::string::npos && tagEnd - i <= 160) {
        const std::string tagLower =
            toLowerAscii(std::string(text.substr(i + 1, std::min<size_t>(tagEnd - i - 1, 32))));
        // Skip entire content of style/script/svg blocks — they appear here when HTML was
        // entity-encoded inside an article element and then decoded by decodeEntities().
        const char* skipContentTags[] = {"style", "script", "svg"};
        bool skipped = false;
        for (const char* skipTag : skipContentTags) {
          if (tagMatches(tagLower, skipTag)) {
            const std::string closeTag = std::string("</") + skipTag;
            const size_t closeStart = findCaseInsensitive(text, closeTag.c_str(), tagEnd + 1);
            const size_t closeEnd =
                closeStart != std::string::npos ? findChar(text, '>', closeStart) : std::string::npos;
            i = closeEnd != std::string::npos ? closeEnd : text.size() - 1;
            skipped = true;
            break;
          }
        }
        if (skipped) continue;
        if (tagMatches(tagLower, "p") || startsWithAt(tagLower, 0, "/p") || tagMatches(tagLower, "br") ||
            tagMatches(tagLower, "li") || startsWithAt(tagLower, 0, "/div") || tagMatches(tagLower, "h1") ||
            tagMatches(tagLower, "h2") || tagMatches(tagLower, "h3")) {
          while (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
          consecutiveNewlines++;
          if (!normalized.empty() && consecutiveNewlines <= 2) normalized.push_back('\n');
          if (!normalized.empty() && consecutiveNewlines <= 2) normalized.push_back('\n');
          spacePending = false;
        } else if (!normalized.empty() && normalized.back() != '\n') {
          spacePending = true;
        }
        i = tagEnd;
        continue;
      }
    }

    if (c == '\n') {
      while (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
      consecutiveNewlines++;
      if (consecutiveNewlines <= 2) normalized.push_back('\n');
      spacePending = false;
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      if (!normalized.empty() && normalized.back() != '\n') spacePending = true;
    } else {
      if (spacePending && !normalized.empty()) normalized.push_back(' ');
      normalized.push_back(c);
      consecutiveNewlines = 0;
      spacePending = false;
    }
  }
}

std::string htmlToText(std::string_view html) {
  // Scope `out` so it is freed before `normalized` is built.
  // Peak allocation: out(≤MAX) + decoded(≤MAX) during decodeEntities,
  // then decoded(≤MAX) + normalized(≤MAX) during normalizeTextInto = 2×MAX max.
  std::string decoded;
  {
    std::string out;
    out.reserve(std::min<size_t>(html.size(), 2048));
    bool lastSpace = false;

    for (size_t i = 0; i < html.size(); i++) {
      if (out.size() >= MAX_EXTRACTED_TEXT_BYTES) break;
      const char c = html[i];
      if (c == '<') {
        const size_t tagEnd = findChar(html, '>', i + 1);
        if (tagEnd == std::string::npos) break;
        const std::string tagLower =
            toLowerAscii(std::string(html.substr(i + 1, std::min<size_t>(tagEnd - i - 1, 32))));
        const char* skipTags[] = {"script", "style", "noscript", "svg", "nav", "footer", "aside", "template", "iframe"};
        bool skipped = false;
        for (const char* skipTag : skipTags) {
          if (tagMatches(tagLower, skipTag)) {
            const std::string close = std::string("</") + skipTag;
            const size_t closeStart = findCaseInsensitive(html, close.c_str(), tagEnd + 1);
            if (closeStart != std::string::npos) {
              const size_t closeEnd = findChar(html, '>', closeStart);
              i = closeEnd == std::string::npos ? html.size() - 1 : closeEnd;
            } else {
              // Close tag not found — HTML is truncated mid-block. Skip to end of buffer
              // to prevent CSS/script text content from leaking into the output.
              i = html.empty() ? 0 : html.size() - 1;
            }
            appendSpace(out);
            lastSpace = true;
            skipped = true;
            break;
          }
        }
        if (skipped) continue;

        if (tagMatches(tagLower, "p") || startsWithAt(tagLower, 0, "/p") || tagMatches(tagLower, "br") ||
            tagMatches(tagLower, "li") || startsWithAt(tagLower, 0, "/div") || tagMatches(tagLower, "h1") ||
            tagMatches(tagLower, "h2") || tagMatches(tagLower, "h3")) {
          appendBreak(out);
          lastSpace = false;
        } else {
          appendSpace(out);
          lastSpace = true;
        }
        i = tagEnd;
        continue;
      }

      if (std::isspace(static_cast<unsigned char>(c))) {
        if (!lastSpace) {
          out.push_back(' ');
          lastSpace = true;
        }
      } else {
        out.push_back(c);
        lastSpace = false;
      }
    }

    decoded = decodeEntities(out);
    // out goes out of scope here — its heap block is freed before normalized is allocated
  }

  std::string normalized;
  normalized.reserve(std::min<size_t>(decoded.size(), 1024));
  normalizeTextInto(normalized, decoded);
  trimInPlace(normalized);
  return normalized;
}

std::string paragraphText(std::string_view html) {
  std::string out;
  size_t pos = 0;
  while ((pos = findCaseInsensitive(html, "<p", pos)) != std::string::npos) {
    if (out.size() >= MAX_EXTRACTED_TEXT_BYTES) break;
    const size_t nameEnd = pos + 2;
    if (!tagNameBoundary(html, nameEnd)) {
      pos = nameEnd;
      continue;
    }

    const size_t startEnd = findChar(html, '>', pos);
    if (startEnd == std::string::npos) break;
    const size_t end = findCaseInsensitive(html, "</p>", startEnd + 1);
    if (end == std::string::npos) {
      pos = startEnd + 1;
      continue;
    }

    const std::string text = htmlToText(html.substr(startEnd + 1, end - startEnd - 1));
    // Skip tiny boilerplate/link-only paragraphs, but keep short quoted leads
    // once they are part of a larger paragraph set.
    if (text.size() >= 30 || (!out.empty() && text.size() >= 15)) {
      if (!out.empty()) out += "\n\n";
      out.append(text, 0, MAX_EXTRACTED_TEXT_BYTES - std::min(out.size(), MAX_EXTRACTED_TEXT_BYTES));
    }
    pos = end + 4;
  }
  trimInPlace(out);
  return out;
}
// Find the closing tag that correctly matches the opening tag whose '>' is at openTagEnd.
// Counts open/close pairs so nested elements of the same type don't confuse the search.
// Returns std::string::npos if no matching close is found (truncated HTML).
size_t findMatchingClose(std::string_view html, size_t openTagEnd, const char* tag) {
  const std::string openStr = std::string("<") + tag;
  const std::string closeStr = std::string("</") + tag + ">";
  const size_t openLen = openStr.size();
  const size_t closeLen = closeStr.size();
  int depth = 1;
  size_t pos = openTagEnd + 1;
  while (depth > 0) {
    const size_t nextOpen = findCaseInsensitive(html, openStr.c_str(), pos);
    const size_t nextClose = findCaseInsensitive(html, closeStr.c_str(), pos);
    if (nextClose == std::string::npos) return std::string::npos;
    if (nextOpen != std::string::npos && nextOpen < nextClose) {
      const size_t nameEnd = nextOpen + openLen;
      if (tagNameBoundary(html, nameEnd)) depth++;
      const size_t tagEnd = findChar(html, '>', nextOpen);
      pos = tagEnd != std::string::npos ? tagEnd + 1 : nextOpen + 1;
    } else {
      if (--depth == 0) return nextClose;
      pos = nextClose + closeLen;
    }
  }
  return std::string::npos;
}

// --- Readability-inspired content scoring ---
// Metrics computed from the raw HTML of a block element's inner content.
struct TextMetrics {
  size_t textLen = 0;
  size_t commas = 0;
  size_t linkTextLen = 0;
  size_t paragraphs = 0;
};

// Single-pass scan of raw HTML. No heap allocation — all stack locals.
TextMetrics analyzeContent(std::string_view html) {
  TextMetrics m;
  int linkDepth = 0;
  size_t i = 0;
  while (i < html.size()) {
    if (html[i] != '<') {
      const char c = html[i];
      if (!std::isspace(static_cast<unsigned char>(c))) {
        m.textLen++;
        if (c == ',') m.commas++;
        if (linkDepth > 0) m.linkTextLen++;
      }
      i++;
      continue;
    }
    const size_t tagEnd = findChar(html, '>', i + 1);
    if (tagEnd == std::string::npos) break;
    const size_t nameStart = i + 1;
    const bool closing = nameStart < html.size() && html[nameStart] == '/';
    const size_t ns = closing ? nameStart + 1 : nameStart;

    // Skip non-text element content entirely (incl. nav/footer/aside so their
    // links don't inflate link density and disqualify real article blocks)
    const char* skipContentTags[] = {"script", "style", "noscript", "svg", "nav", "footer", "aside"};
    bool skipped = false;
    for (const char* skipTag : skipContentTags) {
      if (!closing && startsWithAtCaseInsensitive(html, ns, skipTag) && tagNameBoundary(html, ns + strlen(skipTag))) {
        char closeTag[12] = "</";
        strncat(closeTag, skipTag, sizeof(closeTag) - 3);
        const size_t closePos = findCaseInsensitive(html, closeTag, tagEnd + 1);
        if (closePos != std::string::npos) {
          const size_t closeEnd = findChar(html, '>', closePos);
          i = closeEnd != std::string::npos ? closeEnd + 1 : html.size();
        } else {
          i = html.size();
        }
        skipped = true;
        break;
      }
    }
    if (skipped) continue;

    // Track <a> depth to measure link density
    if (startsWithAtCaseInsensitive(html, ns, "a") && tagNameBoundary(html, ns + 1)) {
      if (closing) {
        if (linkDepth > 0) linkDepth--;
      } else
        linkDepth++;
    }
    // <p> tags signal prose structure
    if (!closing && startsWithAtCaseInsensitive(html, ns, "p") && tagNameBoundary(html, ns + 1)) {
      m.paragraphs++;
    }
    i = tagEnd + 1;
  }
  return m;
}

// Readability-inspired scoring: text density + comma richness + paragraph count,
// penalised by link density, boosted/nulled by class/id name patterns.
// Uses findCaseInsensitive on string_view — no heap allocation.
int readabilityScore(std::string_view content, std::string_view tagHtml) {
  const TextMetrics m = analyzeContent(content);
  if (m.textLen < 25) return 0;

  const float linkDensity = static_cast<float>(m.linkTextLen) / static_cast<float>(m.textLen);
  if (linkDensity > 0.5f) return 0;  // navigation or link-list block

  int score = static_cast<int>(m.textLen / 10) + static_cast<int>(std::min(m.commas, size_t{10}) * 3) +
              static_cast<int>(m.paragraphs * 2);
  score = static_cast<int>(static_cast<float>(score) * (1.0f - linkDensity));

  // Negative patterns: hard-reject these blocks (from Mozilla Readability pattern list)
  const char* negativeHints[] = {"comment",  "sidebar", "banner", "advertisement", "widget",     "related",
                                 "social",   "footer",  "promo",  "popup",         "newsletter", "subscribe",
                                 "masthead", "sponsor", "combx",  "shoutbox"};
  for (const char* hint : negativeHints) {
    if (findCaseInsensitive(tagHtml, hint) != std::string::npos) return 0;
  }

  // Positive patterns: these blocks likely contain article body text
  const char* positiveHints[] = {"article", "content", "post", "story", "entry", "body", "main", "text", "hentry"};
  for (const char* hint : positiveHints) {
    if (findCaseInsensitive(tagHtml, hint) != std::string::npos) {
      score += 25;
      break;
    }
  }

  return std::max(0, score);
}

// Score every candidate block element and return the inner content of the winner.
// Uses nesting-aware close-tag matching so a div containing nested divs (e.g. image
// blocks) is scored on its full content, not just text before the first inner </div>.
// All nested elements are visited (pos advances past the opening tag, not the close)
// so inner high-scoring blocks can beat a lower-scoring outer wrapper.
// Truncated elements (HTML cut off before the matching close) are still scored on
// whatever content is available — important when the article starts near the download limit.
std::string_view selectReadableContent(std::string_view html) {
  const char* blockTags[] = {"article", "main", "section", "div"};
  std::string_view best;
  int bestScore = 0;

  for (const char* tag : blockTags) {
    const std::string open = std::string("<") + tag;
    size_t pos = 0;
    while ((pos = findCaseInsensitive(html, open.c_str(), pos)) != std::string::npos) {
      const size_t nameEnd = pos + open.size();
      if (!tagNameBoundary(html, nameEnd)) {
        pos = nameEnd;
        continue;
      }
      const size_t startEnd = findChar(html, '>', pos);
      if (startEnd == std::string::npos) break;

      // Nesting-aware: find the true matching close tag.
      // Fall back to end of buffer when HTML is truncated mid-element.
      const size_t end = findMatchingClose(html, startEnd, tag);
      const size_t contentEnd = end != std::string::npos ? end : html.size();

      const std::string_view content = html.substr(startEnd + 1, contentEnd - startEnd - 1);
      const std::string_view tagHtml = html.substr(pos, startEnd - pos + 1);
      const int score = readabilityScore(content, tagHtml);
      if (score > bestScore) {
        bestScore = score;
        best = content;
      }
      pos = startEnd + 1;  // advance past opening tag, not close — visit nested elements
    }
  }
  return best;
}
// --- end Readability scoring ---

}  // namespace

std::string HtmlArticleExtractor::extractReadableText(const std::string& html) {
  if (html.empty()) return {};

  // Try common JSON keys used by news/blog platforms for embedded article content.
  // extractJsonStringValue is case-insensitive on the key name.
  for (const char* key : {"articlebody", "articletext", "bodytext", "bodyhtml", "contenthtml"}) {
    const std::string val = extractJsonStringValue(html, key);
    if (val.size() >= 120) return htmlToText(val);
  }

  const std::string encodedHtmlPayload = extractEntityEncodedJsonStringValue(html, "html");
  if (encodedHtmlPayload.size() >= 120) {
    const std::string text = htmlToText(encodedHtmlPayload);
    if (text.size() >= 120) return text;
  }

  std::string_view htmlView(html);

  // Readability-style: score all block elements and pick the best by text quality
  std::string_view content = selectReadableContent(htmlView);
  if (content.empty()) content = largestElementContent(htmlView, "body");
  if (content.empty()) content = htmlView;

  std::string text = htmlToText(content);
  if (text.size() >= 120) return text;

  std::string_view body = largestElementContent(htmlView, "body");
  if (body.empty()) body = htmlView;
  text = paragraphText(body);
  return text.size() >= 120 ? text : std::string{};
}
