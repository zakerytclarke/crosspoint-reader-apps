#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "activities/Activity.h"

enum class DDGState { OfflineList, SearchResults, Loading };

struct DuckLink {
  std::string title;
  std::string url;
};

class DuckDuckGoActivity final : public Activity {
 private:
  DDGState state = DDGState::OfflineList;
  std::vector<std::string> offlineWebsites;
  std::vector<DuckLink> searchResults;

  std::string searchQuery;
  std::string errorMessage;

  int selectedIndex = 0;
  int listScrollOffset = 0;

  void* fetchTaskHandle = nullptr;
  bool cancelFetch = false;
  bool wifiWasUsed = false;
  bool pendingUpdateSearch = false;
  bool backgroundFetchFailed = false;
  std::vector<DuckLink> bgSearchResults;

  void loadOfflineWebsitesList();
  void performBackgroundSearch();
  void downloadActivePost();

 public:
  void runBackgroundFetch();
  bool fetchSearchData();
  explicit DuckDuckGoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DuckDuckGo", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
