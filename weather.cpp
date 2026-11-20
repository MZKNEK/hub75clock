// Pogoda z Open-Meteo (darmowe, bez klucza API). Pobierana w osobnym zadaniu
// na rdzeniu 0, zeby pobieranie nie zatrzymywalo animacji.
// Wspolrzedne miejscowosci: kWeatherLatitude/Longitude w config.h.
#include "zegar.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static WeatherData _weather{};
static portMUX_TYPE _weatherMux = portMUX_INITIALIZER_UNLOCKED;

static constexpr uint32_t kWeatherRefreshMs{15UL * 60UL * 1000UL};
static constexpr uint32_t kWeatherRetryMs{60UL * 1000UL};
// Tyle bez udanego pobrania i pogoda jest uznawana za nieaktualna ("--*"),
// zeby np. po polnocy nie pokazywac wczorajszych max/min i wschodu slonca.
static constexpr uint32_t kWeatherStaleMs{2UL * 60UL * 60UL * 1000UL};

WeatherData getWeather() {
  portENTER_CRITICAL(&_weatherMux);
  WeatherData copy = _weather;
  portEXIT_CRITICAL(&_weatherMux);
  if (copy.valid && millis() - copy.fetchedAtMs > kWeatherStaleMs) {
    copy.valid = false;
  }
  return copy;
}

// Szuka "klucz": po pozycji from i czyta liczbe (pomija '[' i spacje).
static bool readJsonNumber(const String& body, int from, const char* key, float& value) {
  int position = body.indexOf(key, from);
  if (position < 0) {
    return false;
  }
  position += strlen(key);
  while (position < static_cast<int>(body.length())
    && (body[position] == ' ' || body[position] == '[')) {
    position++;
  }
  value = atof(body.c_str() + position);
  return true;
}

// Czyta tablice liczb "klucz":[7.9,8.1,null,...] (null -> 0), max maxCount.
static uint8_t readJsonNumberArray(const String& body, int from, const char* key,
  float* values, uint8_t maxCount) {
  int position = body.indexOf(key, from);
  if (position < 0) {
    return 0;
  }
  position = body.indexOf('[', position);
  if (position < 0) {
    return 0;
  }
  const char* text = body.c_str() + position + 1;
  uint8_t count = 0;
  while (count < maxCount && *text != '\0' && *text != ']') {
    char* end = nullptr;
    float value = strtof(text, &end);
    if (end == text) {
      value = 0.0f;
      while (*end != '\0' && *end != ',' && *end != ']') {
        end++;
      }
    }
    values[count++] = value;
    text = end;
    if (*text == ',') {
      text++;
    }
  }
  return count;
}

// Czyta godzine z napisu "2026-10-25T06:31" i zwraca minuty od polnocy.
static int16_t readJsonTimeMinutes(const String& body, int from, const char* key) {
  int position = body.indexOf(key, from);
  if (position < 0) {
    return -1;
  }
  int t = body.indexOf('T', position + strlen(key));
  if (t < 0 || t + 5 >= static_cast<int>(body.length())) {
    return -1;
  }
  int hours = atoi(body.c_str() + t + 1);
  int minutes = atoi(body.c_str() + t + 4);
  return hours * 60 + minutes;
}

static bool fetchWeather(WeatherData& data) {
  char url[400];
  snprintf(url, sizeof(url),
    "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
    "&current=temperature_2m,weather_code,is_day"
    "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset,"
    "temperature_2m_mean,precipitation_probability_max"
    "&timezone=auto&forecast_days=4",
    kWeatherLatitude, kWeatherLongitude);

  WiFiClientSecure client;
  client.setInsecure(); // publiczne dane pogodowe - bez sprawdzania certyfikatu
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) {
    return false;
  }
  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("Pogoda: blad HTTP %d\n", status);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  // "current": i "daily": (nie myli sie z "current_units" / "daily_units").
  int current = body.indexOf("\"current\":");
  int daily = body.indexOf("\"daily\":");
  if (current < 0 || daily < 0) {
    return false;
  }
  float temperature;
  float code;
  float isDay;
  float maxTemp;
  float minTemp;
  if (!readJsonNumber(body, current, "\"temperature_2m\":", temperature)
    || !readJsonNumber(body, current, "\"weather_code\":", code)
    || !readJsonNumber(body, current, "\"is_day\":", isDay)
    || !readJsonNumber(body, daily, "\"temperature_2m_max\":", maxTemp)
    || !readJsonNumber(body, daily, "\"temperature_2m_min\":", minTemp)) {
    return false;
  }
  data.valid = true;
  data.temperature = temperature;
  data.code = static_cast<int>(code);
  data.isDay = isDay > 0.5f;
  data.maxTemp = maxTemp;
  data.minTemp = minTemp;
  data.sunriseMinutes = readJsonTimeMinutes(body, daily, "\"sunrise\":");
  data.sunsetMinutes = readJsonTimeMinutes(body, daily, "\"sunset\":");
  data.fetchedAtMs = millis();

  // Tablice "daily" zaczynaja sie od dzisiaj; prognoza = kolejne 3 dni.
  // (max/min/wschod/zachod wyzej czytaja pierwszy element, czyli dzisiaj.)
  float means[WeatherData::kForecastDays + 1];
  float chances[WeatherData::kForecastDays + 1];
  uint8_t meanCount = readJsonNumberArray(body, daily, "\"temperature_2m_mean\":",
    means, WeatherData::kForecastDays + 1);
  uint8_t chanceCount = readJsonNumberArray(body, daily, "\"precipitation_probability_max\":",
    chances, WeatherData::kForecastDays + 1);
  uint8_t days = meanCount < chanceCount ? meanCount : chanceCount;
  data.forecastCount = days > 1 ? days - 1 : 0;
  for (uint8_t day = 0; day < data.forecastCount; day++) {
    data.dayMeanTemp[day] = means[day + 1];
    data.dayRainChance[day] = static_cast<uint8_t>(constrain(chances[day + 1], 0.0f, 100.0f));
  }
  Serial.printf("Pogoda: %.1f C, kod %d\n", temperature, data.code);
  return true;
}

static void weatherTask(void* parameter) {
  (void)parameter;
  uint32_t nextFetchAtMs = 0;
  for (;;) {
    if (WiFi.status() == WL_CONNECTED
      && static_cast<int32_t>(millis() - nextFetchAtMs) >= 0) {
      WeatherData data;
      if (fetchWeather(data)) {
        portENTER_CRITICAL(&_weatherMux);
        _weather = data;
        portEXIT_CRITICAL(&_weatherMux);
        nextFetchAtMs = millis() + kWeatherRefreshMs;
      } else {
        nextFetchAtMs = millis() + kWeatherRetryMs;
      }
    }
    updateOnlineCounters(); // Gmail + MangaDex z Apps Script (web_counters.cpp)
    updateRates();          // kursy EUR/USD/JPY z NBP (mode_rates.cpp)
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setupWeather() {
  xTaskCreatePinnedToCore(weatherTask, "weather", 12288, nullptr, 1, nullptr, 0);
}
