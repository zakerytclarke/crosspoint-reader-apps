#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"  // For UIIcon

class App {
 public:
  using Factory = std::function<std::unique_ptr<Activity>(GfxRenderer&, MappedInputManager&)>;
  using NameGetter = std::function<std::string()>;
  using VisibleCallback = std::function<bool()>;

  App(NameGetter nameGetter, UIIcon icon, Factory factory, VisibleCallback visibleCallback = nullptr)
      : nameGetter(std::move(nameGetter)),
        icon(icon),
        factory(std::move(factory)),
        visibleCallback(std::move(visibleCallback)) {}

  App(std::string name, UIIcon icon, Factory factory)
      : nameGetter([n = std::move(name)]() { return n; }),
        icon(icon),
        factory(std::move(factory)),
        visibleCallback(nullptr) {}

  std::string getName() const { return nameGetter(); }
  UIIcon getIcon() const { return icon; }
  bool isVisible() const { return visibleCallback ? visibleCallback() : true; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) const {
    return factory(renderer, mappedInput);
  }

 private:
  NameGetter nameGetter;
  UIIcon icon;
  Factory factory;
  VisibleCallback visibleCallback;
};
