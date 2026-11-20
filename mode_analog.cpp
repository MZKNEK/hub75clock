// Tryb "Zegar analogowy": okragla tarcza z kreskami godzin i minut, wskazowki
// godzinowa (bursztynowa), minutowa (turkusowa) i plynny sekundnik (czerwony).
// Linie wygladzane (Xiaolin Wu) - na 64x64 wskazowki nie sa schodkowe.
#include "zegar.h"

static constexpr float kAnalogCenter{31.5f};
static constexpr float kAnalogRadius{30.0f};

// Dodaje kolor * alpha do piksela (wygladzanie i nakladanie sie wskazowek).
static void addAnalogPixel(int16_t x, int16_t y, const RgbColor& color, float alpha) {
  if (x < 0 || x >= kColumns || y < 0 || y >= kRows || alpha <= 0.0f) {
    return;
  }
  RgbColor old = leds.GetPixelColor(pixelIndex(y, x));
  leds.SetPixelColor(pixelIndex(y, x), RgbColor(
    static_cast<uint8_t>(min(255.0f, old.R + color.R * alpha)),
    static_cast<uint8_t>(min(255.0f, old.G + color.G * alpha)),
    static_cast<uint8_t>(min(255.0f, old.B + color.B * alpha))));
}

// Punkt o ulamkowych wspolrzednych rozlozony na 4 piksele.
static void addAnalogPoint(float x, float y, const RgbColor& color, float alpha) {
  int16_t x0 = static_cast<int16_t>(floorf(x));
  int16_t y0 = static_cast<int16_t>(floorf(y));
  float fx = x - x0;
  float fy = y - y0;
  addAnalogPixel(x0, y0, color, alpha * (1 - fx) * (1 - fy));
  addAnalogPixel(x0 + 1, y0, color, alpha * fx * (1 - fy));
  addAnalogPixel(x0, y0 + 1, color, alpha * (1 - fx) * fy);
  addAnalogPixel(x0 + 1, y0 + 1, color, alpha * fx * fy);
}

// Wygladzona linia od srodka pod katem (0 = 12:00, zgodnie z zegarem).
static void drawAnalogHand(float angle, float from, float length, float width,
  const RgbColor& color) {
  float dx = sinf(angle);
  float dy = -cosf(angle);
  for (float t = from; t <= length; t += 0.5f) {
    float x = kAnalogCenter + dx * t;
    float y = kAnalogCenter + dy * t;
    addAnalogPoint(x, y, color, 0.6f);
    if (width > 1.0f) {
      // Grubsza wskazowka: dwie dodatkowe linie po bokach.
      addAnalogPoint(x - dy * 0.7f, y + dx * 0.7f, color, 0.35f);
      addAnalogPoint(x + dy * 0.7f, y - dx * 0.7f, color, 0.35f);
    }
  }
}

void analogAnimation(const AnimationParam& param) {
  leds.ClearTo(kBlack);
  // Kreski minut (przygaszone) i godzin (12, 3, 6, 9 najjasniejsze).
  for (uint8_t mark = 0; mark < 60; mark++) {
    float angle = mark * 2.0f * PI / 60.0f;
    bool hourMark = mark % 5 == 0;
    bool quarter = mark % 15 == 0;
    RgbColor color = HslColor(0.58f, 0.6f,
      kLightness * (quarter ? 1.1f : (hourMark ? 0.7f : 0.22f)));
    float inner = quarter ? kAnalogRadius - 4.0f : (hourMark ? kAnalogRadius - 2.5f : kAnalogRadius - 0.5f);
    for (float r = inner; r <= kAnalogRadius; r += 0.5f) {
      addAnalogPoint(kAnalogCenter + sinf(angle) * r, kAnalogCenter - cosf(angle) * r, color, 0.7f);
    }
  }

  struct timeval now;
  struct tm timeInfo;
  if (_hasValidTime && gettimeofday(&now, nullptr) == 0 && readLocalTime(&timeInfo)) {
    float seconds = timeInfo.tm_sec + now.tv_usec / 1000000.0f;
    float minutes = timeInfo.tm_min + seconds / 60.0f;
    float hours = (timeInfo.tm_hour % 12) + minutes / 60.0f;
    drawAnalogHand(hours * 2.0f * PI / 12.0f, 0.0f, 15.0f, 2.0f,
      HslColor(0.1f, 1.0f, kLightness * 1.3f));
    drawAnalogHand(minutes * 2.0f * PI / 60.0f, 0.0f, 23.0f, 2.0f,
      HslColor(0.5f, 1.0f, kLightness * 1.2f));
    drawAnalogHand(seconds * 2.0f * PI / 60.0f, -5.0f, 26.0f, 1.0f,
      HslColor(0.0f, 1.0f, kLightness * 1.3f));
    addAnalogPoint(kAnalogCenter, kAnalogCenter, HslColor(0.0f, 1.0f, kLightness * 1.4f), 1.0f);
  }
  drawNetworkStatus();
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(0);
  }
}
