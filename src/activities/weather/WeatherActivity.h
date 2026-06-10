#pragma once

#include <functional>
#include <string>

#include "activities/Activity.h"

class WeatherActivity final : public Activity {
 public:
  enum class WeatherState { Init, SelectCity, Loading, ShowWeather };

 private:
  WeatherState state = WeatherState::Init;
  int selectedCityIndex = 0;
  bool weatherLoaded = false;
  double temp = 0.0;
  double windspeed = 0.0;
  int weatherCode = 0;
  std::string timeStr;
  std::string cityName;
  std::string errorMessage;
  bool shouldFetch = false;

  double fetchedTemp = 0.0;
  double fetchedWindspeed = 0.0;
  int fetchedWeatherCode = 0;
  std::string fetchedTimeStr;
  bool backgroundFetchSuccess = false;
  volatile bool pendingUpdateWeather = false;
  void* fetchTaskHandle = nullptr;

  void performFetch();
  void cancelFetchTask();

 public:
  void runBackgroundFetch();
  explicit WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
