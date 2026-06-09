#include <gtest/gtest.h>

#include <string>

#include "HtmlArticleExtractor.h"

TEST(HtmlArticleExtractor, PrefersArticleContentAndRemovesChrome) {
  const std::string html = R"html(
    <html>
      <body>
        <nav>Menu Links Everywhere</nav>
        <article>
          <h1>Article title</h1>
          <p>First paragraph &amp; useful context for the reader.</p>
          <script>tracking()</script>
          <p>Second paragraph with enough text to pass the minimum useful article threshold.</p>
        </article>
        <footer>Footer links</footer>
      </body>
    </html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("Article title"), std::string::npos);
  EXPECT_NE(text.find("First paragraph & useful context"), std::string::npos);
  EXPECT_NE(text.find("Second paragraph"), std::string::npos);
  EXPECT_EQ(text.find("Menu Links"), std::string::npos);
  EXPECT_EQ(text.find("tracking"), std::string::npos);
  EXPECT_EQ(text.find("Footer links"), std::string::npos);
}

TEST(HtmlArticleExtractor, FallsBackToBody) {
  const std::string html = R"html(
    <html>
      <body>
        <p>This body has no article tag, but it still has enough readable article-like text to extract.</p>
        <p>The second paragraph helps make the body content long enough to be considered useful.</p>
      </body>
    </html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("no article tag"), std::string::npos);
  EXPECT_NE(text.find("second paragraph"), std::string::npos);
}

TEST(HtmlArticleExtractor, PicksLargestArticleBlock) {
  const std::string html = R"html(
    <html><body>
      <article><p>Short teaser.</p></article>
      <article>
        <p>This is the real article body with enough text to be useful on the reader.</p>
        <p>It should win over the teaser because it has the largest article element content.</p>
      </article>
    </body></html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("real article body"), std::string::npos);
  EXPECT_EQ(text.find("Short teaser"), std::string::npos);
}

TEST(HtmlArticleExtractor, UsesArticleBodyJsonLd) {
  const std::string html = R"html(
    <html><head>
      <script type="application/ld+json">
        {"@type":"NewsArticle","articleBody":"Full article text from JSON-LD. This has enough sentence content to be useful when the rendered HTML is mostly client-side chrome and empty containers."}
      </script>
    </head><body><div id="app"></div></body></html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("Full article text from JSON-LD"), std::string::npos);
}

TEST(HtmlArticleExtractor, UsesEntityEncodedHtmlPayload) {
  const std::string html = R"html(
    <html><body>
      <astro-island props="{&quot;info&quot;:[0,{&quot;html&quot;:[0,&quot;&lt;p&gt;Article body from an Astro payload with &lt;a href=\&quot;https://example.com\&quot;&gt;links&lt;/a&gt; inside it.&lt;/p&gt;&lt;p&gt;The second paragraph makes this payload long enough to be accepted as the readable article text.&lt;/p&gt;&quot;]}]}"></astro-island>
      <p>Comments</p>
    </body></html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("Article body from an Astro payload"), std::string::npos);
  EXPECT_NE(text.find("second paragraph"), std::string::npos);
}

TEST(HtmlArticleExtractor, SkipsTinyEntityEncodedHtmlPayloads) {
  const std::string html = R"html(
    <html><body>
      <astro-island props="{&quot;html&quot;:&quot;&lt;p&gt;Tiny widget.&lt;/p&gt;&quot;,&quot;info&quot;:[0,{&quot;html&quot;:[0,&quot;&lt;p&gt;The larger article payload should win over small embedded widgets on modern generated pages.&lt;/p&gt;&lt;p&gt;This paragraph gives the reader enough text to prove we selected the body payload instead.&lt;/p&gt;&quot;]}]}"></astro-island>
    </body></html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("larger article payload"), std::string::npos);
  EXPECT_EQ(text.find("Tiny widget"), std::string::npos);
}

TEST(HtmlArticleExtractor, UsesTruncatedEntityEncodedHtmlPayload) {
  const std::string html = R"html(
    <html><body>
      <astro-island props="{&quot;info&quot;:[0,{&quot;html&quot;:[0,&quot;&lt;p&gt;Article text can start before the firmware download limit and continue past the truncated end.&lt;/p&gt;&lt;p&gt;The extractor should still return the useful text it already has instead of falling back to the short feed description.&lt;/p&gt;
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("firmware download limit"), std::string::npos);
  EXPECT_NE(text.find("short feed description"), std::string::npos);
}

TEST(HtmlArticleExtractor, RemovesTagsThatAppearAfterEntityDecode) {
  const std::string html = R"html(
    <html><body>
      <article>
        &lt;p&gt;Entity encoded paragraphs should be converted into clean reader text, not visible HTML tags.&lt;/p&gt;
        &lt;p&gt;The second paragraph gives enough content to pass the useful text threshold.&lt;/p&gt;
      </article>
    </body></html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("Entity encoded paragraphs"), std::string::npos);
  EXPECT_NE(text.find("second paragraph"), std::string::npos);
  EXPECT_EQ(text.find("<p>"), std::string::npos);
  EXPECT_EQ(text.find("</p>"), std::string::npos);
}

TEST(HtmlArticleExtractor, UsesContentHintDiv) {
  const std::string html = R"html(
    <html><body>
      <div class="sidebar">Lots of unrelated sidebar links links links links links links links links.</div>
      <div class="article-content">
        <p>Main story text lives in a content div on this site instead of a semantic article element.</p>
        <p>This second paragraph makes the extracted story long enough and should be selected.</p>
      </div>
    </body></html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("Main story text"), std::string::npos);
  EXPECT_EQ(text.find("sidebar links"), std::string::npos);
}

TEST(HtmlArticleExtractor, FallsBackToBodyParagraphs) {
  const std::string html = R"html(
    <html><body>
      <header>Navigation and site branding.</header>
      <div class="layout">
        <aside>Related links and other clutter that should not be enough by itself.</aside>
        <p>The article starts here in plain paragraphs without a helpful semantic wrapper around it.</p>
        <p>The next paragraph contains the rest of the story, enough detail, and enough length to be useful.</p>
        <p>A final paragraph confirms that paragraph fallback can recover full article text from simple pages.</p>
      </div>
    </body></html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("article starts here"), std::string::npos);
  EXPECT_NE(text.find("next paragraph"), std::string::npos);
  EXPECT_NE(text.find("final paragraph"), std::string::npos);
}

TEST(HtmlArticleExtractor, RejectsTinyFragments) {
  EXPECT_TRUE(HtmlArticleExtractor::extractReadableText("<html><body><p>Too short.</p></body></html>").empty());
}

TEST(HtmlArticleExtractor, CapturesContentAfterNestedDiv) {
  // Articles often contain nested divs (image blocks, pull quotes) before the
  // remaining paragraphs. The extractor must not stop at the first </div>.
  const std::string html = R"html(
    <html><body>
      <div class="entry-content">
        <p>Opening paragraph sets up the story with enough length to be useful.</p>
        <div class="wp-block-image"><img src="photo.jpg" alt="photo"></div>
        <p>Middle paragraph continues after the image block and adds more context.</p>
        <p>Final paragraph closes the article and confirms full content extraction.</p>
      </div>
    </body></html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("Opening paragraph"), std::string::npos);
  EXPECT_NE(text.find("Middle paragraph"), std::string::npos);
  EXPECT_NE(text.find("Final paragraph"), std::string::npos);
}

TEST(HtmlArticleExtractor, IgnoresNavLinksWhenScoring) {
  // Navigation inside an article wrapper should not inflate link density enough
  // to disqualify the block from being selected as article content.
  const std::string html = R"html(
    <html><body>
      <article>
        <nav><a href="/1">One</a> <a href="/2">Two</a> <a href="/3">Three</a></nav>
        <p>Real article text starts here and has enough length to demonstrate that
        nav link density does not disqualify this article element from selection.</p>
        <p>Second paragraph confirms the article body is returned, not an empty result.</p>
      </article>
    </body></html>
  )html";

  const std::string text = HtmlArticleExtractor::extractReadableText(html);
  EXPECT_NE(text.find("Real article text"), std::string::npos);
  EXPECT_NE(text.find("Second paragraph"), std::string::npos);
}
