#include "zegar.h"
#include <WiFi.h>

// Ustawienia i stan zegara (clockAnimation, initClock).
static constexpr uint8_t kStartHour{12};
static constexpr uint8_t kStartMinute{0};
static constexpr float kDefaultMinuteHue{0.0f}; // Minuty: czerwony.
static constexpr float kDefaultSeparatorHue{0.16f}; // Kropki i pasek sekund: zolty.
uint32_t _elapsedSeconds;
static uint8_t _previousValues[4] {};
static bool _digitTransitionActive{false};
static bool _clockStarted{false};
static bool _clockFirstAnimation{true};
static float _clockInitialProgress{0.0f};
bool _clockEnabled{false};

// Kropka stanu sieci i odstepy synchronizacji NTP.
static constexpr float kErrorHue{0.0f}; // Blad sieci: czerwony.
static constexpr float kConnectingHue{0.58f}; // Laczenie z Wi-Fi: niebieski.
static constexpr float kNtpHue{0.16f}; // Pobieranie czasu NTP: zolty.
static constexpr uint32_t kNtpSyncIntervalMs{2UL * 60UL * 60UL * 1000UL};
static constexpr uint32_t kNtpRetryIntervalMs{30UL * 1000UL};
static uint32_t _lastNtpSync;
static constexpr uint32_t kShowIpForMs{30000UL};

// Wzory cyfr uzywane przez clockAnimation.
static const uint8_t kDigits[10][7] {
  {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},
  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
  {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
  {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110},
  {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
  {0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110},
  {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},
  {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
  {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
  {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b11100}
};

void updateClock() {
  if (_clockStarted) {
    uint32_t syncInterval = (_networkErrorCode == 1 || _networkErrorCode == 2)
      ? kNtpRetryIntervalMs
      : kNtpSyncIntervalMs;
    if (_syncState == SyncState::Idle
      && millis() - _lastNtpSync >= syncInterval) {
      syncClockFromNtp();
      _lastNtpSync = millis();
    }

    updateNtpSync();
  }

  if (!_clockEnabled) {
    return;
  }
}

struct DateFontGlyph {
  char character;
  uint8_t rows[5];
};

static const DateFontGlyph kDateFont[] {
  {'0', {0b111, 0b101, 0b101, 0b101, 0b111}},
  {'1', {0b010, 0b110, 0b010, 0b010, 0b111}},
  {'2', {0b111, 0b001, 0b111, 0b100, 0b111}},
  {'3', {0b111, 0b001, 0b111, 0b001, 0b111}},
  {'4', {0b101, 0b101, 0b111, 0b001, 0b001}},
  {'5', {0b111, 0b100, 0b111, 0b001, 0b111}},
  {'6', {0b111, 0b100, 0b111, 0b101, 0b111}},
  {'7', {0b111, 0b001, 0b010, 0b010, 0b010}},
  {'8', {0b111, 0b101, 0b111, 0b101, 0b111}},
  {'9', {0b111, 0b101, 0b111, 0b001, 0b111}},
  {'.', {0b000, 0b000, 0b000, 0b000, 0b010}},
  {'-', {0b000, 0b000, 0b111, 0b000, 0b000}},
  {'/', {0b001, 0b001, 0b010, 0b100, 0b100}},
  {'*', {0b010, 0b101, 0b010, 0b000, 0b000}}, // znak stopnia
  {'%', {0b100, 0b001, 0b010, 0b100, 0b001}},
  {'+', {0b000, 0b010, 0b111, 0b010, 0b000}},
  {'!', {0b010, 0b010, 0b010, 0b000, 0b010}},
  {'?', {0b111, 0b001, 0b011, 0b000, 0b010}},
  {'A', {0b010, 0b101, 0b111, 0b101, 0b101}},
  {'B', {0b110, 0b101, 0b110, 0b101, 0b110}},
  {'C', {0b111, 0b100, 0b100, 0b100, 0b111}},
  {'D', {0b110, 0b101, 0b101, 0b101, 0b110}},
  {'E', {0b111, 0b100, 0b110, 0b100, 0b111}},
  {'F', {0b111, 0b100, 0b110, 0b100, 0b100}},
  {'G', {0b111, 0b100, 0b101, 0b101, 0b111}},
  {'H', {0b101, 0b101, 0b111, 0b101, 0b101}},
  {'I', {0b111, 0b010, 0b010, 0b010, 0b111}},
  {'J', {0b001, 0b001, 0b001, 0b101, 0b111}},
  {'K', {0b101, 0b101, 0b110, 0b101, 0b101}},
  {'L', {0b100, 0b100, 0b100, 0b100, 0b111}},
  {'M', {0b101, 0b111, 0b111, 0b101, 0b101}},
  {'N', {0b101, 0b111, 0b111, 0b111, 0b101}},
  {'O', {0b111, 0b101, 0b101, 0b101, 0b111}},
  {'P', {0b110, 0b101, 0b110, 0b100, 0b100}},
  {'Q', {0b111, 0b101, 0b101, 0b111, 0b001}},
  {'R', {0b110, 0b101, 0b110, 0b101, 0b101}},
  {'S', {0b111, 0b100, 0b111, 0b001, 0b111}},
  {'T', {0b111, 0b010, 0b010, 0b010, 0b010}},
  {'U', {0b101, 0b101, 0b101, 0b101, 0b111}},
  {'V', {0b101, 0b101, 0b101, 0b101, 0b010}},
  {'W', {0b101, 0b101, 0b101, 0b111, 0b101}},
  {'X', {0b101, 0b101, 0b010, 0b101, 0b101}},
  {'Y', {0b101, 0b101, 0b010, 0b010, 0b010}},
  {'Z', {0b111, 0b001, 0b010, 0b100, 0b111}},
  {'\x83', {0b010, 0b011, 0b110, 0b010, 0b011}} // L przekreslone (kPolishFont)
};

// ---------------------------------------------------------------------------
// Pulpit 64x64:
//   y  1-14   HH:MM - cyfry 5x7 powiekszone 2x (10x14 px)
//   y 17      pasek sekund (1 px) - zarazem linia oddzielajaca pogode
//   y 20-38   pogoda: ikona 16x16 + temperatura + max/min dzisiaj,
//             zaraz za tekstem liczniki: MangaDex (pomaranczowy) i Gmail (czerwony) pod nim
//   y 42-52   prognoza na 3 kolejne dni: skrot dnia + srednia temperatura,
//             pod spodem % szansy opadow (przez 30 s po Wi-Fi zamiast tego IP)
//   y 55-58   pasek doby: dzien zolty / noc granatowa, minione jasne, teraz = biala kreska
//   prawy dolny rog: kropka stanu sieci (gdy cos jest nie tak)
// ---------------------------------------------------------------------------
static constexpr uint8_t kClockDigitScale{2};
static constexpr int16_t kClockDigitTop{1};
static constexpr int16_t kClockDigitHeight{7 * kClockDigitScale};
static constexpr int16_t kClockDigitX[4] {7, 19, 35, 47};
static constexpr int16_t kClockColonX{31};
static constexpr int16_t kSecondBarY{17};
static constexpr int16_t kWeatherIconX{4};
static constexpr int16_t kWeatherIconY{20};
static constexpr int16_t kWeatherTextX{24};
static constexpr int16_t kWeatherTempY{21};
static constexpr int16_t kWeatherRangeY{34};
static constexpr int16_t kForecastY{42};
static constexpr int16_t kForecastRainY{48};
static constexpr int16_t kForecastColumnWidth{21};
static constexpr int16_t kDayBarY{56};
static constexpr int16_t kDayBarX{4};
static constexpr uint8_t kDayBarWidth{56};

static void fillClockRectClipped(int16_t x, int16_t y, int16_t width,
  int16_t height, const RgbColor& color, int16_t clipTop, int16_t clipBottom) {
  for (int16_t row = y; row < y + height; row++) {
    if (row < clipTop || row >= clipBottom || row < 0 || row >= kRows) {
      continue;
    }
    for (int16_t column = x; column < x + width; column++) {
      if (column >= 0 && column < kColumns) {
        leds.SetPixelColor(pixelIndex(row, column), color);
      }
    }
  }
}

void fillClockRect(int16_t x, int16_t y, int16_t width, int16_t height,
  const RgbColor& color) {
  fillClockRectClipped(x, y, width, height, color, 0, kRows);
}

void setDashPixel(int16_t x, int16_t y, const RgbColor& color) {
  if (x >= 0 && x < kColumns && y >= 0 && y < kRows) {
    leds.SetPixelColor(pixelIndex(y, x), color);
  }
}

// Cyfra zegara; yOffset przesuwa ja w pionie (animacja zmiany),
// wszystko poza polem cyfry jest obcinane.
static void drawClockDigit(uint8_t position, uint8_t value, int16_t yOffset,
  const RgbColor& color) {
  for (uint8_t row = 0; row < 7; row++) {
    for (uint8_t column = 0; column < 5; column++) {
      if (kDigits[value][row] & (1 << (4 - column))) {
        fillClockRectClipped(
          kClockDigitX[position] + column * kClockDigitScale,
          kClockDigitTop + yOffset + row * kClockDigitScale,
          kClockDigitScale, kClockDigitScale, color,
          kClockDigitTop, kClockDigitTop + kClockDigitHeight);
      }
    }
  }
}

// Polskie wielkie litery: w tekstach jako kody 0x80-0x88 (z UTF-8 zamienia je
// message_overlay.cpp). Rysowane jako litera bazowa 3x5 + znak diakrytyczny:
// kreska/kropka nad litera (wiersze -2, -1), ogonek pod nia (wiersz 5).
struct PolishGlyph {
  char code;
  char base;
  int8_t marks[2][2]; // {kolumna, wiersz}; wiersz kNoMark = brak
};
static constexpr int8_t kNoMark{99};
static const PolishGlyph kPolishFont[] {
  {'\x80', 'A', {{2, 5}, {0, kNoMark}}},   // A z ogonkiem
  {'\x81', 'C', {{1, -1}, {2, -2}}},       // C z kreska
  {'\x82', 'E', {{2, 5}, {0, kNoMark}}},   // E z ogonkiem
  {'\x83', '\x83', {{0, kNoMark}, {0, kNoMark}}},  // L przekreslone: wlasny glif
  {'\x84', 'N', {{1, -1}, {2, -2}}},       // N z kreska
  {'\x85', 'O', {{1, -1}, {2, -2}}},       // O z kreska
  {'\x86', 'S', {{1, -1}, {2, -2}}},       // S z kreska
  {'\x87', 'Z', {{1, -1}, {2, -2}}},       // Z z kreska
  {'\x88', 'Z', {{1, -1}, {0, kNoMark}}},  // Z z kropka
};

static uint8_t getDateGlyphRow(char character, uint8_t row) {
  if (character == ' ') {
    return 0;
  }
  for (const DateFontGlyph& glyph : kDateFont) {
    if (glyph.character == character) {
      return glyph.rows[row];
    }
  }
  return 0;
}

uint8_t getSmallCharAdvance(char character, uint8_t scale) {
  return (character == '.' || character == ':' || character == ' ' ? 2 : 4) * scale;
}

// Tekst czcionka 3x5 (cyfry, wielkie litery z polskimi, - / . : % + ! ? i '*'
// jako stopien).
void drawSmallText(const char* text, int16_t x, int16_t y, uint8_t scale,
  const RgbColor& color) {
  for (const char* character = text; *character != '\0'; character++) {
    if (*character == '.') {
      fillClockRect(x, y + 4 * scale, scale, scale, color);
    } else if (*character == ':') {
      fillClockRect(x, y + 1 * scale, scale, scale, color);
      fillClockRect(x, y + 3 * scale, scale, scale, color);
    } else {
      char base = *character;
      for (const PolishGlyph& polish : kPolishFont) {
        if (polish.code != *character) {
          continue;
        }
        base = polish.base;
        for (const auto& mark : polish.marks) {
          if (mark[1] != kNoMark) {
            fillClockRect(x + mark[0] * scale, y + mark[1] * scale, scale, scale, color);
          }
        }
      }
      for (uint8_t row = 0; row < 5; row++) {
        uint8_t glyphRow = getDateGlyphRow(base, row);
        for (uint8_t column = 0; column < 3; column++) {
          if (glyphRow & (1 << (2 - column))) {
            fillClockRect(x + column * scale, y + row * scale, scale, scale, color);
          }
        }
      }
    }
    x += getSmallCharAdvance(*character, scale);
  }
}

int16_t getDashTextWidth(const char* text) {
  int16_t width = 0;
  for (const char* c = text; *c; c++) {
    width += getSmallCharAdvance(*c, 1);
  }
  return width > 0 ? width - 1 : 0;
}

static void drawClockColon(const RgbColor& color) {
  fillClockRect(kClockColonX, kClockDigitTop + 2 * kClockDigitScale, 2, 2, color);
  fillClockRect(kClockColonX, kClockDigitTop + 4 * kClockDigitScale, 2, 2, color);
}

// Nocny zegar (brightness.cpp, "W nocy: ciemny zegar"): sama godzina na czerwono
// na srodku ekranu, bez animacji. Przyciemnia go sprzetowa jasnosc panelu,
// wiec kolor jest pelny (przy malym OE slabsze kolory moglyby zniknac).
static constexpr int16_t kNightClockTop{25};

void drawNightClock() {
  struct tm timeInfo;
  if (!readLocalTime(&timeInfo)) {
    return;
  }
  uint8_t values[4] {
    static_cast<uint8_t>(timeInfo.tm_hour / 10), static_cast<uint8_t>(timeInfo.tm_hour % 10),
    static_cast<uint8_t>(timeInfo.tm_min / 10), static_cast<uint8_t>(timeInfo.tm_min % 10)
  };
  RgbColor color(255, 0, 0);
  for (uint8_t digit = 0; digit < 4; digit++) {
    for (uint8_t row = 0; row < 7; row++) {
      for (uint8_t column = 0; column < 5; column++) {
        if (kDigits[values[digit]][row] & (1 << (4 - column))) {
          fillClockRect(kClockDigitX[digit] + column * kClockDigitScale,
            kNightClockTop + row * kClockDigitScale, kClockDigitScale, kClockDigitScale, color);
        }
      }
    }
  }
  fillClockRect(kClockColonX, kNightClockTop + 2 * kClockDigitScale, 2, 2, color);
  fillClockRect(kClockColonX, kNightClockTop + 4 * kClockDigitScale, 2, 2, color);
}

void drawNetworkStatus() {
  float hue;
  if (_networkErrorCode == kNetworkConnecting) {
    hue = kConnectingHue;
  } else if (_networkErrorCode == kNetworkWaitingForNtp) {
    hue = kNtpHue;
  } else if (_networkErrorCode == 1 || WiFi.status() != WL_CONNECTED) {
    // Stan Wi-Fi na biezaco, nie tylko z ostatniej synchronizacji (co 2 h).
    hue = kErrorHue;
  } else if (_networkErrorCode == 2) {
    hue = 0.06f;
  } else {
    return;
  }
  fillClockRect(kColumns - 2, kRows - 2, 2, 2, HslColor(hue, 1.0f, kLightness));
}

// Pasek sekund na samej gorze: 64 px = 60 s.
static void drawSecondProgressBar(float transitionProgress) {
  float seconds = (_elapsedSeconds % 60) + transitionProgress;
  uint8_t barLength = seconds * kColumns / 60.0f;
  float barHue = 0.66f * (1.0f - seconds / 60.0f);

  // Na poczatku minuty pelny pasek z poprzedniej minuty gasnie od prawej.
  float trailProgress = seconds / 4.0f;
  if (trailProgress < 1.0f) {
    uint8_t trailLength = (1.0f - trailProgress) * kColumns;
    RgbColor trailColor = HslColor(barHue, 1.0f, kLightness * (1.0f - trailProgress));
    for (uint8_t column = barLength; column < trailLength; column++) {
      setDashPixel(column, kSecondBarY, trailColor);
    }
  }
  for (uint8_t column = 0; column < barLength && column < kColumns; column++) {
    float columnProgress = static_cast<float>(column) / (kColumns - 1);
    setDashPixel(column, kSecondBarY,
      HslColor(0.66f * (1.0f - columnProgress), 1.0f, kLightness));
  }
}

// ---------------------------------------------------------------- pogoda
static float temperatureHue(float temperature) {
  float t = constrain((temperature + 10.0f) / 40.0f, 0.0f, 1.0f);
  return 0.66f * (1.0f - t); // -10 = niebieski, 30 = czerwony
}

static void drawCloud(int16_t x, int16_t y, float lightness, bool dark) {
  // Chmurka z trzech kol, plaski spod.
  static const int8_t circles[3][3] {{5, 9, 4}, {9, 7, 5}, {12, 10, 3}};
  for (int16_t py = 0; py < 16; py++) {
    for (int16_t px = 0; px < 16; px++) {
      if (py > 13) {
        continue;
      }
      bool inside = false;
      for (const auto& circle : circles) {
        int16_t dx = px - circle[0];
        int16_t dy = py - circle[1];
        if (dx * dx + dy * dy <= circle[2] * circle[2]) {
          inside = true;
        }
      }
      if (inside) {
        // Jasna: blekitnobiala z delikatnym cieniowaniem, ciemna: granatowa.
        if (dark) {
          setDashPixel(x + px, y + py, HslColor(0.64f, 0.7f, lightness * 0.55f));
        } else {
          float shade = 0.8f + 0.2f * (1.0f - py / 13.0f);
          setDashPixel(x + px, y + py, HslColor(0.58f, 0.35f, lightness * shade));
        }
      }
    }
  }
}

static void drawSun(int16_t x, int16_t y, int16_t cx, int16_t cy, float radius,
  float lightness) {
  float pulse = 0.75f + 0.25f * sinf((millis() % 2000UL) * 2.0f * PI / 2000.0f);
  for (int16_t py = 0; py < 16; py++) {
    for (int16_t px = 0; px < 16; px++) {
      float dx = px - cx;
      float dy = py - cy;
      float distance = sqrtf(dx * dx + dy * dy);
      if (distance <= radius) {
        setDashPixel(x + px, y + py, HslColor(0.13f, 1.0f, lightness));
      } else if (distance <= radius + 3.0f && distance > radius + 1.0f) {
        // Promienie: 8 kierunkow.
        float angle = atan2f(dy, dx) * 4.0f / PI;
        if (fabsf(angle - roundf(angle)) < 0.18f) {
          setDashPixel(x + px, y + py, HslColor(0.11f, 1.0f, lightness * pulse));
        }
      }
    }
  }
}

static void drawMoon(int16_t x, int16_t y, float lightness) {
  for (int16_t py = 0; py < 16; py++) {
    for (int16_t px = 0; px < 16; px++) {
      float d1 = hypotf(px - 8.0f, py - 8.0f);
      float d2 = hypotf(px - 11.0f, py - 6.0f);
      if (d1 <= 6.0f && d2 > 5.0f) {
        setDashPixel(x + px, y + py, HslColor(0.14f, 0.5f, lightness));
      }
    }
  }
  bool twinkle = (millis() / 700UL) % 2 == 0;
  setDashPixel(x + 14, y + 2, HslColor(0.6f, 0.2f, lightness * (twinkle ? 1.0f : 0.4f)));
  setDashPixel(x + 1, y + 13, HslColor(0.6f, 0.2f, lightness * (twinkle ? 0.4f : 1.0f)));
}

static void drawPrecipitation(int16_t x, int16_t y, bool snow, float lightness) {
  uint32_t step = millis() / (snow ? 220UL : 110UL);
  for (uint8_t drop = 0; drop < 4; drop++) {
    int16_t px = 3 + drop * 3;
    int16_t py = 12 + static_cast<int16_t>((step + drop * 2) % 5);
    if (snow) {
      setDashPixel(x + px, y + py, HslColor(0.58f, 0.35f, lightness));
    } else {
      RgbColor rain = HslColor(0.58f, 1.0f, lightness);
      setDashPixel(x + px, y + py, rain);
      setDashPixel(x + px, y + py - 1, HslColor(0.58f, 1.0f, lightness * 0.5f));
    }
  }
}

static void drawWeatherIcon(const WeatherData& weather) {
  float lightness = kLightness * 1.2f;
  int16_t x = kWeatherIconX;
  int16_t y = kWeatherIconY;
  if (!weather.valid) {
    // Czekamy na dane: trzy przesuwajace sie kropki.
    uint8_t active = (millis() / 400UL) % 3;
    for (uint8_t dot = 0; dot < 3; dot++) {
      fillClockRect(x + 2 + dot * 5, y + 7, 2, 2,
        HslColor(0.58f, 0.5f, kLightness * (dot == active ? 1.0f : 0.4f)));
    }
    return;
  }

  int code = weather.code;
  if (code == 0) {
    if (weather.isDay) {
      drawSun(x, y, 8, 8, 3.5f, lightness);
    } else {
      drawMoon(x, y, lightness);
    }
  } else if (code <= 2) {
    if (weather.isDay) {
      drawSun(x, y, 5, 5, 2.5f, lightness);
    } else {
      drawMoon(x - 3, y - 3, lightness * 0.8f);
    }
    drawCloud(x + 1, y + 3, lightness, false);
  } else if (code == 3) {
    drawCloud(x - 2, y - 2, lightness * 0.7f, true);
    drawCloud(x + 1, y + 2, lightness, false);
  } else if (code == 45 || code == 48) {
    uint32_t drift = (millis() / 300UL) % 4;
    for (uint8_t line = 0; line < 4; line++) {
      for (int16_t px = 1 + (line % 2) * 2; px < 15; px++) {
        if ((px + drift + line) % 7 != 0) {
          setDashPixel(x + px, y + 3 + line * 3, HslColor(0.55f, 0.4f, lightness * 0.8f));
        }
      }
    }
  } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    drawCloud(x, y - 2, lightness, code >= 61);
    drawPrecipitation(x, y, false, lightness);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    drawCloud(x, y - 2, lightness, false);
    drawPrecipitation(x, y, true, lightness);
  } else if (code >= 95) {
    drawCloud(x, y - 2, lightness, true);
    if ((millis() / 150UL) % 10 < 3) {
      static const int8_t bolt[][2] {{9, 10}, {8, 11}, {7, 12}, {8, 12}, {9, 12},
        {8, 13}, {7, 14}};
      for (const auto& pixel : bolt) {
        setDashPixel(x + pixel[0], y + pixel[1], HslColor(0.15f, 1.0f, lightness * 1.5f));
      }
    }
  } else {
    drawCloud(x, y, lightness, false);
  }
}

static void formatTemperature(char* buffer, size_t size, float temperature, bool degree) {
  int value = static_cast<int>(lroundf(temperature));
  snprintf(buffer, size, degree ? "%d*" : "%d", value);
}

// Prawa krawedz tekstu pogody (logo MangaDex stoi zaraz za nia).
static int16_t _weatherTextRight{kWeatherTextX};

static void drawWeather() {
  WeatherData weather = getWeather();
  drawWeatherIcon(weather);

  if (!weather.valid) {
    drawSmallText("--*", kWeatherTextX, kWeatherTempY, 2,
      HslColor(0.58f, 0.5f, kLightness * 0.55f));
    _weatherTextRight = kWeatherTextX + 2 * getDashTextWidth("--*");
    return;
  }

  char text[12];
  formatTemperature(text, sizeof(text), weather.temperature, true);
  drawSmallText(text, kWeatherTextX, kWeatherTempY, 2,
    HslColor(temperatureHue(weather.temperature), 1.0f, kLightness));

  // Dzisiaj: max / min.
  char maxText[8];
  char minText[8];
  formatTemperature(maxText, sizeof(maxText), weather.maxTemp, false);
  formatTemperature(minText, sizeof(minText), weather.minTemp, false);
  int16_t x = kWeatherTextX;
  drawSmallText(maxText, x, kWeatherRangeY, 1,
    HslColor(temperatureHue(weather.maxTemp), 1.0f, kLightness * 0.8f));
  for (const char* c = maxText; *c; c++) {
    x += getSmallCharAdvance(*c, 1);
  }
  drawSmallText("/", x, kWeatherRangeY, 1, HslColor(0.6f, 0.6f, kLightness * 0.55f));
  x += getSmallCharAdvance('/', 1);
  drawSmallText(minText, x, kWeatherRangeY, 1,
    HslColor(temperatureHue(weather.minTemp), 1.0f, kLightness * 0.8f));

  int16_t temperatureRight = kWeatherTextX + 2 * getDashTextWidth(text);
  int16_t rangeRight = x + getDashTextWidth(minText);
  _weatherTextRight = temperatureRight > rangeRight ? temperatureRight : rangeRight;
}

// ---------------------------------------------------------------- prognoza i dzien
// Kolejne 3 dni w kolumnach po 21 px:
//   "SR  6*"  - skrot dnia (weekend inny kolor) + srednia temperatura
//    "20%"    - najwieksza szansa opadow w ciagu dnia
static void drawForecast() {
  // Przez chwile po polaczeniu z Wi-Fi zamiast prognozy pokazujemy IP
  // (adres strony do zmiany trybu).
  if (_ipText[0] != '\0' && millis() - _wifiConnectedAtMs < kShowIpForMs) {
    int16_t width = 0;
    for (const char* c = _ipText; *c; c++) {
      width += getSmallCharAdvance(*c, 1);
    }
    drawSmallText(_ipText, (kColumns - width + 1) / 2, kForecastY + 2, 1,
      HslColor(0.55f, 0.6f, kLightness));
    return;
  }
  WeatherData weather = getWeather();
  struct tm timeInfo;
  if (!weather.valid || weather.forecastCount == 0 || !_hasValidTime
    || !readLocalTime(&timeInfo)) {
    return;
  }
  static const char* const kDayNames[7] {"ND", "PN", "WT", "\x86" "R", "CZ", "PT", "SB"};
  constexpr int16_t kNameWidth{7};
  constexpr int16_t kNameGap{3};
  constexpr int16_t kInner{kForecastColumnWidth - 1}; // bez kreski oddzielajacej
  RgbColor separator = HslColor(0.6f, 0.5f, kLightness * 0.35f);
  for (uint8_t day = 0; day < weather.forecastCount; day++) {
    int16_t column = day * kForecastColumnWidth;
    if (day > 0) {
      fillClockRect(column, kForecastY, 1, kForecastRainY + 5 - kForecastY, separator);
    }
    int16_t x = column + 1;
    uint8_t weekday = (timeInfo.tm_wday + day + 1) % 7;
    bool weekend = weekday == 0 || weekday == 6;

    // "SR 6*" wysrodkowane w kolumnie; przy mrozie ponizej -9 bez '*'.
    char temperature[8];
    float mean = weather.dayMeanTemp[day];
    formatTemperature(temperature, sizeof(temperature), mean, true);
    int16_t width = getDashTextWidth(temperature);
    if (kNameWidth + kNameGap + width > kInner) {
      formatTemperature(temperature, sizeof(temperature), mean, false);
      width = getDashTextWidth(temperature);
    }
    int16_t start = x + (kInner - (kNameWidth + kNameGap + width)) / 2;
    drawSmallText(kDayNames[weekday], start, kForecastY, 1,
      HslColor(weekend ? 0.9f : 0.55f, 0.7f, kLightness * 0.7f));
    drawSmallText(temperature, start + kNameWidth + kNameGap, kForecastY, 1,
      HslColor(temperatureHue(mean), 1.0f, kLightness));

    char rain[6];
    uint8_t chance = weather.dayRainChance[day];
    snprintf(rain, sizeof(rain), "%u%%", static_cast<unsigned>(chance));
    width = getDashTextWidth(rain);
    // Im wieksza szansa deszczu, tym jasniejszy niebieski.
    drawSmallText(rain, x + (kInner - width) / 2, kForecastRainY, 1,
      HslColor(0.58f, 1.0f, kLightness * (chance >= 50 ? 1.0f : (chance >= 20 ? 0.7f : 0.45f))));
  }
}

// Liczniki z komputera - same liczby (do 99), bez opisow:
//   pomaranczowa = nieprzeczytane rozdzialy MangaDex (/manga),
//   fioletowa    = nieprzeczytane powiadomienia comix.to (/comix),
//   czerwona     = nieprzeczytane maile Gmail (/mail),
//   zielona      = kanaly z subskrypcji YouTube nadajace teraz na zywo.
// Na pulpicie zaraz za tekstem pogody (_weatherTextRight), jedna pod druga;
// na panelu Claude obok siebie w dolnym wierszu. Licznik znika przy 0 i gdy
// skrypt na komputerze dawno sie nie odezwal (MangaDex co 30 min, Gmail co 5).
static constexpr int16_t kCounterTopY{22};
static constexpr int16_t kCounterLineHeight{7};

bool isCounterVisible(int16_t count, uint32_t updatedAtMs, uint32_t staleMs) {
  return count > 0 && millis() - updatedAtMs <= staleMs;
}

void formatCounter(char* buffer, size_t size, int16_t count) {
  snprintf(buffer, size, "%d", count > 99 ? 99 : count);
}

static void drawCounterAt(int16_t count, int16_t x, int16_t y, float hue) {
  char text[4];
  formatCounter(text, sizeof(text), count);
  int16_t width = getDashTextWidth(text);
  if (x > kColumns - width) {
    x = kColumns - width;
  }
  drawSmallText(text, x, y, 1, HslColor(hue, 1.0f, kLightness));
}

static void drawCounters() {
  bool manga = isCounterVisible(_mangaUnread, _mangaUpdatedAtMs, kMangaStaleMs);
  bool comix = isCounterVisible(_comixUnread, _comixUpdatedAtMs, kComixStaleMs);
  bool mail = isCounterVisible(_mailUnread, _mailUpdatedAtMs, kMailStaleMs);
  bool live = isCounterVisible(_liveCount, _liveUpdatedAtMs, kLiveStaleMs);
  int16_t x = _weatherTextRight + kCounterGap;
  int16_t y = kCounterTopY;
  // Miejsce do prognozy (y 42) jest na 3 linie po 7 px; przy 4 liczbach ciasniej
  // (y 19, 25, 31, 37 - ostatnia konczy sie tuz nad prognoza).
  int16_t lineHeight = kCounterLineHeight;
  if (manga && comix && mail && live) {
    y = kCounterTopY - 3;
    lineHeight = kCounterLineHeight - 1;
  }
  if (manga) {
    drawCounterAt(_mangaUnread, x, y, kMangaHue);
    y += lineHeight;
  }
  if (comix) {
    drawCounterAt(_comixUnread, x, y, kComixHue);
    y += lineHeight;
  }
  if (mail) {
    drawCounterAt(_mailUnread, x, y, kMailHue);
    y += lineHeight;
  }
  if (live) {
    drawCounterAt(_liveCount, x, y, kLiveHue);
  }
}

// Pasek doby 00-24: dzien (od wschodu do zachodu slonca) zolty, noc granatowa.
// To, co juz minelo, swieci jasno, reszta przygaszona; teraz = biala kreska
// wystajaca nad i pod pasek. Bez danych o sloncu caly pasek jest granatowy.
static void drawDayProgress() {
  WeatherData weather = getWeather();
  bool hasSun = weather.valid && weather.sunriseMinutes >= 0 && weather.sunsetMinutes >= 0;
  uint8_t now = static_cast<uint8_t>((_elapsedSeconds % 86400UL) * kDayBarWidth / 86400UL);
  for (uint8_t column = 0; column < kDayBarWidth; column++) {
    int16_t minutes = static_cast<int16_t>(column * 1440L / kDayBarWidth);
    bool day = hasSun && minutes >= weather.sunriseMinutes && minutes < weather.sunsetMinutes;
    bool past = column < now;
    RgbColor color = day
      ? HslColor(0.12f, 1.0f, kLightness * (past ? 1.0f : 0.4f))
      : HslColor(0.64f, 1.0f, kLightness * (past ? 0.9f : 0.35f));
    fillClockRect(kDayBarX + column, kDayBarY, 1, 2, color);
  }
  fillClockRect(kDayBarX + now, kDayBarY - 1, 1, 4, HslColor(0.0f, 0.0f, kLightness * 2.0f));
}

void clockAnimation(const AnimationParam& param) {
  uint8_t hour = _elapsedSeconds / 3600UL;
  uint8_t minute = (_elapsedSeconds / 60UL) % 60;
  uint8_t values[4] {
    static_cast<uint8_t>(hour / 10), static_cast<uint8_t>(hour % 10),
    static_cast<uint8_t>(minute / 10), static_cast<uint8_t>(minute % 10)
  };
  float transitionProgress = getClockAnimationProgress(param);

  leds.ClearTo(kBlack);

  RgbColor hourRgb = getHourColor(_elapsedSeconds, kLightness);
  RgbColor minuteRgb = getMinuteColor(kLightness);
  int16_t slide = kClockDigitHeight + kClockDigitScale;
  for (uint8_t digit = 0; digit < 4; digit++) {
    const RgbColor& color = digit < 2 ? hourRgb : minuteRgb;
    bool digitChanged = _digitTransitionActive
      && _previousValues[digit] != values[digit];
    if (digitChanged) {
      // Stara cyfra zjezdza w dol, nowa wjezdza od dolu (jak w oryginale).
      drawClockDigit(digit, _previousValues[digit],
        static_cast<int16_t>(transitionProgress * slide), color);
      drawClockDigit(digit, values[digit],
        static_cast<int16_t>((1.0f - transitionProgress) * slide), color);
    } else {
      drawClockDigit(digit, values[digit], 0, color);
    }
  }
  if (_elapsedSeconds % 2 == 0) {
    drawClockColon(getSeparatorColor(kLightness));
  }

  drawSecondProgressBar(transitionProgress);
  drawWeather();
  drawForecast();
  drawDayProgress();
  drawCounters();
  drawNetworkStatus();
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    for (uint8_t digit = 0; digit < 4; digit++) {
      _previousValues[digit] = values[digit];
    }
    struct tm timeInfo;
    if (readLocalTime(&timeInfo)) {
      _elapsedSeconds = timeInfo.tm_hour * 3600UL
        + timeInfo.tm_min * 60UL + timeInfo.tm_sec;
      _hasValidTime = true;
    } else {
      _elapsedSeconds = (_elapsedSeconds + 1) % (24UL * 60UL * 60UL);
    }
    _digitTransitionActive = true;
    _clockFirstAnimation = false;
    animations.StartAnimation(0, getClockAnimationDurationMs(), clockAnimation);
  }
}

// Czas animacji zegara i kolory.

uint16_t getClockAnimationDurationMs() {
  struct timeval currentTime;
  gettimeofday(&currentTime, nullptr);
  uint32_t elapsedMilliseconds = currentTime.tv_usec / 1000;
  return static_cast<uint16_t>(1000 - elapsedMilliseconds);
}

float getClockAnimationProgress(const AnimationParam& param) {
  if (!_clockFirstAnimation) {
    return param.progress;
  }
  return _clockInitialProgress
    + param.progress * (1.0f - _clockInitialProgress);
}

HslColor getHourColor(uint32_t seconds, float lightness) {
  float hour = static_cast<uint8_t>(
    (seconds % (24UL * 60UL * 60UL)) / 3600UL);
  float hue;

  // Noc 22:00-06:00: niebieski -> fiolet -> zolty.
  if (hour < 6.0f) {
    hue = 0.60f + 0.56f * hour / 6.0f;
    if (hue >= 1.0f) {
      hue -= 1.0f;
    }
  // Rano 06:00-12:00: zolty -> zielony.
  } else if (hour < 12.0f) {
    hue = 0.16f + (0.33f - 0.16f) * (hour - 6.0f) / 6.0f;
  // Dzien 12:00-18:00: zielony -> czerwony.
  } else if (hour < 18.0f) {
    hue = 0.33f - 0.33f * (hour - 12.0f) / 6.0f;
  // Wieczor 18:00-22:00: czerwony -> niebieski.
  } else {
    hue = 0.60f * (hour - 18.0f) / 4.0f;
  }

  return HslColor(hue, 1.0f, lightness);
}

HslColor getMinuteColor(float lightness) {
  return HslColor(kDefaultMinuteHue, 1.0f, lightness);
}

HslColor getSeparatorColor(float lightness) {
  return HslColor(kDefaultSeparatorHue, 1.0f, lightness);
}

void initClock() {
  _animationMode = AnimationMode::Clock;
  _clockEnabled = true;
  if (!_hasValidTime) {
    _elapsedSeconds = kStartHour * 3600UL + kStartMinute * 60UL;
    _previousValues[0] = kStartHour / 10;
    _previousValues[1] = kStartHour % 10;
    _previousValues[2] = kStartMinute / 10;
    _previousValues[3] = kStartMinute % 10;
  }
  if (_clockStarted) {
    struct tm timeInfo;
    if (readLocalTime(&timeInfo)) {
      _elapsedSeconds = timeInfo.tm_hour * 3600UL
        + timeInfo.tm_min * 60UL + timeInfo.tm_sec;
      _previousValues[0] = timeInfo.tm_hour / 10;
      _previousValues[1] = timeInfo.tm_hour % 10;
      _previousValues[2] = timeInfo.tm_min / 10;
      _previousValues[3] = timeInfo.tm_min % 10;
    }
  }
  _digitTransitionActive = false;
  struct timeval currentTime;
  gettimeofday(&currentTime, nullptr);
  _clockInitialProgress = currentTime.tv_usec / 1000000.0f;
  _clockFirstAnimation = true;

  if (!_clockStarted) {
    _clockStarted = true;
    syncClockFromNtp();
    _lastNtpSync = millis();
  }
  animations.StartAnimation(0, getClockAnimationDurationMs(), clockAnimation);
}
