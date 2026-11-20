// Jasnosc ekranu: suwak na stronie www (0-8), zapamietywana w Preferences.
//   0   - ekran wylaczony (wiadomosci /msg nadal sie pokazuja; wlacza sie
//         suwakiem albo przyciskiem "Domyslna" na stronie),
//   3   - kDefaultLightness (mnoznik 1; od niej licza sie kolory),
//   8   - ok. 4,5x jasniej, jasnosc domyslna (przycisk "Domyslna", start bez
//         zapisu); wyzej kolory przechodza w biel (kHslOutputGain).
// Ponizej poziomu 3 i w nocy ekran ciemnieje sprzetowo (applyBrightness).
// GET /brightness?v=0..8  (patrz web_control.cpp)
//
// Tryb nocny (ustawiany na stronie, GET /night, zapis w Preferences "disp"):
//   - od _nightOffStart do _nightOffEnd (domyslnie 23-6) ekran wylaczony jak
//     przy jasnosci 0 (wiadomosci dalej widac); "Wlacz do rana" (wake=1) wlacza
//     go do konca tej nocy; start == koniec = bez wylaczania,
//     Aktywnosc na komputerze tez wlacza ekran: gra muzyka, Claude pracuje/czeka
//     albo ruch myszki/klawiatury (idle z muzyka_zegar.ps1); gasnie znow po
//     kNightActivityHoldMs bez aktywnosci,
//   - po zmroku (od zachodu do wschodu slonca z pogody) jasnosc x kNightDimFactor,
//   - "W nocy" (_nightClockLevel, Preferences "nclk"): 0 = ekran czarny,
//     1-3 = zamiast czarnego ekranu sama godzina na czerwono (drawNightClock
//     w clock.cpp), przyciemniona sprzetowo (kNightClockPanelBrightness).
//     Reczne "Wylacz ekran" (jasnosc 0) dalej gasi wszystko.
#include "zegar.h"
#include <Preferences.h>

float kLightness{kDefaultLightness};

static constexpr uint8_t kBrightnessLevels{9};
static constexpr float kBrightnessFactors[kBrightnessLevels] {
  0.0f, 0.5f, 0.7f, 1.0f, 1.4f, 2.0f, 2.7f, 3.5f, 4.5f
};
static constexpr uint8_t kDefaultBrightnessLevel{8};
static uint8_t _brightnessLevel{kDefaultBrightnessLevel};
static uint8_t _brightnessBeforeOff{kDefaultBrightnessLevel};

static constexpr float kNightDimFactor{0.6f};
static constexpr uint32_t kNightActivityHoldMs{5UL * 60UL * 1000UL};
static constexpr uint32_t kPcInputActiveSeconds{120}; // ruch myszki = aktywny
static uint32_t _pcInputAtMs{0};       // ostatni ruch myszki/klawiatury
static uint32_t _activityAtMs{0};      // ostatnia aktywnosc na komputerze
static bool _activityRecent{false};
static uint8_t _nightOffStart{23};   // godzina wylaczenia ekranu
static uint8_t _nightOffEnd{6};      // godzina wlaczenia
static bool _nightDimEnabled{true};  // przyciemnianie po zmroku
static bool _nightOff{false};        // teraz jest pora wylaczenia
static bool _nightWoken{false};      // "Wlacz do rana" w tej nocy
static bool _dark{false};            // teraz jest po zmroku
static bool _offUntilMorning{false}; // "Wylacz ekran" po zmroku/w nocy - wlacz rano
static bool _timeKnown{false};       // zegar systemowy ma poprawny czas
static uint8_t _lastHour{0};
// Nocny zegar: sprzetowa jasnosc panelu (0-255) dla poziomow 1-3. Panel ma ok.
// 60 krokow (szerokosc okna OE w taktach), 4 = najmniejszy (1 takt).
static constexpr uint8_t kNightClockLevels{4};
static constexpr uint8_t kNightClockPanelBrightness[kNightClockLevels] {0, 4, 12, 30};
static uint8_t _nightClockLevel{0};
static bool _nightClockApplied{false};

// Teraz zamiast czarnego ekranu nocny zegar (loop -> drawNightClock).
bool isNightClockShown() {
  return _nightClockLevel > 0 && _brightnessLevel > 0
    && _nightOff && !_nightWoken && !_activityRecent;
}

// Przyciemnianie (mnoznik < 1 i noc) idzie przez sprzetowa jasnosc panelu
// (OE w bibliotece DMA) - wszystkie kolory ciemnieja rowno i nic nie gasnie.
// Rozjasnianie (> 1) przez kolory (kLightness), bo sprzetowo jasniej niz 255
// sie nie da. Przy wylaczonym ekranie kLightness zostaje z ostatniej jasnosci,
// zeby palety GIF-ow i obrazkow sie nie psuly.
static void applyBrightness() {
  uint8_t level = _brightnessLevel > 0 ? _brightnessLevel : _brightnessBeforeOff;
  float factor = kBrightnessFactors[level];
  float hardware = factor < 1.0f ? factor : 1.0f;
  if (_dark && _nightDimEnabled) {
    hardware *= kNightDimFactor;
  }
  kLightness = kDefaultLightness * (factor > 1.0f ? factor : 1.0f);
  _nightClockApplied = isNightClockShown();
  leds.SetBrightness(_nightClockApplied
    ? kNightClockPanelBrightness[_nightClockLevel]
    : static_cast<uint8_t>(lroundf(255.0f * hardware)));
}

void loadBrightness() {
  Preferences preferences;
  if (preferences.begin("disp", true)) {
    _brightnessLevel = preferences.getUChar("lvl", kDefaultBrightnessLevel);
    _brightnessBeforeOff = preferences.getUChar("on", kDefaultBrightnessLevel);
    _offUntilMorning = preferences.getBool("offm", false);
    _nightOffStart = preferences.getUChar("noff_s", 23) % 24;
    _nightOffEnd = preferences.getUChar("noff_e", 6) % 24;
    _nightDimEnabled = preferences.getBool("ndim", true);
    _nightClockLevel = preferences.getUChar("nclk", 0) % kNightClockLevels;
    preferences.end();
  }
  if (_brightnessLevel >= kBrightnessLevels) {
    _brightnessLevel = kDefaultBrightnessLevel;
  }
  if (_brightnessBeforeOff == 0 || _brightnessBeforeOff >= kBrightnessLevels) {
    _brightnessBeforeOff = kDefaultBrightnessLevel;
  }
  applyBrightness();
}

void setBrightnessLevel(uint8_t level) {
  if (level >= kBrightnessLevels) {
    level = kBrightnessLevels - 1;
  }
  if (level > 0) {
    _brightnessBeforeOff = level;
  }
  // Wylaczenie wieczorem / w nocy = do rana (koniec okna nocnego albo wschod).
  _offUntilMorning = level == 0 && (_nightOff || _dark);
  _brightnessLevel = level;
  applyBrightness();
  Preferences preferences;
  if (preferences.begin("disp", false)) {
    preferences.putUChar("lvl", _brightnessLevel);
    preferences.putUChar("on", _brightnessBeforeOff);
    preferences.putBool("offm", _offUntilMorning);
    preferences.end();
  }
}

uint8_t getBrightnessLevel() {
  return _brightnessLevel;
}

bool isScreenOff() {
  return _brightnessLevel == 0 || (_nightOff && !_nightWoken && !_activityRecent);
}

// Z /np?idle=<s>: ile sekund temu ruszano myszka/klawiatura na komputerze.
void notePcIdleSeconds(uint32_t idleSeconds) {
  if (idleSeconds <= kPcInputActiveSeconds) {
    _pcInputAtMs = millis() - idleSeconds * 1000UL;
  }
}

static bool isHourInRange(uint8_t hour, uint8_t start, uint8_t end) {
  if (start == end) {
    return false;
  }
  return start < end ? hour >= start && hour < end : hour >= start || hour < end;
}

// Wolane w loop(): pora wylaczenia i zmrok liczone raz na sekunde.
void updateNightMode() {
  static uint32_t lastCheckMs{0};
  if (millis() - lastCheckMs < 1000UL && lastCheckMs != 0) {
    return;
  }
  lastCheckMs = millis();
  // Zegar systemowy (SNTP), nie _hasValidTime - wystarczy, ze czas jest poprawny.
  struct tm timeInfo;
  _timeKnown = readLocalTime(&timeInfo);
  if (!_timeKnown) {
    return;
  }
  _lastHour = static_cast<uint8_t>(timeInfo.tm_hour);
  bool off = isHourInRange(_lastHour, _nightOffStart, _nightOffEnd);
  if (!off) {
    _nightWoken = false; // noc sie skonczyla - nastepna znow wylacza
  }
  bool nightEnded = _nightOff && !off;
  _nightOff = off;

  bool pcInput = _pcInputAtMs != 0 && millis() - _pcInputAtMs <= kPcInputActiveSeconds * 1000UL;
  if (isMusicPlaying() || isClaudeBusy() || pcInput) {
    _activityAtMs = millis();
  }
  _activityRecent = _activityAtMs != 0 && millis() - _activityAtMs <= kNightActivityHoldMs;
  if (isNightClockShown() != _nightClockApplied) {
    applyBrightness(); // nocny zegar wlaczyl sie / zniknal
  }

  WeatherData weather = getWeather();
  int16_t minutes = static_cast<int16_t>(timeInfo.tm_hour * 60 + timeInfo.tm_min);
  bool dark = weather.valid && weather.sunriseMinutes >= 0 && weather.sunsetMinutes >= 0
    && (minutes < weather.sunriseMinutes || minutes >= weather.sunsetMinutes);
  bool sunrise = _dark && !dark;
  if (dark != _dark) {
    _dark = dark;
    applyBrightness();
  }
  // Rano po recznym "Wylacz ekran": koniec okna nocnego (bez okna - wschod).
  bool windowEnabled = _nightOffStart != _nightOffEnd;
  if (_offUntilMorning && (windowEnabled ? nightEnded : sunrise)) {
    setBrightnessLevel(_brightnessBeforeOff);
  }
}

// Diagnostyka do /info: dlaczego ekran swieci / nie swieci w nocy.
String getNightStatusJson() {
  long pcInputAgo = _pcInputAtMs == 0 ? -1 : static_cast<long>((millis() - _pcInputAtMs) / 1000UL);
  long activityAgo = _activityAtMs == 0 ? -1 : static_cast<long>((millis() - _activityAtMs) / 1000UL);
  return String("{\"time_ok\":") + (_timeKnown ? "true" : "false")
    + ",\"hour\":" + _lastHour + ",\"night_off\":" + (_nightOff ? "true" : "false")
    + ",\"woken\":" + (_nightWoken ? "true" : "false")
    + ",\"off_until_morning\":" + (_offUntilMorning ? "true" : "false")
    + ",\"dark\":" + (_dark ? "true" : "false")
    + ",\"activity_recent\":" + (_activityRecent ? "true" : "false")
    + ",\"activity_s_ago\":" + activityAgo
    + ",\"music\":" + (isMusicPlaying() ? "true" : "false")
    + ",\"claude_busy\":" + (isClaudeBusy() ? "true" : "false")
    + ",\"pc_input_s_ago\":" + pcInputAgo
    + ",\"screen_off\":" + (isScreenOff() ? "true" : "false")
    + ",\"night_clock\":" + _nightClockLevel
    + ",\"night_clock_shown\":" + (isNightClockShown() ? "true" : "false") + "}";
}

void setNightMode(uint8_t start, uint8_t end, bool dim, uint8_t clockLevel) {
  _nightOffStart = start % 24;
  _nightOffEnd = end % 24;
  _nightDimEnabled = dim;
  _nightClockLevel = clockLevel % kNightClockLevels;
  applyBrightness();
  Preferences preferences;
  if (preferences.begin("disp", false)) {
    preferences.putUChar("noff_s", _nightOffStart);
    preferences.putUChar("noff_e", _nightOffEnd);
    preferences.putBool("ndim", _nightDimEnabled);
    preferences.putUChar("nclk", _nightClockLevel);
    preferences.end();
  }
}

uint8_t getNightClockLevel() {
  return _nightClockLevel;
}

void wakeUntilMorning() {
  _nightWoken = true;
}

uint8_t getNightOffStart() {
  return _nightOffStart;
}

uint8_t getNightOffEnd() {
  return _nightOffEnd;
}

bool isNightDimEnabled() {
  return _nightDimEnabled;
}

// Teraz pora wylaczenia (niezaleznie od "Wlacz do rana").
bool isNightOffTime() {
  return _nightOff;
}

bool isNightWoken() {
  return _nightWoken;
}

bool isPcActivityRecent() {
  return _activityRecent;
}

bool isDarkNow() {
  return _dark;
}
