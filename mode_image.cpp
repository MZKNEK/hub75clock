// Tryb Obrazek: statyczne obrazki 64x64 wgrane przez strone zegara.
// Przegladarka przycina obrazek do kwadratu, zmniejsza do 64x64 i wysyla
// POST /image (RGB jako hex, 24576 znakow) - patrz web_control.cpp.
// Pliki RGB (64*64*3 = 12288 B) leza na partycji FFat: /obrazy/0.rgb, 1.rgb, ...
// (zawsze kolejne numery - usuniecie przesuwa nastepne w dol), wiec przetrwaja
// restart i wgranie nowego programu. Wybor zapamietany w Preferences "image".
// Kolory jak w GIF-ach (toPanelColor z gif_animation.cpp).
#include "zegar.h"
#include <Preferences.h>
#include <FFat.h>

static constexpr size_t kImageRowBytes{kColumns * 3};

static bool _imagesReady{false};
static uint8_t _imageCount{0};
static uint8_t _imageSelection{0};
// Przeliczone kolory obrazka (12 KB) - tylko w trybie Obrazek (stopImageMode zwalnia).
static RgbColor* _imageFrame{nullptr};
static int16_t _imageShown{-1};          // ktory obrazek jest w _imageFrame
static float _imageShownLightness{-1.0f};

static String getImagePath(uint8_t index) {
  return String("/obrazy/") + index + ".rgb";
}

// "a7" -> 0xA7. Uzywa tez mode_music.cpp (okladka).
bool decodeHexByte(char high, char low, uint8_t* value) {
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int h = nibble(high);
  int l = nibble(low);
  if (h < 0 || l < 0) {
    return false;
  }
  *value = static_cast<uint8_t>(h * 16 + l);
  return true;
}

void setupImages() {
  // Przy pierwszym uruchomieniu partycja jest pusta - begin(true) ja formatuje.
  _imagesReady = FFat.begin(true);
  if (!_imagesReady) {
    return;
  }
  if (!FFat.exists("/obrazy")) {
    FFat.mkdir("/obrazy");
  }
  _imageCount = 0;
  while (_imageCount < kImageMax && FFat.exists(getImagePath(_imageCount))) {
    _imageCount++;
  }
  Preferences preferences;
  if (preferences.begin("image", true)) {
    _imageSelection = preferences.getUChar("sel", 0);
    preferences.end();
  }
  if (_imageSelection >= _imageCount) {
    _imageSelection = 0;
  }
}

uint8_t getImageCount() {
  return _imageCount;
}

int8_t getImageSelection() {
  return static_cast<int8_t>(_imageSelection);
}

// Wybor obrazka (strona www) - zapamietany i od razu pokazany.
void setImageSelection(int8_t selection) {
  if (selection < 0 || selection >= _imageCount) {
    return;
  }
  _imageSelection = static_cast<uint8_t>(selection);
  Preferences preferences;
  if (preferences.begin("image", false)) {
    preferences.putUChar("sel", _imageSelection);
    preferences.end();
  }
  if (_animationMode != AnimationMode::Image) {
    selectAnimation(AnimationMode::Image);
  }
}

// Nowy obrazek z hex (POST /image) - zapisany jako kolejny numer i wybrany.
bool addImage(const String& hex) {
  if (!_imagesReady || _imageCount >= kImageMax || hex.length() != kImageBytes * 2) {
    return false;
  }
  String path = getImagePath(_imageCount);
  File file = FFat.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }
  const char* text = hex.c_str();
  uint8_t row[kImageRowBytes];
  bool ok = true;
  for (size_t offset = 0; offset < kImageBytes && ok; offset += kImageRowBytes) {
    for (size_t i = 0; i < kImageRowBytes; i++) {
      size_t at = (offset + i) * 2;
      if (!decodeHexByte(text[at], text[at + 1], &row[i])) {
        ok = false;
        break;
      }
    }
    ok = ok && file.write(row, kImageRowBytes) == kImageRowBytes;
  }
  file.close();
  if (!ok) {
    FFat.remove(path);
    return false;
  }
  _imageCount++;
  _imageShown = -1;
  setImageSelection(static_cast<int8_t>(_imageCount - 1));
  return true;
}

bool deleteImage(uint8_t index) {
  if (!_imagesReady || index >= _imageCount) {
    return false;
  }
  FFat.remove(getImagePath(index));
  for (uint8_t next = index + 1; next < _imageCount; next++) {
    FFat.rename(getImagePath(next), getImagePath(next - 1));
  }
  _imageCount--;
  if (_imageSelection > index || _imageSelection >= _imageCount) {
    _imageSelection = _imageSelection > 0 ? _imageSelection - 1 : 0;
  }
  _imageShown = -1;
  Preferences preferences;
  if (preferences.begin("image", false)) {
    preferences.putUChar("sel", _imageSelection);
    preferences.end();
  }
  return true;
}

static bool loadImageFrame(uint8_t index) {
  if (_imageFrame == nullptr) {
    _imageFrame = static_cast<RgbColor*>(malloc(kPixelCount * sizeof(RgbColor)));
    if (_imageFrame == nullptr) {
      return false;
    }
  }
  File file = FFat.open(getImagePath(index), FILE_READ);
  if (!file) {
    return false;
  }
  float level = getPictureLevel();
  uint8_t row[kImageRowBytes];
  for (uint16_t y = 0; y < kRows; y++) {
    if (file.read(row, kImageRowBytes) != kImageRowBytes) {
      file.close();
      return false;
    }
    for (uint16_t x = 0; x < kColumns; x++) {
      const uint8_t* rgb = row + x * 3;
      _imageFrame[y * kColumns + x] = toPanelColor(rgb[0], rgb[1], rgb[2], level);
    }
  }
  file.close();
  return true;
}

// Automat (mode_show.cpp) wybiera obrazek bez zapisu; -1 = koniec.
static int8_t _imageShowOverride{-1};

void setImageShowOverride(int8_t selection) {
  _imageShowOverride = selection;
}

static uint8_t getActiveImageSelection() {
  return _imageShowOverride >= 0 && _imageShowOverride < _imageCount
    ? static_cast<uint8_t>(_imageShowOverride) : _imageSelection;
}

void startImageMode() {
  _imageShown = -1; // przeladuj z pliku (mogl sie zmienic)
}

// Wyjscie z trybu (selectAnimation) - bufor obrazka nie jest potrzebny.
void stopImageMode() {
  free(_imageFrame);
  _imageFrame = nullptr;
  _imageShown = -1;
}

static RgbColor sampleImagePixel(uint16_t pixel) {
  return _imageFrame[pixel];
}

void imageAnimation(const AnimationParam& param) {
  leds.ClearTo(kBlack);
  if (_imageCount == 0) {
    RgbColor color = HslColor(0.58f, 0.5f, kLightness * 0.7f);
    drawSmallText(_imagesReady ? "BRAK" : "BLAD", 24, 22, 1, color);
    drawSmallText("OBRAZKOW", 16, 30, 1, color);
  } else {
    uint8_t selection = getActiveImageSelection();
    if (_imageShown != selection || _imageShownLightness != kLightness) {
      _imageShown = loadImageFrame(selection) ? selection : -2;
      _imageShownLightness = kLightness;
    }
    if (_imageShown == selection && _imageFrame != nullptr) {
      drawPicture(sampleImagePixel);
    }
  }
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(0);
  }
}
