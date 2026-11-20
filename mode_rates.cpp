// Tryb "Kursy": sredni kurs NBP w zlotych dla EUR, USD i JPY (jen za 100,
// jak w kantorach - 1 JPY to ok. 0,025 zl), wykres 30 ostatnich notowan, zmiana
// do poprzedniego notowania. Darmowe API bez klucza.
// Pobieranie w zadaniu pogody (weather.cpp -> updateRates), po kolei z pogoda
// i licznikami - jedno polaczenie HTTPS naraz. Stan: GET /rates.
//
//   3 bloki po 21 px: "EUR 4.406 +0.07%", pod spodem wykres (11 px).
#include "zegar.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Kurs z historia do wykresu.
static constexpr uint8_t kRateHistory{30};
struct RateSeries {
  float values[kRateHistory]{};
  uint8_t count{0};
  float current{0.0f};
  float changePercent{0.0f};
  uint32_t fetchedAtMs{0}; // 0 = jeszcze nie pobrane
};

static constexpr uint32_t kNbpRefreshMs{60UL * 60UL * 1000UL};   // NBP: raz dziennie ok. 12:00
static constexpr uint32_t kRatesRetryMs{5UL * 60UL * 1000UL};
static constexpr uint32_t kNbpStaleMs{26UL * 60UL * 60UL * 1000UL};
static constexpr int16_t kRateBlockHeight{21};
static constexpr int16_t kRateGraphX{2};
static constexpr int16_t kRateGraphWidth{60};
static constexpr int16_t kRateGraphHeight{11};

struct RateInfo {
  const char* label;
  const char* code;  // kod waluty w API NBP
  float hue;
  float scale;       // ile jednostek waluty (JPY: 100)
};
static constexpr uint8_t kRateCount{3};
static const RateInfo kRateInfo[kRateCount] {
  {"EUR", "eur", 0.6f, 1.0f},
  {"USD", "usd", 0.33f, 1.0f},
  {"JPY", "jpy", 0.95f, 100.0f},
};

static RateSeries _rates[kRateCount];
static portMUX_TYPE _ratesMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t _nbpNextFetchAtMs{0};
static int _ratesLastStatus{0};

// NBP: {"rates":[{"no":..,"effectiveDate":..,"mid":4.3917},...]} od najstarszego.
static bool fetchNbpRate(uint8_t index) {
  const RateInfo& info = kRateInfo[index];
  char url[96];
  snprintf(url, sizeof(url),
    "https://api.nbp.pl/api/exchangerates/rates/a/%s/last/%u/?format=json", info.code, kRateHistory);
  WiFiClientSecure client;
  client.setInsecure(); // publiczne kursy - bez sprawdzania certyfikatu
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    _ratesLastStatus = -100;
    return false;
  }
  int status = http.GET();
  _ratesLastStatus = status;
  if (status != HTTP_CODE_OK) {
    Serial.printf("Kursy: blad HTTP %d (%s)\n", status, info.code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  RateSeries series;
  int position = 0;
  while (series.count < kRateHistory && (position = body.indexOf("\"mid\":", position)) >= 0) {
    position += 6;
    series.values[series.count++] = atof(body.c_str() + position) * info.scale;
  }
  if (series.count == 0) {
    return false;
  }
  series.current = series.values[series.count - 1];
  if (series.count >= 2 && series.values[series.count - 2] > 0.0f) {
    series.changePercent = (series.current / series.values[series.count - 2] - 1.0f) * 100.0f;
  }
  series.fetchedAtMs = millis() | 1;
  portENTER_CRITICAL(&_ratesMux);
  _rates[index] = series;
  portEXIT_CRITICAL(&_ratesMux);
  return true;
}

// Wolane co sekunde z zadania pogody (rdzen 0).
void updateRates() {
  if (WiFi.status() != WL_CONNECTED
    || static_cast<int32_t>(millis() - _nbpNextFetchAtMs) < 0) {
    return;
  }
  bool ok = true;
  for (uint8_t index = 0; index < kRateCount; index++) {
    ok = fetchNbpRate(index) && ok;
  }
  _nbpNextFetchAtMs = millis() + (ok ? kNbpRefreshMs : kRatesRetryMs);
}

static RateSeries getRateSeries(uint8_t index) {
  portENTER_CRITICAL(&_ratesMux);
  RateSeries copy = _rates[index];
  portEXIT_CRITICAL(&_ratesMux);
  return copy;
}

static bool isRateFresh(const RateSeries& series) {
  return series.fetchedAtMs != 0 && millis() - series.fetchedAtMs <= kNbpStaleMs;
}

String getRatesJson() {
  String body = String("{\"last_status\":") + _ratesLastStatus;
  for (uint8_t index = 0; index < kRateCount; index++) {
    RateSeries series = getRateSeries(index);
    body += String(",\"") + kRateInfo[index].label + "\":";
    if (series.fetchedAtMs == 0) {
      body += "null";
      continue;
    }
    body += String("{\"value\":") + String(series.current, 4)
      + ",\"change\":" + String(series.changePercent, 2)
      + ",\"points\":" + series.count
      + ",\"fetched_s_ago\":" + ((millis() - series.fetchedAtMs) / 1000UL) + "}";
  }
  return body + "}";
}

static int16_t getRateGraphY(float value, float low, float high, int16_t top) {
  float level = high > low ? (value - low) / (high - low) : 0.5f;
  return top + kRateGraphHeight - 1 - static_cast<int16_t>(lroundf(level * (kRateGraphHeight - 1)));
}

// Wykres: linia + przygaszone pole pod nia, punkty rozciagniete na cala szerokosc.
static void drawRateGraph(const RateSeries& series, float hue, int16_t top) {
  if (series.count < 2) {
    return;
  }
  float low = series.values[0];
  float high = series.values[0];
  for (uint8_t i = 1; i < series.count; i++) {
    low = series.values[i] < low ? series.values[i] : low;
    high = series.values[i] > high ? series.values[i] : high;
  }
  RgbColor line = HslColor(hue, 1.0f, kLightness * 0.9f);
  RgbColor area = HslColor(hue, 1.0f, kLightness * 0.2f);
  int16_t previousY = -1;
  for (int16_t column = 0; column < kRateGraphWidth; column++) {
    float position = static_cast<float>(column) * (series.count - 1) / (kRateGraphWidth - 1);
    uint8_t left = static_cast<uint8_t>(position);
    uint8_t right = left + 1 < series.count ? left + 1 : left;
    float value = series.values[left]
      + (series.values[right] - series.values[left]) * (position - left);
    int16_t y = getRateGraphY(value, low, high, top);
    int16_t x = kRateGraphX + column;
    fillClockRect(x, y + 1, 1, top + kRateGraphHeight - 1 - y, area);
    int16_t from = previousY < 0 ? y : (previousY < y ? previousY + 1 : y);
    int16_t to = previousY < 0 ? y : (previousY > y ? previousY - 1 : y);
    fillClockRect(x, from, 1, to - from + 1, line);
    setDashPixel(x, y, line);
    previousY = y;
  }
}

static void drawRateBlock(uint8_t index, int16_t top) {
  const RateInfo& info = kRateInfo[index];
  RateSeries series = getRateSeries(index);
  drawSmallText(info.label, 2, top + 1, 1, HslColor(info.hue, 1.0f, kLightness * 0.8f));
  if (!isRateFresh(series)) {
    drawSmallText("--", 16, top + 1, 1, HslColor(0.58f, 0.5f, kLightness * 0.55f));
    return;
  }
  char text[16];
  snprintf(text, sizeof(text), "%.3f", series.current);
  drawSmallText(text, 16, top + 1, 1, HslColor(0.58f, 0.3f, kLightness));
  bool up = series.changePercent >= 0.0f;
  snprintf(text, sizeof(text), "%+.2f%%", series.changePercent);
  drawSmallText(text, 62 - getDashTextWidth(text), top + 1, 1,
    HslColor(up ? 0.33f : 0.0f, 1.0f, kLightness));
  drawRateGraph(series, info.hue, top + 8);
}

void ratesAnimation(const AnimationParam& param) {
  leds.ClearTo(kBlack);
  for (uint8_t index = 0; index < kRateCount; index++) {
    drawRateBlock(index, index * kRateBlockHeight);
  }
  drawNetworkStatus();
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(0);
  }
}
