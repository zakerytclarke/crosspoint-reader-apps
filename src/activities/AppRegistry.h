#pragma once

#include <memory>
#include <vector>

#include "App.h"

class AppRegistry {
 public:
  static AppRegistry& getInstance();
  const std::vector<std::unique_ptr<App>>& getApps() const { return apps; }

 private:
  AppRegistry();
  std::vector<std::unique_ptr<App>> apps;
};
