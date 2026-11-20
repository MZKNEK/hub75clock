// Wiadomosci na zegarze: pasek z tekstem pod godzina (y 18-41), nad kazdym trybem.
//   GET /msg?t=PRANIE%20GOTOWE        (tekst; pusty t = schowaj wiadomosc)
//       &s=20                         (opcjonalnie: ile sekund, 1-600)
//       &c=200                        (opcjonalnie: kolor, odcien 0-360)
// Dlugi tekst przewija sie od prawej do lewej, krotki stoi na srodku.
// Male litery sa zamieniane na wielkie, polskie rysowane z ogonkami i kreskami.
#include "zegar.h"

static constexpr int16_t kMessageTop{18};
static constexpr int16_t kMessageHeight{24}; // zaslania cala pogode (y 18-41)
static constexpr uint8_t kMessageScale{2};
static constexpr int16_t kMessageTextY{kMessageTop + 7};
static constexpr float kMessageScrollPxPerSecond{24.0f};
static constexpr uint32_t kMessageStaticMs{8000UL};
static constexpr uint8_t kMessagePasses{2}; // ile razy przewinac dlugi tekst

static char _messageText[96]{};
static uint32_t _messageStartedAtMs{0};
static uint32_t _messageDurationMs{0};
static float _messageHue{0.13f};

static int16_t getMessageWidth() {
  int16_t width = 0;
  for (const char* c = _messageText; *c; c++) {
    width += getSmallCharAdvance(*c, kMessageScale);
  }
  return width > 0 ? width - kMessageScale : 0;
}

// Polska litera z UTF-8 (2 bajty) -> kod 0x80-0x88 z kPolishFont (clock.cpp),
// male i wielkie tak samo; 0 = nieznany znak.
static char foldPolishLetter(uint8_t first, uint8_t second) {
  uint8_t code = 0;
  if (first == 0xC3 && (second == 0xB3 || second == 0x93)) code = 0x85;       // o
  if (first == 0xC4) {
    switch (second) {
      case 0x84: case 0x85: code = 0x80; break;                               // a
      case 0x86: case 0x87: code = 0x81; break;                               // c
      case 0x98: case 0x99: code = 0x82; break;                               // e
    }
  }
  if (first == 0xC5) {
    switch (second) {
      case 0x81: case 0x82: code = 0x83; break;                               // l
      case 0x83: case 0x84: code = 0x84; break;                               // n
      case 0x9A: case 0x9B: code = 0x86; break;                               // s
      case 0xB9: case 0xBA: code = 0x87; break;                               // z z kreska
      case 0xBB: case 0xBC: code = 0x88; break;                               // z z kropka
    }
  }
  return static_cast<char>(code);
}

// Tekst UTF-8 -> znaki czcionki zegara: wielkie litery, polskie jako kody
// 0x80-0x88, przecinek jako kropka, nieznane znaki jako spacja.
// Uzywa tez ekran "Teraz grane" (mode_music.cpp).
void foldTextForDisplay(const String& text, char* out, size_t size) {
  size_t length = 0;
  for (size_t i = 0; i < text.length() && length < size - 1; i++) {
    uint8_t c = static_cast<uint8_t>(text[i]);
    char character;
    if (c >= 0x80) {
      character = i + 1 < text.length()
        ? foldPolishLetter(c, static_cast<uint8_t>(text[i + 1])) : 0;
      // Pomijamy bajty kontynuacji znaku UTF-8.
      while (i + 1 < text.length() && (static_cast<uint8_t>(text[i + 1]) & 0xC0) == 0x80) {
        i++;
      }
      if (character == 0) {
        character = ' ';
      }
    } else {
      character = static_cast<char>(toupper(c));
      if (character == ',') {
        character = '.';
      }
    }
    out[length++] = character;
  }
  out[length] = '\0';
}

// seconds = 0 -> czas dobrany do dlugosci tekstu.
void showMessage(const String& text, uint32_t seconds, float hue) {
  foldTextForDisplay(text, _messageText, sizeof(_messageText));
  int16_t width = getMessageWidth();
  if (seconds > 0) {
    _messageDurationMs = seconds * 1000UL;
  } else if (width <= kColumns - 4) {
    _messageDurationMs = kMessageStaticMs;
  } else {
    _messageDurationMs = static_cast<uint32_t>(
      (width + kColumns) * kMessagePasses * 1000.0f / kMessageScrollPxPerSecond);
  }
  _messageHue = hue;
  _messageStartedAtMs = millis();
}

void hideMessage() {
  _messageText[0] = '\0';
}

bool isMessageActive() {
  return _messageText[0] != '\0' && millis() - _messageStartedAtMs < _messageDurationMs;
}

void drawMessageOverlay() {
  if (!isMessageActive()) {
    return;
  }
  uint32_t elapsed = millis() - _messageStartedAtMs;
  fillClockRect(0, kMessageTop, kColumns, kMessageHeight, kBlack);

  // Ramka: na poczatku miga 3 razy, zeby zwrocic uwage.
  if (elapsed >= 1500UL || (elapsed / 250UL) % 2 == 0) {
    RgbColor frame = HslColor(_messageHue, 1.0f, kLightness * 0.6f);
    fillClockRect(0, kMessageTop, kColumns, 1, frame);
    fillClockRect(0, kMessageTop + kMessageHeight - 1, kColumns, 1, frame);
  }

  int16_t width = getMessageWidth();
  int16_t x;
  if (width <= kColumns - 4) {
    x = (kColumns - width) / 2;
  } else {
    int16_t span = width + kColumns;
    x = kColumns - static_cast<int16_t>(
      static_cast<uint32_t>(elapsed * kMessageScrollPxPerSecond / 1000.0f) % span);
  }
  drawSmallText(_messageText, x, kMessageTextY, kMessageScale,
    HslColor(_messageHue, 1.0f, kLightness));
}
