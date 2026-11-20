// Tryb "Teraz grane": okladka 48x48, przewijany wykonawca i tytul, pasek postepu.
// Dane wysyla narzedzia/muzyka_zegar.ps1 z odtwarzacza Windows (Spotify,
// YouTube w przegladarce...) - patrz web_control.cpp:
//   GET  /np?a=Wykonawca&t=Tytul&p=<pozycja ms>&d=<dlugosc ms>&s=1|0  (gra / pauza)
//   GET  /np?off=1   - nic nie gra
//   (+ &idle=<s> - czas od ostatniego ruchu myszki/klawiatury, dla trybu nocnego)
//   POST /cover      - okladka 48x48 RGB jako hex (13824 znakow)
// Bez wiadomosci od komputera przez kNowPlayingStaleMs ekran pokazuje "nic nie gra".
// Tylko w RAM.
#include "zegar.h"

static constexpr int16_t kCoverSize{48};
static constexpr int16_t kCoverX{8};
static constexpr int16_t kCoverY{1};
static constexpr size_t kCoverBytes{kCoverSize * kCoverSize * 3};
static constexpr int16_t kMusicTextY{52};
static constexpr int16_t kMusicBarX{4};
static constexpr int16_t kMusicBarY{60};
static constexpr int16_t kMusicBarWidth{56};
static constexpr float kMusicScrollPxPerSecond{16.0f};
static constexpr int16_t kMusicScrollGap{16};
static constexpr uint32_t kMusicScrollPauseMs{1500UL}; // tekst chwile stoi na starcie
static constexpr uint32_t kNowPlayingStaleMs{60000UL};
static constexpr float kMusicHue{0.39f}; // zielen Spotify

static char _npText[128]{};  // "WYKONAWCA - TYTUL" w znakach czcionki zegara
static bool _npActive{false};
static bool _npPlaying{false};
static uint32_t _npPositionMs{0};
static uint32_t _npDurationMs{0};
static uint32_t _npReceivedAtMs{0};
static uint32_t _npTextChangedAtMs{0};
static uint8_t _coverRaw[kCoverBytes];
// Przeliczone kolory okladki (7 KB) - tylko w trybie "Teraz grane"
// (stopMusicMode zwalnia); surowa okladka _coverRaw zostaje na stale.
static RgbColor* _coverFrame{nullptr};
static bool _coverValid{false};
static float _coverLightness{-1.0f};

void updateNowPlaying(const String& artist, const String& title, uint32_t positionMs,
  uint32_t durationMs, bool playing) {
  char text[sizeof(_npText)];
  foldTextForDisplay(artist.length() > 0 ? artist + " - " + title : title, text, sizeof(text));
  if (strcmp(text, _npText) != 0) {
    // Nowy utwor: stara okladka nie pasuje, komputer zaraz przysle nowa.
    strcpy(_npText, text);
    _npTextChangedAtMs = millis();
    _coverValid = false;
  }
  _npPositionMs = positionMs;
  _npDurationMs = durationMs;
  _npPlaying = playing;
  _npActive = true;
  _npReceivedAtMs = millis();
}

void clearNowPlaying() {
  _npActive = false;
  _npReceivedAtMs = millis();
}

bool setNowPlayingCover(const String& hex) {
  if (hex.length() != kCoverBytes * 2) {
    return false;
  }
  const char* text = hex.c_str();
  for (size_t i = 0; i < kCoverBytes; i++) {
    if (!decodeHexByte(text[i * 2], text[i * 2 + 1], &_coverRaw[i])) {
      _coverValid = false;
      return false;
    }
  }
  _coverValid = true;
  _coverLightness = -1.0f; // przelicz kolory
  return true;
}

static bool isNowPlayingFresh() {
  return _npActive && millis() - _npReceivedAtMs <= kNowPlayingStaleMs;
}

// Tryb nocny (brightness.cpp): gra muzyka = ekran zostaje wlaczony.
bool isMusicPlaying() {
  return isNowPlayingFresh() && _npPlaying;
}

String getNowPlayingJson() {
  return String("{\"active\":") + (isNowPlayingFresh() ? "true" : "false")
    + ",\"playing\":" + (_npPlaying ? "true" : "false")
    + ",\"position_ms\":" + _npPositionMs + ",\"duration_ms\":" + _npDurationMs
    + ",\"cover\":" + (_coverValid ? "true" : "false") + "}";
}

static void drawCover() {
  if (!_coverValid) {
    // Okladka jeszcze nie doszla: sama ramka.
    RgbColor frame = HslColor(0.62f, 1.0f, kLightness * 0.3f);
    fillClockRect(kCoverX, kCoverY, kCoverSize, 1, frame);
    fillClockRect(kCoverX, kCoverY + kCoverSize - 1, kCoverSize, 1, frame);
    fillClockRect(kCoverX, kCoverY, 1, kCoverSize, frame);
    fillClockRect(kCoverX + kCoverSize - 1, kCoverY, 1, kCoverSize, frame);
    return;
  }
  if (_coverFrame == nullptr) {
    _coverFrame = static_cast<RgbColor*>(malloc(kCoverSize * kCoverSize * sizeof(RgbColor)));
    if (_coverFrame == nullptr) {
      return;
    }
    _coverLightness = -1.0f;
  }
  if (_coverLightness != kLightness) {
    float level = getPictureLevel();
    for (uint16_t i = 0; i < kCoverSize * kCoverSize; i++) {
      const uint8_t* rgb = _coverRaw + i * 3;
      _coverFrame[i] = toPanelColor(rgb[0], rgb[1], rgb[2], level);
    }
    _coverLightness = kLightness;
  }
  for (int16_t y = 0; y < kCoverSize; y++) {
    for (int16_t x = 0; x < kCoverSize; x++) {
      leds.SetPixelColor(pixelIndex(kCoverY + y, kCoverX + x), _coverFrame[y * kCoverSize + x]);
    }
  }
}

// Pauza: dwie kreski w prawym dolnym rogu okladki.
static void drawPauseMark() {
  RgbColor color = HslColor(0.0f, 0.0f, kLightness * 1.5f);
  int16_t x = kCoverX + kCoverSize - 8;
  int16_t y = kCoverY + kCoverSize - 9;
  fillClockRect(x - 1, y - 1, 8, 9, kBlack);
  fillClockRect(x, y, 2, 7, color);
  fillClockRect(x + 4, y, 2, 7, color);
}

static void drawMusicText() {
  int16_t width = getDashTextWidth(_npText);
  RgbColor color = HslColor(0.0f, 0.0f, kLightness * (_npPlaying ? 1.0f : 0.5f));
  if (width <= kColumns - 4) {
    drawSmallText(_npText, (kColumns - width) / 2, kMusicTextY, 1, color);
    return;
  }
  // Dlugi tekst przewija sie w kolko; po zmianie utworu chwile stoi na poczatku.
  uint32_t elapsed = millis() - _npTextChangedAtMs;
  uint32_t moving = elapsed > kMusicScrollPauseMs ? elapsed - kMusicScrollPauseMs : 0;
  int16_t span = width + kMusicScrollGap;
  int16_t offset = static_cast<int16_t>(
    static_cast<uint32_t>(moving * kMusicScrollPxPerSecond / 1000.0f) % span);
  drawSmallText(_npText, 2 - offset, kMusicTextY, 1, color);
  drawSmallText(_npText, 2 - offset + span, kMusicTextY, 1, color);
}

static void drawMusicProgress() {
  if (_npDurationMs == 0) {
    return;
  }
  uint32_t position = _npPositionMs;
  if (_npPlaying) {
    position += millis() - _npReceivedAtMs;
  }
  if (position > _npDurationMs) {
    position = _npDurationMs;
  }
  int16_t filled = static_cast<int16_t>(
    static_cast<uint64_t>(position) * kMusicBarWidth / _npDurationMs);
  RgbColor fill = HslColor(kMusicHue, 1.0f, kLightness * (_npPlaying ? 0.9f : 0.45f));
  RgbColor track = HslColor(0.62f, 1.0f, kLightness * 0.3f);
  fillClockRect(kMusicBarX, kMusicBarY, filled, 2, fill);
  fillClockRect(kMusicBarX + filled, kMusicBarY, kMusicBarWidth - filled, 2, track);
}

static void drawNothingPlaying() {
  RgbColor color = HslColor(0.58f, 0.5f, kLightness * 0.6f);
  const char* text = "NIC NIE GRA";
  drawSmallText(text, (kColumns - getDashTextWidth(text)) / 2, 24, 1, color);
  struct tm timeInfo;
  if (_hasValidTime && readLocalTime(&timeInfo)) {
    char clockText[6];
    snprintf(clockText, sizeof(clockText), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
    uint32_t secondsOfDay = timeInfo.tm_hour * 3600UL + timeInfo.tm_min * 60UL;
    drawSmallText(clockText, (kColumns - getDashTextWidth(clockText)) / 2, 34, 1,
      getHourColor(secondsOfDay, kLightness * 0.8f));
  }
}

// Wyjscie z trybu (selectAnimation) - przeliczona okladka nie jest potrzebna.
void stopMusicMode() {
  free(_coverFrame);
  _coverFrame = nullptr;
}

void musicAnimation(const AnimationParam& param) {
  leds.ClearTo(kBlack);
  if (isNowPlayingFresh()) {
    drawCover();
    if (!_npPlaying) {
      drawPauseMark();
    }
    drawMusicText();
    drawMusicProgress();
  } else {
    drawNothingPlaying();
  }
  drawNetworkStatus();
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(0);
  }
}
