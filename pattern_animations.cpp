// Losowe piksele (Tecza usunieta 2026-10-27, na zyczenie).
#include "zegar.h"

// Losowe "duze piksele": siatka 16x16 komorek po 4x4 px.
static constexpr uint8_t kRandomCellSize{4};
static constexpr uint8_t kRandomGridSize{kColumns / kRandomCellSize};
static constexpr uint16_t kRandomCellCount{kRandomGridSize * kRandomGridSize};
static constexpr uint8_t kRandomPixelMinChanges{3};
static constexpr uint8_t kRandomPixelMaxChanges{10};
static constexpr uint8_t kRandomOffChancePercent{25};
static uint16_t _randomPixels[kRandomPixelMaxChanges] {};
static float _randomTargetHues[kRandomPixelMaxChanges] {};
static float _randomTargetLightness[kRandomPixelMaxChanges] {};
static uint8_t _randomPixelCount{0};
static float _randomHues[kRandomCellCount] {};
static float _randomLightness[kRandomCellCount] {};

// Losowe "duze piksele": komorki 4x4, swieci 3x3 (szczelina daje efekt LED).
static void drawRandomCell(uint16_t cell, const RgbColor& color) {
  uint8_t cellRow = cell / kRandomGridSize;
  uint8_t cellColumn = cell % kRandomGridSize;
  if ((cellRow + 1) * kRandomCellSize > getEffectRows()) {
    return; // pod paskiem Automatu (zmniejszony obraz)
  }
  for (uint8_t row = 0; row < kRandomCellSize - 1; row++) {
    for (uint8_t column = 0; column < kRandomCellSize - 1; column++) {
      leds.SetPixelColor(pixelIndex(cellRow * kRandomCellSize + row,
        cellColumn * kRandomCellSize + column), color);
    }
  }
}

void randomAnimation(const AnimationParam& param) {
  if (param.state == AnimationState_Started) {
    _randomPixelCount = random(kRandomPixelMinChanges, kRandomPixelMaxChanges + 1);

    for (uint8_t selected = 0; selected < _randomPixelCount; selected++) {
      uint16_t cell;
      bool alreadySelected;
      do {
        cell = random(0, kRandomCellCount);
        alreadySelected = false;
        for (uint8_t previous = 0; previous < selected; previous++) {
          if (_randomPixels[previous] == cell) {
            alreadySelected = true;
            break;
          }
        }
      } while (alreadySelected);

      _randomPixels[selected] = cell;
      _randomTargetHues[selected] = random(0, 1000) / 1000.0f;
      _randomTargetLightness[selected] = random(0, 100) < kRandomOffChancePercent
        ? 0.0f
        : kLightness;
    }
  }

  leds.ClearTo(kBlack);
  for (uint16_t cell = 0; cell < kRandomCellCount; cell++) {
    if (_randomLightness[cell] > 0.0f) {
      drawRandomCell(cell,
        HslColor(_randomHues[cell], 1.0f, _randomLightness[cell]));
    }
  }

  for (uint8_t selected = 0; selected < _randomPixelCount; selected++) {
    uint16_t cell = _randomPixels[selected];
    float startHue = _randomHues[cell];
    float hueDelta = _randomTargetHues[selected] - startHue;
    if (hueDelta > 0.5f) {
      hueDelta -= 1.0f;
    } else if (hueDelta < -0.5f) {
      hueDelta += 1.0f;
    }

    float hue = startHue + hueDelta * param.progress;
    if (hue < 0.0f) {
      hue += 1.0f;
    } else if (hue >= 1.0f) {
      hue -= 1.0f;
    }
    float lightness = _randomLightness[cell]
      + (_randomTargetLightness[selected] - _randomLightness[cell]) * param.progress;
    drawRandomCell(cell, HslColor(hue, 1.0f, lightness));
  }

  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    for (uint8_t selected = 0; selected < _randomPixelCount; selected++) {
      uint16_t cell = _randomPixels[selected];
      _randomHues[cell] = _randomTargetHues[selected];
      _randomLightness[cell] = _randomTargetLightness[selected];
    }

    animations.RestartAnimation(0);
  }
}
