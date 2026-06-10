#include "WeatherActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

#include <cstdlib>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/WifiConnectHelper.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
namespace {
struct City {
  const char* name;
  double lat;
  double lon;
};

const City CITIES[] = {
    // Major world cities with latitude / longitude
    // Format: {"City", latitude, longitude},

    // =========================
    // UNITED STATES
    // =========================
    {"New York", 40.7128, -74.0060},
    {"Los Angeles", 34.0522, -118.2437},
    {"Chicago", 41.8781, -87.6298},
    {"Houston", 29.7604, -95.3698},
    {"Phoenix", 33.4484, -112.0740},
    {"Philadelphia", 39.9526, -75.1652},
    {"San Antonio", 29.4241, -98.4936},
    {"San Diego", 32.7157, -117.1611},
    {"Dallas", 32.7767, -96.7970},
    {"Austin", 30.2672, -97.7431},
    {"Jacksonville", 30.3322, -81.6557},
    {"Fort Worth", 32.7555, -97.3308},
    {"San Jose", 37.3382, -121.8863},
    {"Columbus", 39.9612, -82.9988},
    {"Charlotte", 35.2271, -80.8431},
    {"Indianapolis", 39.7684, -86.1581},
    {"San Francisco", 37.7749, -122.4194},
    {"Seattle", 47.6062, -122.3321},
    {"Denver", 39.7392, -104.9903},
    {"Washington DC", 38.9072, -77.0369},
    {"Boston", 42.3601, -71.0589},
    {"Miami", 25.7617, -80.1918},
    {"Atlanta", 33.7490, -84.3880},
    {"Detroit", 42.3314, -83.0458},
    {"Minneapolis", 44.9778, -93.2650},
    {"Las Vegas", 36.1699, -115.1398},
    {"Portland", 45.5152, -122.6784},
    {"Nashville", 36.1627, -86.7816},
    {"New Orleans", 29.9511, -90.0715},
    {"Baltimore", 39.2904, -76.6122},
    {"Cleveland", 41.4993, -81.6944},
    {"Pittsburgh", 40.4406, -79.9959},
    {"Cincinnati", 39.1031, -84.5120},
    {"Kansas City", 39.0997, -94.5786},
    {"St. Louis", 38.6270, -90.1994},
    {"Salt Lake City", 40.7608, -111.8910},
    {"Orlando", 28.5383, -81.3792},
    {"Tampa", 27.9506, -82.4572},
    {"Sacramento", 38.5816, -121.4944},
    {"Raleigh", 35.7796, -78.6382},
    {"Milwaukee", 43.0389, -87.9065},
    {"Buffalo", 42.8864, -78.8784},
    {"Memphis", 35.1495, -90.0490},
    {"Oklahoma City", 35.4676, -97.5164},
    {"Albuquerque", 35.0844, -106.6504},
    {"Honolulu", 21.3069, -157.8583},
    {"Anchorage", 61.2181, -149.9003},

    // =========================
    // CANADA
    // =========================
    {"Toronto", 43.6532, -79.3832},
    {"Montreal", 45.5017, -73.5673},
    {"Vancouver", 49.2827, -123.1207},
    {"Calgary", 51.0447, -114.0719},
    {"Ottawa", 45.4215, -75.6972},
    {"Edmonton", 53.5461, -113.4938},
    {"Quebec City", 46.8139, -71.2080},
    {"Winnipeg", 49.8951, -97.1384},
    {"Halifax", 44.6488, -63.5752},

    // =========================
    // MEXICO
    // =========================
    {"Mexico City", 19.4326, -99.1332},
    {"Guadalajara", 20.6597, -103.3496},
    {"Monterrey", 25.6866, -100.3161},
    {"Cancun", 21.1619, -86.8515},
    {"Tijuana", 32.5149, -117.0382},

    // =========================
    // SOUTH AMERICA
    // =========================
    {"Bogota", 4.7110, -74.0721},
    {"Medellin", 6.2442, -75.5812},
    {"Lima", -12.0464, -77.0428},
    {"Santiago", -33.4489, -70.6693},
    {"Buenos Aires", -34.6037, -58.3816},
    {"Cordoba", -31.4201, -64.1888},
    {"Rio de Janeiro", -22.9068, -43.1729},
    {"Sao Paulo", -23.5505, -46.6333},
    {"Brasilia", -15.7939, -47.8828},
    {"Salvador", -12.9777, -38.5016},
    {"Quito", -0.1807, -78.4678},
    {"Caracas", 10.4806, -66.9036},
    {"Montevideo", -34.9011, -56.1645},
    {"La Paz", -16.4897, -68.1193},

    // =========================
    // UNITED KINGDOM & IRELAND
    // =========================
    {"London", 51.5074, -0.1278},
    {"Manchester", 53.4808, -2.2426},
    {"Birmingham", 52.4862, -1.8904},
    {"Glasgow", 55.8642, -4.2518},
    {"Edinburgh", 55.9533, -3.1883},
    {"Dublin", 53.3498, -6.2603},
    {"Belfast", 54.5973, -5.9301},

    // =========================
    // FRANCE
    // =========================
    {"Paris", 48.8566, 2.3522},
    {"Lyon", 45.7640, 4.8357},
    {"Marseille", 43.2965, 5.3698},
    {"Nice", 43.7102, 7.2620},
    {"Toulouse", 43.6047, 1.4442},

    // =========================
    // GERMANY
    // =========================
    {"Berlin", 52.5200, 13.4050},
    {"Munich", 48.1351, 11.5820},
    {"Hamburg", 53.5511, 9.9937},
    {"Frankfurt", 50.1109, 8.6821},
    {"Cologne", 50.9375, 6.9603},
    {"Stuttgart", 48.7758, 9.1829},
    {"Dusseldorf", 51.2277, 6.7735},

    // =========================
    // ITALY
    // =========================
    {"Rome", 41.9028, 12.4964},
    {"Milan", 45.4642, 9.1900},
    {"Naples", 40.8518, 14.2681},
    {"Turin", 45.0703, 7.6869},
    {"Florence", 43.7696, 11.2558},
    {"Venice", 45.4408, 12.3155},

    // =========================
    // SPAIN & PORTUGAL
    // =========================
    {"Madrid", 40.4168, -3.7038},
    {"Barcelona", 41.3851, 2.1734},
    {"Valencia", 39.4699, -0.3763},
    {"Seville", 37.3891, -5.9845},
    {"Lisbon", 38.7223, -9.1393},
    {"Porto", 41.1579, -8.6291},

    // =========================
    // NETHERLANDS / BELGIUM / SWITZERLAND / AUSTRIA
    // =========================
    {"Amsterdam", 52.3676, 4.9041},
    {"Rotterdam", 51.9244, 4.4777},
    {"Brussels", 50.8503, 4.3517},
    {"Zurich", 47.3769, 8.5417},
    {"Geneva", 46.2044, 6.1432},
    {"Vienna", 48.2082, 16.3738},

    // =========================
    // SCANDINAVIA
    // =========================
    {"Stockholm", 59.3293, 18.0686},
    {"Oslo", 59.9139, 10.7522},
    {"Copenhagen", 55.6761, 12.5683},
    {"Helsinki", 60.1699, 24.9384},
    {"Reykjavik", 64.1466, -21.9426},

    // =========================
    // EASTERN EUROPE
    // =========================
    {"Warsaw", 52.2297, 21.0122},
    {"Prague", 50.0755, 14.4378},
    {"Budapest", 47.4979, 19.0402},
    {"Kyiv", 50.4501, 30.5234},
    {"Moscow", 55.7558, 37.6173},
    {"Saint Petersburg", 59.9311, 30.3609},
    {"Bucharest", 44.4268, 26.1025},
    {"Belgrade", 44.7866, 20.4489},

    // =========================
    // TURKEY & MIDDLE EAST
    // =========================
    {"Istanbul", 41.0082, 28.9784},
    {"Ankara", 39.9334, 32.8597},
    {"Dubai", 25.2048, 55.2708},
    {"Abu Dhabi", 24.4539, 54.3773},
    {"Doha", 25.2854, 51.5310},
    {"Riyadh", 24.7136, 46.6753},
    {"Jeddah", 21.4858, 39.1925},
    {"Tel Aviv", 32.0853, 34.7818},
    {"Jerusalem", 31.7683, 35.2137},
    {"Tehran", 35.6892, 51.3890},
    {"Baghdad", 33.3152, 44.3661},
    {"Kuwait City", 29.3759, 47.9774},

    // =========================
    // AFRICA
    // =========================
    {"Cairo", 30.0444, 31.2357},
    {"Alexandria", 31.2001, 29.9187},
    {"Cape Town", -33.9249, 18.4241},
    {"Johannesburg", -26.2041, 28.0473},
    {"Durban", -29.8587, 31.0218},
    {"Lagos", 6.5244, 3.3792},
    {"Nairobi", -1.2921, 36.8219},
    {"Casablanca", 33.5731, -7.5898},
    {"Marrakesh", 31.6295, -7.9811},
    {"Addis Ababa", 8.9806, 38.7578},
    {"Accra", 5.6037, -0.1870},
    {"Tunis", 36.8065, 10.1815},

    // =========================
    // INDIA
    // =========================
    {"Delhi", 28.6139, 77.2090},
    {"Mumbai", 19.0760, 72.8777},
    {"Bangalore", 12.9716, 77.5946},
    {"Hyderabad", 17.3850, 78.4867},
    {"Chennai", 13.0827, 80.2707},
    {"Kolkata", 22.5726, 88.3639},
    {"Pune", 18.5204, 73.8567},
    {"Ahmedabad", 23.0225, 72.5714},
    {"Jaipur", 26.9124, 75.7873},

    // =========================
    // CHINA
    // =========================
    {"Beijing", 39.9042, 116.4074},
    {"Shanghai", 31.2304, 121.4737},
    {"Shenzhen", 22.5431, 114.0579},
    {"Guangzhou", 23.1291, 113.2644},
    {"Hong Kong", 22.3193, 114.1694},
    {"Chengdu", 30.5728, 104.0668},
    {"Wuhan", 30.5928, 114.3055},
    {"Hangzhou", 30.2741, 120.1551},
    {"Xi'an", 34.3416, 108.9398},
    {"Nanjing", 32.0603, 118.7969},
    {"Tianjin", 39.3434, 117.3616},
    {"Chongqing", 29.4316, 106.9123},

    // =========================
    // JAPAN
    // =========================
    {"Tokyo", 35.6762, 139.6503},
    {"Osaka", 34.6937, 135.5023},
    {"Kyoto", 35.0116, 135.7681},
    {"Yokohama", 35.4437, 139.6380},
    {"Nagoya", 35.1815, 136.9066},
    {"Sapporo", 43.0618, 141.3545},
    {"Fukuoka", 33.5902, 130.4017},

    // =========================
    // SOUTH KOREA
    // =========================
    {"Seoul", 37.5665, 126.9780},
    {"Busan", 35.1796, 129.0756},
    {"Incheon", 37.4563, 126.7052},

    // =========================
    // SOUTHEAST ASIA
    // =========================
    {"Singapore", 1.3521, 103.8198},
    {"Bangkok", 13.7563, 100.5018},
    {"Kuala Lumpur", 3.1390, 101.6869},
    {"Jakarta", -6.2088, 106.8456},
    {"Manila", 14.5995, 120.9842},
    {"Ho Chi Minh City", 10.8231, 106.6297},
    {"Hanoi", 21.0278, 105.8342},
    {"Phnom Penh", 11.5564, 104.9282},
    {"Yangon", 16.8409, 96.1735},

    // =========================
    // OCEANIA
    // =========================
    {"Sydney", -33.8688, 151.2093},
    {"Melbourne", -37.8136, 144.9631},
    {"Brisbane", -27.4698, 153.0251},
    {"Perth", -31.9505, 115.8605},
    {"Adelaide", -34.9285, 138.6007},
    {"Auckland", -36.8485, 174.7633},
    {"Wellington", -41.2866, 174.7756},
    {"Christchurch", -43.5321, 172.6362},
};
const int CITY_COUNT = sizeof(CITIES) / sizeof(CITIES[0]);

const char* getWeatherDesc(int code) {
  switch (code) {
    case 0:
      return "Clear sky";
    case 1:
      return "Mainly clear";
    case 2:
      return "Partly cloudy";
    case 3:
      return "Overcast";
    case 45:
    case 48:
      return "Foggy";
    case 51:
    case 53:
    case 55:
      return "Drizzle";
    case 61:
    case 63:
    case 65:
      return "Rainy";
    case 71:
    case 73:
    case 75:
      return "Snowy";
    case 80:
    case 81:
    case 82:
      return "Rain showers";
    case 95:
    case 96:
    case 99:
      return "Thunderstorm";
    default:
      return "Unknown";
  }
}

void saveConfig(int cityIndex, const std::string& cityName) {
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/weather");
  JsonDocument doc;
  doc["city_index"] = cityIndex;
  doc["city_name"] = cityName;
  String output;
  serializeJson(doc, output);
  Storage.writeFile("/apps/weather/config.json", output);
}

bool loadConfig(int& cityIndex, std::string& cityName) {
  String input = Storage.readFile("/apps/weather/config.json");
  if (input.length() == 0) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, input);
  if (err) return false;
  cityIndex = doc["city_index"] | 0;
  cityName = doc["city_name"] | "";
  return true;
}

std::string getSafeCityFilename(const std::string& name) {
  std::string safe = "";
  for (char c : name) {
    if (std::isalnum(c)) {
      safe += std::tolower(c);
    } else if (c == ' ') {
      safe += '_';
    }
  }
  return safe;
}

void saveCache(const std::string& cityName, double temp, double windspeed, int weathercode,
               const std::string& timeStr) {
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/weather");
  JsonDocument doc;
  doc["temp"] = temp;
  doc["windspeed"] = windspeed;
  doc["weathercode"] = weathercode;
  doc["time"] = timeStr;
  String output;
  serializeJson(doc, output);
  std::string filepath = "/apps/weather/" + getSafeCityFilename(cityName) + ".txt";
  Storage.writeFile(filepath.c_str(), output);
}

bool loadCache(const std::string& cityName, double& temp, double& windspeed, int& weathercode, std::string& timeStr) {
  std::string filepath = "/apps/weather/" + getSafeCityFilename(cityName) + ".txt";
  String input = Storage.readFile(filepath.c_str());
  if (input.length() == 0) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, input);
  if (err) return false;
  temp = doc["temp"] | 0.0;
  windspeed = doc["windspeed"] | 0.0;
  weathercode = doc["weathercode"] | 0;
  timeStr = doc["time"] | "";
  return true;
}

bool fetchWeather(double lat, double lon, double& temp, double& windspeed, int& weathercode, std::string& timeStr) {
  char url[256];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/"
           "forecast?latitude=%.4f&longitude=%.4f&current_weather=true",
           lat, lon);
  std::string response;
  if (!HttpDownloader::fetchUrl(url, response)) {
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) return false;

  if (doc["current_weather"].isNull()) return false;
  auto cw = doc["current_weather"];
  temp = cw["temperature"] | 0.0;
  windspeed = cw["windspeed"] | 0.0;
  weathercode = cw["weathercode"] | 0;
  timeStr = cw["time"] | "";
  return true;
}

void drawWeatherIcon(const GfxRenderer& r, int cx, int cy, int code) {
  switch (code) {
    case 0:  // Clear sky
    case 1:  // Mainly clear
      // Draw Sun
      r.fillRoundedRect(cx - 20, cy - 20, 40, 40, 20, Color::Black);
      r.fillRoundedRect(cx - 15, cy - 15, 30, 30, 15, Color::White);
      r.fillRoundedRect(cx - 10, cy - 10, 20, 20, 10, Color::Black);

      // Ray lines
      r.drawLine(cx, cy - 30, cx, cy - 24, 2, true);
      r.drawLine(cx, cy + 24, cx, cy + 30, 2, true);
      r.drawLine(cx - 30, cy, cx - 24, cy, 2, true);
      r.drawLine(cx + 24, cy, cx + 30, cy, 2, true);
      r.drawLine(cx - 21, cy - 21, cx - 17, cy - 17, 2, true);
      r.drawLine(cx + 17, cy - 17, cx + 21, cy - 21, 2, true);
      r.drawLine(cx - 21, cy + 21, cx - 17, cy + 17, 2, true);
      r.drawLine(cx + 17, cy + 17, cx + 21, cy + 21, 2, true);
      break;

    case 2:  // Partly cloudy
      // Draw Sun behind cloud
      r.fillRoundedRect(cx + 12 - 15, cy - 12 - 15, 30, 30, 15, Color::Black);
      r.fillRoundedRect(cx + 12 - 11, cy - 12 - 11, 22, 22, 11, Color::White);
      r.fillRoundedRect(cx + 12 - 7, cy - 12 - 7, 14, 14, 7, Color::Black);

      // Sun rays
      r.drawLine(cx + 12, cy - 31, cx + 12, cy - 27, 2, true);
      r.drawLine(cx + 31, cy - 12, cx + 35, cy - 12, 2, true);
      r.drawLine(cx + 25, cy - 25, cx + 28, cy - 28, 2, true);

      // White mask to separate cloud and sun
      r.fillRoundedRect(cx - 32, cy - 2, 64, 32, 16, Color::White);

      // Draw Cloud in front
      r.fillRect(cx - 25, cy + 5, 50, 15, true);
      r.fillRoundedRect(cx - 25, cy - 8, 24, 24, 12, Color::Black);
      r.fillRoundedRect(cx - 12, cy - 16, 30, 30, 15, Color::Black);
      r.fillRoundedRect(cx + 10, cy - 4, 18, 18, 9, Color::Black);
      break;

    case 3:   // Overcast
    case 45:  // Foggy
    case 48:
      // Draw Cloud
      r.fillRect(cx - 30, cy, 60, 20, true);
      r.fillRoundedRect(cx - 30, cy - 12, 30, 30, 15, Color::Black);
      r.fillRoundedRect(cx - 15, cy - 22, 38, 38, 19, Color::Black);
      r.fillRoundedRect(cx + 10, cy - 6, 24, 24, 12, Color::Black);

      if (code == 45 || code == 48) {
        // Fog lines below
        r.drawLine(cx - 35, cy + 24, cx + 35, cy + 24, 2, true);
        r.drawLine(cx - 25, cy + 30, cx + 25, cy + 30, 2, true);
      }
      break;

    case 51:  // Drizzle
    case 53:  // Drizzle
    case 55:  // Drizzle
    case 61:  // Rainy
    case 63:  // Rainy
    case 65:  // Rainy
    case 80:  // Rain showers
    case 81:
    case 82:
      // Draw Cloud
      r.fillRect(cx - 28, cy - 5, 56, 20, true);
      r.fillRoundedRect(cx - 28, cy - 15, 28, 28, 14, Color::Black);
      r.fillRoundedRect(cx - 14, cy - 24, 36, 36, 18, Color::Black);
      r.fillRoundedRect(cx + 10, cy - 9, 22, 22, 11, Color::Black);

      // Slanted raindrops
      r.drawLine(cx - 18, cy + 18, cx - 22, cy + 26, 2, true);
      r.drawLine(cx - 6, cy + 18, cx - 10, cy + 26, 2, true);
      r.drawLine(cx + 8, cy + 18, cx + 4, cy + 26, 2, true);
      r.drawLine(cx + 20, cy + 18, cx + 16, cy + 26, 2, true);
      break;

    case 71:  // Snowy
    case 73:
    case 75:
      // Draw Cloud
      r.fillRect(cx - 28, cy - 5, 56, 20, true);
      r.fillRoundedRect(cx - 28, cy - 15, 28, 28, 14, Color::Black);
      r.fillRoundedRect(cx - 14, cy - 24, 36, 36, 18, Color::Black);
      r.fillRoundedRect(cx + 10, cy - 9, 22, 22, 11, Color::Black);

      // Snowflakes (small cross/asterisks)
      r.drawLine(cx - 17, cy + 21, cx - 11, cy + 21, 2, true);
      r.drawLine(cx - 14, cy + 18, cx - 14, cy + 24, 2, true);

      r.drawLine(cx - 3, cy + 24, cx + 3, cy + 24, 2, true);
      r.drawLine(cx, cy + 21, cx, cy + 27, 2, true);

      r.drawLine(cx + 11, cy + 21, cx + 17, cy + 21, 2, true);
      r.drawLine(cx + 14, cy + 18, cx + 14, cy + 24, 2, true);
      break;

    case 95:  // Thunderstorm
    case 96:
    case 99:
      // Draw Cloud
      r.fillRect(cx - 28, cy - 5, 56, 20, true);
      r.fillRoundedRect(cx - 28, cy - 15, 28, 28, 14, Color::Black);
      r.fillRoundedRect(cx - 14, cy - 24, 36, 36, 18, Color::Black);
      r.fillRoundedRect(cx + 10, cy - 9, 22, 22, 11, Color::Black);

      // Lightning bolt
      r.drawLine(cx - 3, cy + 16, cx + 5, cy + 23, 2, true);
      r.drawLine(cx + 5, cy + 23, cx - 5, cy + 23, 2, true);
      r.drawLine(cx - 5, cy + 23, cx + 1, cy + 31, 2, true);
      break;

    default:
      // Unknown: Draw a question mark outline
      r.drawRoundedRect(cx - 20, cy - 20, 40, 40, 2, 20, true);
      r.drawCenteredText(NOTOSANS_18_FONT_ID, cy - 9, "?", true, EpdFontFamily::BOLD);
      break;
  }
}
}  // namespace

namespace {
static void weatherFetchTaskFunc(void* param) {
  WeatherActivity* activity = static_cast<WeatherActivity*>(param);
  activity->runBackgroundFetch();
  vTaskDelete(nullptr);
}
}  // namespace

void WeatherActivity::runBackgroundFetch() {
  if (WiFi.status() == WL_CONNECTED) {
    // Add a stabilization delay for DNS resolution
    delay(500);

    double lat = CITIES[selectedCityIndex].lat;
    double lon = CITIES[selectedCityIndex].lon;

    int fetchRetries = 3;
    backgroundFetchSuccess = false;
    while (fetchRetries > 0) {
      if (fetchWeather(lat, lon, fetchedTemp, fetchedWindspeed, fetchedWeatherCode, fetchedTimeStr)) {
        backgroundFetchSuccess = true;
        break;
      }
      fetchRetries--;
      if (fetchRetries > 0) {
        delay(1000);
      }
    }
  } else {
    backgroundFetchSuccess = false;
  }
  pendingUpdateWeather = true;
}

void WeatherActivity::onEnter() {
  Activity::onEnter();
  Storage.ensureDirectoryExists("/apps");
  Storage.ensureDirectoryExists("/apps/weather");

  errorMessage.clear();
  pendingUpdateWeather = false;
  fetchTaskHandle = nullptr;

  if (loadConfig(selectedCityIndex, cityName)) {
    if (loadCache(cityName, temp, windspeed, weatherCode, timeStr)) {
      weatherLoaded = true;
      state = WeatherState::ShowWeather;
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        performFetch();
      }
    } else {
      weatherLoaded = false;
      errorMessage = "No cached weather data. Fetching...";
      state = WeatherState::Loading;
      requestUpdate();
      ensureWifiConnected([this]() { performFetch(); },
                          [this]() {
                            state = WeatherState::SelectCity;
                            requestUpdate();
                          });
    }
  } else {
    state = WeatherState::SelectCity;
    selectedCityIndex = 0;
  }
  requestUpdate();
}

void WeatherActivity::onExit() {
  Activity::onExit();
  cancelFetchTask();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void WeatherActivity::cancelFetchTask() {
  if (fetchTaskHandle != nullptr) {
    TaskHandle_t tempHandle = static_cast<TaskHandle_t>(fetchTaskHandle);
    fetchTaskHandle = nullptr;
    vTaskDelete(tempHandle);
  }
  pendingUpdateWeather = false;
}

void WeatherActivity::loop() {
  if (pendingUpdateWeather) {
    pendingUpdateWeather = false;
    fetchTaskHandle = nullptr;
    if (backgroundFetchSuccess) {
      temp = fetchedTemp;
      windspeed = fetchedWindspeed;
      weatherCode = fetchedWeatherCode;
      timeStr = fetchedTimeStr;
      weatherLoaded = true;
      saveCache(cityName, temp, windspeed, weatherCode, timeStr);
      if (state == WeatherState::Loading) {
        state = WeatherState::ShowWeather;
      }
      requestUpdate();
    } else {
      if (!weatherLoaded) {
        errorMessage = "No network & no cached data.";
        if (state == WeatherState::Loading) {
          state = WeatherState::ShowWeather;
        }
      }
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (state == WeatherState::SelectCity) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectedCityIndex = (selectedCityIndex - 1 + CITY_COUNT) % CITY_COUNT;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectedCityIndex = (selectedCityIndex + 1) % CITY_COUNT;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      cityName = CITIES[selectedCityIndex].name;
      saveConfig(selectedCityIndex, cityName);
      if (loadCache(cityName, temp, windspeed, weatherCode, timeStr)) {
        weatherLoaded = true;
        state = WeatherState::ShowWeather;
        requestUpdate();
        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
          performFetch();
        }
      } else {
        weatherLoaded = false;
        errorMessage = "No cached weather data. Fetching...";
        state = WeatherState::Loading;
        requestUpdate();
        ensureWifiConnected([this]() { performFetch(); },
                            [this]() {
                              state = WeatherState::SelectCity;
                              requestUpdate();
                            });
      }
    }
  } else if (state == WeatherState::ShowWeather) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = WeatherState::Loading;
      requestUpdate();
      ensureWifiConnected([this]() { performFetch(); },
                          [this]() {
                            state = WeatherState::ShowWeather;
                            requestUpdate();
                          });
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      state = WeatherState::SelectCity;
      requestUpdate();
    }
  }
}

void WeatherActivity::performFetch() {
  cancelFetchTask();
  backgroundFetchSuccess = false;
  xTaskCreate(weatherFetchTaskFunc, "weather_fetch", 8192, this, 5, (TaskHandle_t*)&fetchTaskHandle);
}

void WeatherActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  if (state == WeatherState::SelectCity) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Select City");

    GUI.drawButtonMenu(
        renderer,
        Rect{0, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing, pageWidth,
             pageHeight -
                 (metrics.headerHeight + metrics.topPadding + metrics.verticalSpacing + metrics.buttonHintsHeight)},
        CITY_COUNT, selectedCityIndex, [](int index) { return std::string(CITIES[index].name); },
        [](int index) { return UIIcon::Library; }, 18);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == WeatherState::Loading) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Weather");

    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int contentHeight = contentBottom - contentTop;

    int textY = contentTop + contentHeight / 2 - renderer.getLineHeight(UI_12_FONT_ID) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, textY, "Loading weather...");

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == WeatherState::ShowWeather) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Weather");

    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int contentHeight = contentBottom - contentTop;

    const int cardX = metrics.contentSidePadding;
    const int cardW = pageWidth - 2 * metrics.contentSidePadding;
    const int cardH = 420;
    const int cardY = contentTop + (contentHeight - cardH) / 2;

    renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 2, 12, true);

    if (weatherLoaded) {
      // Draw weather visual icon
      drawWeatherIcon(renderer, cardX + cardW / 2, cardY + 70, weatherCode);

      // Temperature in Celsius and Fahrenheit
      char tempBuf[64];
      double tempF = temp * 9.0 / 5.0 + 32.0;
      snprintf(tempBuf, sizeof(tempBuf), "%.1f °C / %.1f °F", temp, tempF);
      renderer.drawCenteredText(NOTOSANS_18_FONT_ID, cardY + 145, tempBuf, true, EpdFontFamily::BOLD);

      // City Name
      renderer.drawCenteredText(NOTOSANS_16_FONT_ID, cardY + 195, cityName.c_str(), true, EpdFontFamily::BOLD);

      // Weather Description
      const char* desc = getWeatherDesc(weatherCode);
      renderer.drawCenteredText(NOTOSANS_14_FONT_ID, cardY + 235, desc, true, EpdFontFamily::BOLD);

      // Wind Speed
      char windBuf[64];
      double windspeedMph = windspeed * 0.621371;
      snprintf(windBuf, sizeof(windBuf), "Wind: %.1f km/h / %.1f mph", windspeed, windspeedMph);
      renderer.drawCenteredText(NOTOSANS_12_FONT_ID, cardY + 275, windBuf, true, EpdFontFamily::REGULAR);

      // Updated Time
      char timeBuf[64];
      if (fetchTaskHandle != nullptr) {
        snprintf(timeBuf, sizeof(timeBuf), "Refreshing...");
      } else {
        std::string formattedTime = timeStr;
        size_t tPos = formattedTime.find('T');
        if (tPos != std::string::npos) {
          formattedTime[tPos] = ' ';
        }
        snprintf(timeBuf, sizeof(timeBuf), "Updated: %s", formattedTime.c_str());
      }
      renderer.drawCenteredText(SMALL_FONT_ID, cardY + 315, timeBuf, true, EpdFontFamily::REGULAR);

    } else {
      int textY = cardY + cardH / 2 - renderer.getLineHeight(UI_10_FONT_ID) / 2;
      renderer.drawCenteredText(UI_10_FONT_ID, textY, errorMessage.c_str(), true, EpdFontFamily::BOLD);
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Refresh", "City", nullptr);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
