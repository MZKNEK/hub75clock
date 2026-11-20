// Tryb "Gra w zycie" (Conway): komorki 2x2 px na planszy 32x32 zawinietej
// na brzegach. Kolor zalezy od wieku komorki: nowe turkusowe, starsze
// niebieskie -> fioletowe -> czerwone po 12 pokoleniach (stabilne bloki).
// Martwe komorki przez chwile gasna. Gdy plansza zamrze albo utknie w
// powtarzajacym sie ukladzie, startuje od nowa z losowym ukladem.
#include "zegar.h"

static constexpr uint8_t kLifeCell{2};
static constexpr uint8_t kLifeSize{kColumns / kLifeCell};  // 32 x 32
static constexpr uint32_t kLifeStepMs{120};
static constexpr uint8_t kLifeFadeSteps{3};
static constexpr uint8_t kLifeFillPercent{30};
static constexpr uint8_t kLifeHistory{32};                 // pamiec ukladow (wykrycie petli)
static constexpr uint8_t kLifeStuckGenerations{40};        // tyle w petli -> nowa plansza
static constexpr uint16_t kLifeMaxGenerations{2000};
static constexpr uint8_t kLifeAgeForRed{12};

static uint8_t _lifeAge[kLifeSize][kLifeSize];   // 0 = martwa, inaczej wiek
static uint8_t _lifeFade[kLifeSize][kLifeSize];  // ile krokow jeszcze gasnie
static uint32_t _lifeHashes[kLifeHistory];
static uint8_t _lifeHashIndex{0};
static uint8_t _lifeStuck{0};
static uint16_t _lifeGeneration{0};
static uint32_t _lifeSteppedAtMs{0};
static uint8_t _lifeRows{kLifeSize}; // wysokosc planszy (mniej pod paskiem Automatu)

static void seedLife() {
  for (uint8_t y = 0; y < kLifeSize; y++) {
    for (uint8_t x = 0; x < kLifeSize; x++) {
      _lifeAge[y][x] = random(0, 100) < kLifeFillPercent ? 1 : 0;
      _lifeFade[y][x] = 0;
    }
  }
  for (uint32_t& hash : _lifeHashes) {
    hash = 0;
  }
  _lifeStuck = 0;
  _lifeGeneration = 0;
}

void startLifeMode() {
  seedLife();
  _lifeSteppedAtMs = millis();
}

static void stepLife() {
  static uint8_t next[kLifeSize][kLifeSize];
  uint32_t hash = 2166136261UL; // FNV-1a po ukladzie zywych komorek
  uint16_t population = 0;
  for (uint8_t y = 0; y < _lifeRows; y++) {
    for (uint8_t x = 0; x < kLifeSize; x++) {
      uint8_t neighbours = 0;
      for (int8_t dy = -1; dy <= 1; dy++) {
        for (int8_t dx = -1; dx <= 1; dx++) {
          if ((dx != 0 || dy != 0)
            && _lifeAge[(y + dy + _lifeRows) % _lifeRows][(x + dx + kLifeSize) % kLifeSize]) {
            neighbours++;
          }
        }
      }
      uint8_t age = _lifeAge[y][x];
      bool alive = age ? (neighbours == 2 || neighbours == 3) : neighbours == 3;
      next[y][x] = alive ? (age < 255 ? age + 1 : 255) : 0;
      if (age && !alive) {
        _lifeFade[y][x] = kLifeFadeSteps;
      } else if (_lifeFade[y][x] > 0) {
        _lifeFade[y][x]--;
      }
      if (alive) {
        population++;
        hash = (hash ^ (y * kLifeSize + x)) * 16777619UL;
      }
    }
  }
  memcpy(_lifeAge, next, sizeof(_lifeAge));
  _lifeGeneration++;

  bool repeated = false;
  for (uint32_t previous : _lifeHashes) {
    repeated |= previous == hash;
  }
  _lifeHashes[_lifeHashIndex] = hash;
  _lifeHashIndex = (_lifeHashIndex + 1) % kLifeHistory;
  _lifeStuck = repeated ? _lifeStuck + 1 : 0;
  if (population < 6 || _lifeStuck > kLifeStuckGenerations
    || _lifeGeneration > kLifeMaxGenerations) {
    seedLife();
  }
}

void lifeAnimation(const AnimationParam& param) {
  uint8_t rows = getEffectRows() / kLifeCell;
  if (rows != _lifeRows) {
    _lifeRows = rows; // pasek Automatu zmniejszyl/zwolnil pole - nowa plansza
    seedLife();
  }
  uint32_t now = millis();
  if (now - _lifeSteppedAtMs >= kLifeStepMs) {
    _lifeSteppedAtMs = now;
    stepLife();
  }

  leds.ClearTo(kBlack);
  for (uint8_t y = 0; y < _lifeRows; y++) {
    for (uint8_t x = 0; x < kLifeSize; x++) {
      uint8_t age = _lifeAge[y][x];
      if (age) {
        // Wiek 1 -> turkus, 12 i wiecej -> czerwien; mlode jasniejsze.
        float hue = 0.5f + (age < kLifeAgeForRed ? age : kLifeAgeForRed) * 0.5f / kLifeAgeForRed;
        float lightness = age < 3 ? 1.2f : 0.9f;
        fillClockRect(x * kLifeCell, y * kLifeCell, kLifeCell, kLifeCell,
          HslColor(hue, 1.0f, kLightness * lightness));
      } else if (_lifeFade[y][x] > 0) {
        fillClockRect(x * kLifeCell, y * kLifeCell, kLifeCell, kLifeCell,
          HslColor(0.62f, 1.0f, kLightness * 0.15f * _lifeFade[y][x]));
      }
    }
  }
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(0);
  }
}
