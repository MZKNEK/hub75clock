// Automat: zegar sam przelacza tryby wedlug listy ustawionej na stronie.
// Pozycja listy = tryb (+ konkretny GIF / obrazek) i czas w sekundach.
// Lista w Preferences "show" ("list" - bajty ShowEntry, "on" - Automat wlaczony,
// startuje znowu po restarcie). GET /show?e=<tryb.wariant>&t=<s>&e=...&t=...
// (kolejnosc par = kolejnosc listy; puste e = pozycja usunieta), run=1 - wlacz.
// Pomijane: "Teraz grane", gdy nic nie gra; "Komputer" bez danych; obrazek,
// ktorego juz nie ma. Kazda reczna zmiana trybu (przyciski, strona) konczy
// Automat; panel Claude wlaczony automatycznie wraca potem do Automatu.
// GIF i obrazek z listy nie zmieniaja zapamietanego recznego wyboru.
#include "zegar.h"
#include <Preferences.h>

// Pozycja listy - tryb, wariant (GIF / obrazek) i czas.
struct ShowEntry {
  uint8_t mode;     // AnimationMode
  int8_t option;    // GIF: numer albo -1 (losowo); Obrazek: numer; reszta: 0
  uint16_t seconds;
};
static bool _showRunning{false};
bool _showSwitching{false};

// kShowMaxEntries (20) jest w zegar.h - uzywa go tez strona.
static constexpr uint16_t kShowMinSeconds{5};
static constexpr uint16_t kShowMaxSeconds{3600};

static ShowEntry _showEntries[kShowMaxEntries];
static uint8_t _showCount{0};
static uint8_t _showIndex{0};
static uint32_t _showEntryStartedAtMs{0};

static void saveShowRunning(bool running) {
  Preferences preferences;
  if (preferences.begin("show", false)) {
    if (preferences.getBool("on", false) != running) {
      preferences.putBool("on", running);
    }
    preferences.end();
  }
}

bool isShowRunning() {
  return _showRunning;
}

uint8_t getShowCount() {
  return _showCount;
}

// Pozycja do strony: tryb, wariant, czas (false = poza lista).
bool getShowEntry(uint8_t index, uint8_t* mode, int8_t* option, uint16_t* seconds) {
  if (index >= _showCount) {
    return false;
  }
  *mode = _showEntries[index].mode;
  *option = _showEntries[index].option;
  *seconds = _showEntries[index].seconds;
  return true;
}

static bool isShowEntryValid(const ShowEntry& entry) {
  AnimationMode mode = static_cast<AnimationMode>(entry.mode);
  if (entry.mode >= static_cast<uint8_t>(AnimationMode::Show)) {
    return false;
  }
  if (mode == AnimationMode::Gif) {
    return entry.option >= -1 && entry.option < kGifMax; // numer na partycji
  }
  return mode != AnimationMode::Image || entry.option >= 0;
}

// Czy pozycja ma teraz co pokazac.
static bool isShowEntryAvailable(const ShowEntry& entry) {
  switch (static_cast<AnimationMode>(entry.mode)) {
    case AnimationMode::Music:
      return isMusicPlaying();
    case AnimationMode::Pc:
      return isPcStatsFresh();
    case AnimationMode::Image:
      return entry.option < getImageCount();
    case AnimationMode::Gif:
      return getGifCount() > 0 && entry.option < getGifCount();
    default:
      return true;
  }
}

static void applyShowEntry(uint8_t index) {
  const ShowEntry& entry = _showEntries[index];
  AnimationMode mode = static_cast<AnimationMode>(entry.mode);
  setGifShowOverride(mode == AnimationMode::Gif ? entry.option : -2);
  setImageShowOverride(mode == AnimationMode::Image ? entry.option : -1);
  _showSwitching = true;
  selectAnimation(mode);
  _showSwitching = false;
  _showRunning = true;
  _showIndex = index;
  _showEntryStartedAtMs = millis();
}

// Pierwsza pozycja od "from" (wlacznie), ktora ma co pokazac; brak = -1.
static int16_t findAvailableShowEntry(uint8_t from) {
  for (uint8_t step = 0; step < _showCount; step++) {
    uint8_t index = (from + step) % _showCount;
    if (isShowEntryAvailable(_showEntries[index])) {
      return index;
    }
  }
  return -1;
}

// Wlaczenie (strona, przyciski, powrot z panelu Claude): od pozycji, na ktorej
// Automat skonczyl - po panelu Claude lista idzie dalej, a nie od poczatku.
void startShow() {
  if (_showCount == 0) {
    _showRunning = false;
    selectAnimation(AnimationMode::Clock);
    showMessage("AUTOMAT: USTAW LIST\xC4\x98 NA STRONIE", 0, 0.13f);
    return;
  }
  int16_t index = findAvailableShowEntry(_showIndex % _showCount);
  applyShowEntry(index >= 0 ? static_cast<uint8_t>(index) : _showIndex % _showCount);
  saveShowRunning(true);
}

// Reczna zmiana trybu (selectAnimation spoza Automatu).
void stopShow() {
  if (!_showRunning) {
    return;
  }
  _showRunning = false;
  setGifShowOverride(-2);
  setImageShowOverride(-1);
  saveShowRunning(false);
}

// Wolane w loop(): po czasie pozycji nastepna, ktora ma co pokazac.
void updateShow() {
  if (!_showRunning || _showCount == 0) {
    return;
  }
  uint32_t durationMs = _showEntries[_showIndex].seconds * 1000UL;
  if (millis() - _showEntryStartedAtMs < durationMs) {
    return;
  }
  int16_t next = findAvailableShowEntry((_showIndex + 1) % _showCount);
  if (next < 0 || next == _showIndex) {
    _showEntryStartedAtMs = millis(); // nic innego do pokazania - zostaje
    return;
  }
  applyShowEntry(static_cast<uint8_t>(next));
}

void loadShow() {
  Preferences preferences;
  bool running = false;
  if (preferences.begin("show", true)) {
    size_t bytes = preferences.getBytesLength("list");
    if (bytes > 0 && bytes % sizeof(ShowEntry) == 0 && bytes <= sizeof(_showEntries)) {
      preferences.getBytes("list", _showEntries, bytes);
      _showCount = bytes / sizeof(ShowEntry);
    }
    running = preferences.getBool("on", false);
    preferences.end();
  }
  // Wpisy z nieistniejacymi trybami (np. po zmianie programu) wypadaja.
  uint8_t kept = 0;
  for (uint8_t index = 0; index < _showCount; index++) {
    if (isShowEntryValid(_showEntries[index])) {
      _showEntries[kept++] = _showEntries[index];
    }
  }
  _showCount = kept;
  if (running && _showCount > 0) {
    startShow();
  }
}

// Nowa lista ze strony: "tryb.wariant" + sekundy. Zwraca liczbe pozycji.
uint8_t setShowEntriesFromWeb(const String* values, const uint16_t* seconds, uint8_t count) {
  ShowEntry entries[kShowMaxEntries];
  uint8_t kept = 0;
  for (uint8_t index = 0; index < count && kept < kShowMaxEntries; index++) {
    int dot = values[index].indexOf('.');
    if (dot <= 0) {
      continue; // "" = usunieta / pusta pozycja
    }
    ShowEntry entry;
    entry.mode = static_cast<uint8_t>(values[index].substring(0, dot).toInt());
    entry.option = static_cast<int8_t>(values[index].substring(dot + 1).toInt());
    entry.seconds = constrain(seconds[index], kShowMinSeconds, kShowMaxSeconds);
    if (isShowEntryValid(entry)) {
      entries[kept++] = entry;
    }
  }
  memcpy(_showEntries, entries, kept * sizeof(ShowEntry));
  _showCount = kept;
  if (_showIndex >= _showCount) {
    _showIndex = 0;
  }
  Preferences preferences;
  if (preferences.begin("show", false)) {
    if (kept > 0) {
      preferences.putBytes("list", _showEntries, kept * sizeof(ShowEntry));
    } else {
      preferences.remove("list");
    }
    preferences.end();
  }
  if (_showRunning) {
    if (_showCount == 0) {
      selectAnimation(AnimationMode::Clock); // konczy Automat
    } else {
      startShow(); // nowa lista od razu
    }
  }
  return kept;
}
