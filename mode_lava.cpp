// Tryb "Lava": lampa lava - kilka kul (metaballe) powoli plynie w gore i w dol,
// laczac sie i rozdzielajac. Srodek kul jasniejszy i cieplejszy, a kolor calej
// lampy bardzo powoli przechodzi przez wszystkie barwy (pelny obrot ~10 min).
#include "zegar.h"

static constexpr uint8_t kLavaBlobCount{5};
static constexpr float kLavaHueCycleSeconds{600.0f};

// promien, srodek X, wychylenie X, okres X (s), okres Y (s), faza
static const float kLavaBlobs[kLavaBlobCount][6] {
  {9.0f, 20.0f, 10.0f, 23.0f, 31.0f, 0.0f},
  {7.0f, 42.0f, 12.0f, 29.0f, 41.0f, 1.7f},
  {6.0f, 32.0f, 18.0f, 37.0f, 26.0f, 3.1f},
  {8.0f, 26.0f, 14.0f, 19.0f, 47.0f, 4.4f},
  {5.0f, 46.0f, 9.0f, 27.0f, 35.0f, 5.6f},
};

void lavaAnimation(const AnimationParam& param) {
  int16_t rows = getEffectRows(); // mniej, gdy pasek Automatu zmniejsza obraz
  float half = rows / 2.0f;
  float seconds = millis() / 1000.0f;
  float blobX[kLavaBlobCount];
  float blobY[kLavaBlobCount];
  float radius2[kLavaBlobCount];
  for (uint8_t blob = 0; blob < kLavaBlobCount; blob++) {
    const float* b = kLavaBlobs[blob];
    blobX[blob] = b[1] + b[2] * sinf(seconds * 2.0f * PI / b[3] + b[5]);
    // Pelna wysokosc lampy: kule wychodza troche poza gore i dol ekranu.
    blobY[blob] = half + (half - 2.0f) * sinf(seconds * 2.0f * PI / b[4] + b[5] * 1.7f);
    radius2[blob] = b[0] * b[0];
  }
  float baseHue = fmodf(seconds / kLavaHueCycleSeconds, 1.0f);

  leds.ClearTo(kBlack);
  for (int16_t y = 0; y < rows; y++) {
    for (int16_t x = 0; x < kColumns; x++) {
      float field = 0.0f;
      for (uint8_t blob = 0; blob < kLavaBlobCount; blob++) {
        float dx = x - blobX[blob];
        float dy = y - blobY[blob];
        field += radius2[blob] / (dx * dx + dy * dy + 1.0f);
      }
      if (field < 0.85f) {
        continue;
      }
      // Brzeg (0,85-1) ciemniejszy, w glebi kuli jasniej i cieplej.
      float depth = constrain((field - 1.0f) / 2.5f, 0.0f, 1.0f);
      float lightness = field < 1.0f ? 0.45f : 0.75f + 0.55f * depth;
      setDashPixel(x, y, HslColor(baseHue + 0.07f * depth, 1.0f, kLightness * lightness));
    }
  }
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(0);
  }
}
