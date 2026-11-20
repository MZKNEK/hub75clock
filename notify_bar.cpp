// Pasek powiadomien w Automacie (mode_show.cpp): na dole ekranu (y 57-63)
// z lewej liczby MangaDex / Gmail / YouTube na zywo w ich kolorach (tylko
// wieksze od 0), z prawej godzina - uklad jak dolny wiersz panelu Claude.
// Tylko na ekranach bez wlasnej godziny
// i licznikow: Losowe, GIF, Lava, Gra w zycie, Obrazek, Labirynt.
// Ustawienie (strona, sekcja Automat; Preferences "disp" "bar"):
//   0 - wylaczony,
//   1 - staly; liczba, ktora wzrosla, miga przez kNotifyNewMs,
//   2 - tylko przy nowosci: pasek pojawia sie na kNotifyNewMs, gdy liczba wzrosnie.
// Uklad ("disp" "barfit"): pasek naklada sie na obraz albo zmniejsza go - wtedy
// ekrany z paskiem rysuja sie w getEffectRows() = 57 gornych wierszach (Lava,
// Losowe, Gra w zycie, Labirynt od nowa na tej wysokosci; GIF i obrazek
// zmniejszone do 57x57 i wysrodkowane). Przy "tylko przy nowosci" pole jest
// mniejsze caly czas (inaczej efekt skakalby przy kazdym powiadomieniu).
#include "zegar.h"
#include <Preferences.h>

// Ostatnia wartosc licznika i kiedy wzrosl.
struct NotifyCounter {
  int16_t lastSeen;
  uint32_t grewAtMs;
};

static constexpr uint8_t kNotifyBarOff{0};
static constexpr uint8_t kNotifyBarAlways{1};
static constexpr uint8_t kNotifyBarOnNews{2};
static constexpr uint32_t kNotifyNewMs{30000UL};
static constexpr int16_t kNotifyBarTop{57};
static constexpr int16_t kNotifyTextY{58};

static uint8_t _notifyBarMode{kNotifyBarAlways};
static bool _notifyBarFit{false}; // true = pasek zmniejsza obraz, false = naklada sie

// Ostatnio widziane wartosci licznikow i kiedy ktory wzrosl.
static NotifyCounter _notifyManga{-1, 0};
static NotifyCounter _notifyMail{-1, 0};
static NotifyCounter _notifyComix{-1, 0};
static NotifyCounter _notifyLive{-1, 0};

void loadNotifyBar() {
  Preferences preferences;
  if (preferences.begin("disp", true)) {
    _notifyBarMode = preferences.getUChar("bar", kNotifyBarAlways) % 3;
    _notifyBarFit = preferences.getBool("barfit", false);
    preferences.end();
  }
}

void setNotifyBarMode(uint8_t mode) {
  _notifyBarMode = mode % 3;
  Preferences preferences;
  if (preferences.begin("disp", false)) {
    preferences.putUChar("bar", _notifyBarMode);
    preferences.end();
  }
}

uint8_t getNotifyBarMode() {
  return _notifyBarMode;
}

void setNotifyBarFit(bool fit) {
  _notifyBarFit = fit;
  Preferences preferences;
  if (preferences.begin("disp", false)) {
    preferences.putBool("barfit", fit);
    preferences.end();
  }
}

bool getNotifyBarFit() {
  return _notifyBarFit;
}

// Wzrost liczby = nowosc. Pierwsza wartosc po starcie (-1 -> N) nie jest nowoscia.
static void trackNotifyCounter(NotifyCounter& counter, int16_t value) {
  if (counter.lastSeen >= 0 && value > counter.lastSeen) {
    counter.grewAtMs = millis() | 1;
  }
  counter.lastSeen = value;
}

static bool isNotifyNew(const NotifyCounter& counter) {
  return counter.grewAtMs != 0 && millis() - counter.grewAtMs < kNotifyNewMs;
}

static bool isNotifyBarScreen() {
  switch (_animationMode) {
    case AnimationMode::Random:
    case AnimationMode::Gif:
    case AnimationMode::Lava:
    case AnimationMode::Life:
    case AnimationMode::Image:
    case AnimationMode::Maze:
      return true;
    default:
      return false;
  }
}

// Ile gornych wierszy ma efekt: 57, gdy pasek Automatu zmniejsza obraz na tym
// ekranie, inaczej 64. Tryby sprawdzaja to co klatke (Automat wlacza sie juz po
// starcie trybu).
uint8_t getEffectRows() {
  return _notifyBarFit && _notifyBarMode != kNotifyBarOff && isShowRunning()
    && isNotifyBarScreen() ? kNotifyBarTop : kRows;
}

// Liczba od lewej (zwraca x nastepnej); nowa miga (pol sekundy swieci, pol nie).
static int16_t drawNotifyCounter(int16_t x, int16_t count, uint32_t updatedAtMs,
  uint32_t staleMs, const NotifyCounter& counter, float hue) {
  if (!isCounterVisible(count, updatedAtMs, staleMs)) {
    return x;
  }
  char text[4];
  formatCounter(text, sizeof(text), count);
  if (!isNotifyNew(counter) || (millis() / 500UL) % 2 == 0) {
    drawSmallText(text, x, kNotifyTextY, 1, HslColor(hue, 1.0f, kLightness));
  }
  return x + getDashTextWidth(text) + kCounterGap;
}

// Wolane w loop() po animacjach (przed wiadomoscia /msg).
void drawNotifyBar() {
  trackNotifyCounter(_notifyManga, _mangaUnread);
  trackNotifyCounter(_notifyComix, _comixUnread);
  trackNotifyCounter(_notifyMail, _mailUnread);
  trackNotifyCounter(_notifyLive, _liveCount);
  if (_notifyBarMode == kNotifyBarOff || !isShowRunning() || !isNotifyBarScreen()) {
    return;
  }
  bool anyNew = isNotifyNew(_notifyManga) || isNotifyNew(_notifyComix) || isNotifyNew(_notifyMail)
    || isNotifyNew(_notifyLive);
  if (_notifyBarMode == kNotifyBarOnNews && !anyNew) {
    return;
  }
  fillClockRect(0, kNotifyBarTop, kColumns, kRows - kNotifyBarTop, kBlack);
  // Od lewej: MangaDex, comix, Gmail, YouTube - kolejnosc jak na zegarze i panelu Claude.
  int16_t x = 2;
  x = drawNotifyCounter(x, _mangaUnread, _mangaUpdatedAtMs, kMangaStaleMs, _notifyManga, kMangaHue);
  x = drawNotifyCounter(x, _comixUnread, _comixUpdatedAtMs, kComixStaleMs, _notifyComix, kComixHue);
  x = drawNotifyCounter(x, _mailUnread, _mailUpdatedAtMs, kMailStaleMs, _notifyMail, kMailHue);
  drawNotifyCounter(x, _liveCount, _liveUpdatedAtMs, kLiveStaleMs, _notifyLive, kLiveHue);
  struct tm timeInfo;
  if (_hasValidTime && readLocalTime(&timeInfo)) {
    char clockText[6];
    snprintf(clockText, sizeof(clockText), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
    uint32_t secondsOfDay = timeInfo.tm_hour * 3600UL + timeInfo.tm_min * 60UL;
    drawSmallText(clockText, kColumns - 2 - getDashTextWidth(clockText), kNotifyTextY, 1,
      getHourColor(secondsOfDay, kLightness * 0.8f));
  }
}
