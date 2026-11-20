// Zamiennik tych czesci biblioteki NeoPixelBus, ktorych uzywa zegar:
// RgbColor, HslColor (z ta sama konwersja HSL -> RGB) i NeoPixelAnimator.
// Dzieki temu nie trzeba kompilowac calej NeoPixelBus, ktora na ESP32-S3
// z nowszym pakietem ESP32 nie chce sie kompilowac.
#pragma once

#include <Arduino.h>
#include <functional>

// Panel HUB75 swieci slabiej niz WS2812 przy tych samych wartosciach, wiec
// kolory z HSL sa wzmacniane. Wzmocnienie jest liczone na liczbach
// zmiennoprzecinkowych PRZED zaokragleniem do 0-255 - inaczej ciemne kolory
// (np. fiolety) mialyby tylko kilka mozliwych odcieni.
static constexpr float kHslOutputGain{8.0f};

struct HslColor {
  float H{0.0f};
  float S{0.0f};
  float L{0.0f};

  HslColor() = default;
  HslColor(float hue, float saturation, float lightness)
    : H(hue), S(saturation), L(lightness) {}
};

struct RgbColor {
  uint8_t R{0};
  uint8_t G{0};
  uint8_t B{0};

  RgbColor() = default;
  RgbColor(uint8_t red, uint8_t green, uint8_t blue)
    : R(red), G(green), B(blue) {}
  explicit RgbColor(uint8_t brightness)
    : R(brightness), G(brightness), B(brightness) {}

  // Ta sama konwersja co w NeoPixelBus, wiec kolory wygladaja tak jak wczesniej.
  RgbColor(const HslColor& color) {
    float red;
    float green;
    float blue;
    float hue = color.H;
    float saturation = color.S;
    float lightness = color.L;

    if (saturation == 0.0f || lightness == 0.0f) {
      red = green = blue = lightness;
    } else {
      float q = lightness < 0.5f
        ? lightness * (1.0f + saturation)
        : lightness + saturation - (lightness * saturation);
      float p = 2.0f * lightness - q;
      red = calcColor(p, q, hue + 1.0f / 3.0f);
      green = calcColor(p, q, hue);
      blue = calcColor(p, q, hue - 1.0f / 3.0f);
    }

    R = toChannel(red);
    G = toChannel(green);
    B = toChannel(blue);
  }

 private:
  static float calcColor(float p, float q, float t) {
    if (t < 0.0f) {
      t += 1.0f;
    }
    if (t > 1.0f) {
      t -= 1.0f;
    }
    if (t < 1.0f / 6.0f) {
      return p + (q - p) * 6.0f * t;
    }
    if (t < 0.5f) {
      return q;
    }
    if (t < 2.0f / 3.0f) {
      return p + ((q - p) * (2.0f / 3.0f - t) * 6.0f);
    }
    return p;
  }

  static uint8_t toChannel(float value) {
    if (value <= 0.0f) {
      return 0;
    }
    float scaled = value * 255.0f * kHslOutputGain + 0.5f;
    if (scaled >= 255.0f) {
      return 255;
    }
    return static_cast<uint8_t>(scaled);
  }
};

enum AnimationState {
  AnimationState_Started,
  AnimationState_Progress,
  AnimationState_Completed
};

struct AnimationParam {
  float progress;
  uint16_t index;
  AnimationState state;
};

typedef std::function<void(const AnimationParam& param)> AnimUpdateCallback;

// Zachowuje sie tak samo jak NeoPixelAnimator z NeoPixelBus (skala: milisekundy).
class NeoPixelAnimator {
 public:
  explicit NeoPixelAnimator(uint16_t countAnimations)
    : _count(countAnimations),
      _animations(new Context[countAnimations]) {}

  ~NeoPixelAnimator() {
    delete[] _animations;
  }

  void StartAnimation(uint16_t index, uint16_t duration, AnimUpdateCallback callback) {
    if (index >= _count) {
      return;
    }
    if (_active == 0) {
      _lastTick = millis();
    }
    StopAnimation(index);
    if (duration == 0) {
      duration = 1;
    }
    _active++;
    _animations[index].duration = duration;
    _animations[index].remaining = duration;
    _animations[index].callback = callback;
  }

  void StopAnimation(uint16_t index) {
    if (index >= _count) {
      return;
    }
    if (_animations[index].remaining > 0) {
      _active--;
      _animations[index].remaining = 0;
    }
  }

  void RestartAnimation(uint16_t index) {
    if (index >= _count || _animations[index].duration == 0) {
      return;
    }
    StartAnimation(index, _animations[index].duration, _animations[index].callback);
  }

  bool IsAnimating() const {
    return _active > 0;
  }

  void UpdateAnimations() {
    if (_active == 0) {
      return;
    }
    uint32_t now = millis();
    uint32_t delta = now - _lastTick;
    if (delta == 0) {
      return;
    }

    for (uint16_t index = 0; index < _count; index++) {
      Context& animation = _animations[index];
      AnimationParam param;
      param.index = index;
      if (animation.remaining > delta) {
        param.state = animation.remaining == animation.duration
          ? AnimationState_Started
          : AnimationState_Progress;
        param.progress = static_cast<float>(animation.duration - animation.remaining)
          / animation.duration;
        animation.callback(param);
        animation.remaining -= delta;
      } else if (animation.remaining > 0) {
        param.state = AnimationState_Completed;
        param.progress = 1.0f;
        _active--;
        animation.remaining = 0;
        animation.callback(param);
      }
    }
    _lastTick = now;
  }

 private:
  struct Context {
    uint16_t duration{0};
    uint32_t remaining{0};
    AnimUpdateCallback callback{};
  };

  uint16_t _count;
  Context* _animations;
  uint16_t _active{0};
  uint32_t _lastTick{0};
};
