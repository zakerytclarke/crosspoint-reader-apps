#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "activities/Activity.h"

enum class WikiState { OfflineList, SearchResults, Loading, ArticleView };

class WikipediaActivity final : public Activity {
 private:
  WikiState state = WikiState::OfflineList;
  std::vector<std::string> offlineArticles;
  std::vector<std::string> searchResults;

  std::string currentArticleTitle;
  std::string currentArticleText;
  std::vector<std::string> articleLines;

  int selectedIndex = 0;
  int listScrollOffset = 0;
  int articleScrollOffset = 0;  // lines scrolled down in ArticleView

  bool pendingSearch = false;
  bool pendingArticle = false;
  bool wifiConnecting = false;
  std::string searchQuery;
  std::string articleToFetch;
  std::string errorMessage;

  void* fetchTaskHandle = nullptr;
  std::atomic<bool> cancelFetch{false};
  bool wifiWasUsed = false;
  bool pendingUpdateSearch = false;
  bool pendingUpdateArticle = false;
  bool backgroundFetchFailed = false;
  bool isSearchTask = false;
  std::vector<std::string> bgSearchResults;

  void loadOfflineArticlesList();
  void performBackgroundSearch();
  void performBackgroundFetchArticle();

 public:
  void runBackgroundFetch();
  bool fetchSearchData();
  bool fetchArticleData();
  explicit WikipediaActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Wikipedia", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
