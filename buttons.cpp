// Przyciski UP / DOWN na plytce MatrixPortal S3.
// UP   = nastepny tryb, DOWN = poprzedni tryb.
// Kolejnosc: Zegar -> Losowe -> GIF -> Claude -> Lava -> Gra w zycie ->
// Teraz grane -> Obrazek -> Komputer -> Kursy -> Zegar analogowy -> Labirynt ->
// Automat -> Zegar ...
#include "zegar.h"

static constexpr uint8_t kButtonUpPin{6};
static constexpr uint8_t kButtonDownPin{7};
static constexpr uint32_t kButtonDebounceMs{40};
static constexpr uint8_t kModeCount{13};

struct ModeButton {
  uint8_t pin;
  int8_t direction;
  bool pressed;
  uint32_t changedAtMs;
};

static ModeButton _buttons[] {
  {kButtonUpPin, 1, false, 0},
  {kButtonDownPin, -1, false, 0}
};

void setupButtons() {
  // Przyciski nie maja rezystorow podciagajacych - wcisniecie daje stan niski.
  for (ModeButton& button : _buttons) {
    pinMode(button.pin, INPUT_PULLUP);
  }
}

static void changeModeBy(int8_t direction) {
  uint8_t current = static_cast<uint8_t>(_animationMode);
  uint8_t next = (current + kModeCount + direction) % kModeCount;
  AnimationMode mode = static_cast<AnimationMode>(next);

  selectAnimation(mode);
}

void updateButtons() {
  uint32_t now = millis();
  for (ModeButton& button : _buttons) {
    bool pressed = digitalRead(button.pin) == LOW;
    if (pressed == button.pressed || now - button.changedAtMs < kButtonDebounceMs) {
      continue;
    }
    button.pressed = pressed;
    button.changedAtMs = now;
    if (pressed) {
      changeModeBy(button.direction);
    }
  }
}
