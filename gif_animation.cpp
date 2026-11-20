// Tryb GIF: animacje z partycji FFat - /gify/0.g64, 1.g64, ... (zawsze kolejne
// numery, usuniecie przesuwa nastepne w dol). Wgrywane ze strony zegara:
// przegladarka rozklada GIF na klatki 64x64 ze wspolna paleta i wysyla gotowy
// plik .g64 (POST /gifup, zapis strumieniem - patrz web_control.cpp).
// Wybor (strona www): konkretny GIF albo "losowo" - wtedy co ok.
// kGifClipDurationMs losowany jest inny GIF. Wybor jest zapamietywany (numer).
//
// Plik .g64 (little endian):
//   0   "G64" + wersja (1)
//   4   uint16 liczba klatek (1-kGifMaxFrames)
//   6   uint16 rozmiar palety (1-256)
//   8   nazwa, 32 B UTF-8 (zera na koncu)
//   40  uint16 czas klatki w ms x liczba klatek
//   ..  paleta RGB, 3 B x rozmiar palety
//   ..  klatki: 64x64 B indeksow palety (4096 B na klatke)
// W trybie GIF otwarty plik + bufor klatki 4 KB + czasy klatek (stopGifMode zwalnia).
#include "zegar.h"
#include <Preferences.h>
#include <FFat.h>

static constexpr uint32_t kGifClipDurationMs{20000UL};
static constexpr uint16_t kGifFramePixels{kPixelCount};
static constexpr uint16_t kGifMaxFrames{600};
static constexpr size_t kGifNameBytes{32};
static constexpr size_t kGifHeaderBytes{8 + kGifNameBytes};
// Gamma: kolory GIF-ow sa "dla oka" (sRGB), diody swieca liniowo.
static constexpr float kGifGamma{2.0f};
// Jasnosc bieli z GIF-a przy domyslnej jasnosci (skala wyjscia 0-255).
static constexpr float kGifWhiteLevel{90.0f};
// Prawie czarne (ponizej 3/255) = czern - szum kompresji w ciemnych tlach nie swieci.
static constexpr float kGifBlackThreshold{3.0f};

static constexpr int8_t kGifRandom{-1};
static int8_t _gifSelection{kGifRandom};
// Automat (mode_show.cpp) wybiera GIF bez zapisu - reczny wybor zostaje.
static constexpr int8_t kGifNoOverride{-2};
static int8_t _gifShowOverride{kGifNoOverride};

// Lista GIF-ow na partycji (nazwy do strony).
static uint8_t _gifCount{0};
static char _gifNames[kGifMax][kGifNameBytes];

// Odtwarzany GIF.
static File _gifFile;
static int16_t _gifOpenClip{-1};
static uint16_t _gifFrameCount{0};
static uint16_t* _gifDelays{nullptr};
static uint8_t* _gifFrameData{nullptr};
static int32_t _gifLoadedFrame{-1};
static uint32_t _gifFramesOffset{0};
static uint8_t _gifRawPalette[256 * 3];
static uint16_t _gifPaletteSize{0};

static uint8_t _gifClip{0};
static uint16_t _gifFrame{0};
static uint32_t _gifFrameStartedAtMs{0};
static uint32_t _gifClipStartedAtMs{0};
static RgbColor _gifPalette[256]{};
static int16_t _gifPaletteClip{-1};
static float _gifPaletteLightness{-1.0f};

// Wgrywanie (POST /gifup): plik tymczasowy, na koncu sprawdzony i przemianowany.
static constexpr char kGifUploadPath[] = "/gify/nowy.part";
static File _gifUploadFile;
static bool _gifUploadOk{false};

static String getGifPath(uint8_t index) {
  return String("/gify/") + index + ".g64";
}

static int8_t getActiveGifSelection() {
  return _gifShowOverride != kGifNoOverride ? _gifShowOverride : _gifSelection;
}

// kGifNoOverride = koniec (Automat zatrzymany).
void setGifShowOverride(int8_t selection) {
  _gifShowOverride = selection == kGifNoOverride || (selection >= 0 && selection < _gifCount)
    ? selection : kGifRandom;
}

static uint8_t gifChannel(uint8_t value, float level) {
  float linear = powf(value / 255.0f, kGifGamma) * level;
  if (linear < kGifBlackThreshold) {
    return 0;
  }
  return linear >= 255.0f ? 255 : static_cast<uint8_t>(linear + 0.5f);
}

// Jasnosc bieli obrazkow (GIF, okladka, Obrazek) przy biezacej jasnosci.
float getPictureLevel() {
  return kGifWhiteLevel * kLightness / kDefaultLightness;
}

// Kolor sRGB z obrazka -> kolor diody: gamma i jasnosc.
// Uzywaja tez mode_image.cpp i mode_music.cpp.
RgbColor toPanelColor(uint8_t red, uint8_t green, uint8_t blue, float level) {
  return RgbColor(gifChannel(red, level), gifChannel(green, level), gifChannel(blue, level));
}

// Obraz 64x64 na ekran: caly albo - gdy pasek Automatu zmniejsza obraz -
// zmniejszony proporcjonalnie do rows x rows (dwuliniowo), wysrodkowany
// w poziomie. Uzywa tez mode_image.cpp.
void drawPicture(PictureSampler sample) {
  int16_t rows = getEffectRows();
  if (rows >= kRows) {
    for (uint16_t pixel = 0; pixel < kPixelCount; pixel++) {
      leds.SetPixelColor(pixel, sample(pixel));
    }
    return;
  }
  int16_t left = (kColumns - rows) / 2;
  float scale = static_cast<float>(kColumns) / rows;
  for (int16_t y = 0; y < rows; y++) {
    float sy = constrain((y + 0.5f) * scale - 0.5f, 0.0f, kRows - 1.0f);
    int16_t y0 = static_cast<int16_t>(sy);
    int16_t y1 = min<int16_t>(y0 + 1, kRows - 1);
    float fy = sy - y0;
    for (int16_t x = 0; x < rows; x++) {
      float sx = constrain((x + 0.5f) * scale - 0.5f, 0.0f, kColumns - 1.0f);
      int16_t x0 = static_cast<int16_t>(sx);
      int16_t x1 = min<int16_t>(x0 + 1, kColumns - 1);
      float fx = sx - x0;
      RgbColor a = sample(pixelIndex(y0, x0));
      RgbColor b = sample(pixelIndex(y0, x1));
      RgbColor c = sample(pixelIndex(y1, x0));
      RgbColor d = sample(pixelIndex(y1, x1));
      auto mix = [&](uint8_t pa, uint8_t pb, uint8_t pc, uint8_t pd) -> uint8_t {
        float top = pa + (pb - pa) * fx;
        float bottom = pc + (pd - pc) * fx;
        return static_cast<uint8_t>(top + (bottom - top) * fy + 0.5f);
      };
      leds.SetPixelColor(pixelIndex(y, left + x), RgbColor(mix(a.R, b.R, c.R, d.R),
        mix(a.G, b.G, c.G, d.G), mix(a.B, b.B, c.B, d.B)));
    }
  }
}

static RgbColor sampleGifPixel(uint16_t pixel) {
  return _gifPalette[_gifFrameData[pixel]];
}

static uint16_t readGifUint16(const uint8_t* bytes) {
  return bytes[0] | (bytes[1] << 8);
}

// Naglowek pliku: liczba klatek, paleta, nazwa. false = zly plik.
static bool readGifHeader(File& file, uint16_t* frames, uint16_t* paletteSize, char* name) {
  uint8_t header[kGifHeaderBytes];
  if (file.read(header, sizeof(header)) != sizeof(header)
    || memcmp(header, "G64\x01", 4) != 0) {
    return false;
  }
  *frames = readGifUint16(header + 4);
  *paletteSize = readGifUint16(header + 6);
  if (*frames == 0 || *frames > kGifMaxFrames || *paletteSize == 0 || *paletteSize > 256) {
    return false;
  }
  size_t expected = kGifHeaderBytes + *frames * 2UL + *paletteSize * 3UL
    + static_cast<size_t>(*frames) * kGifFramePixels;
  if (file.size() != expected) {
    return false;
  }
  if (name != nullptr) {
    memcpy(name, header + 8, kGifNameBytes);
    name[kGifNameBytes - 1] = '\0';
  }
  return true;
}

// Lista z partycji (po setupImages - ono montuje FFat).
void setupGifs() {
  if (!FFat.exists("/gify")) {
    FFat.mkdir("/gify");
  }
  FFat.remove(kGifUploadPath); // po przerwanym wgrywaniu
  _gifCount = 0;
  while (_gifCount < kGifMax && FFat.exists(getGifPath(_gifCount))) {
    File file = FFat.open(getGifPath(_gifCount), FILE_READ);
    uint16_t frames;
    uint16_t paletteSize;
    bool ok = file && readGifHeader(file, &frames, &paletteSize, _gifNames[_gifCount]);
    file.close();
    if (!ok) {
      break; // uszkodzony - dalsze i tak nie sa osiagalne po numerach
    }
    _gifCount++;
  }
  if (_gifSelection >= _gifCount) {
    _gifSelection = kGifRandom;
  }
}

uint8_t getGifCount() {
  return _gifCount;
}

const char* getGifName(uint8_t index) {
  return index < _gifCount ? _gifNames[index] : "";
}

static void closeGifClip() {
  if (_gifFile) {
    _gifFile.close();
  }
  free(_gifDelays);
  free(_gifFrameData);
  _gifDelays = nullptr;
  _gifFrameData = nullptr;
  _gifOpenClip = -1;
  _gifLoadedFrame = -1;
}

// Wyjscie z trybu (selectAnimation) i przed zmiana plikow na partycji.
void stopGifMode() {
  closeGifClip();
}

static bool openGifClip(uint8_t index) {
  closeGifClip();
  _gifFile = FFat.open(getGifPath(index), FILE_READ);
  if (!_gifFile || !readGifHeader(_gifFile, &_gifFrameCount, &_gifPaletteSize, nullptr)) {
    closeGifClip();
    return false;
  }
  _gifDelays = static_cast<uint16_t*>(malloc(_gifFrameCount * sizeof(uint16_t)));
  _gifFrameData = static_cast<uint8_t*>(malloc(kGifFramePixels));
  if (_gifDelays == nullptr || _gifFrameData == nullptr) {
    closeGifClip();
    return false;
  }
  for (uint16_t frame = 0; frame < _gifFrameCount; frame++) {
    uint8_t bytes[2];
    _gifFile.read(bytes, 2);
    _gifDelays[frame] = max<uint16_t>(20, readGifUint16(bytes));
  }
  _gifFile.read(_gifRawPalette, _gifPaletteSize * 3);
  _gifFramesOffset = kGifHeaderBytes + _gifFrameCount * 2UL + _gifPaletteSize * 3UL;
  _gifOpenClip = index;
  _gifPaletteClip = -1;
  return true;
}

static void buildGifPalette() {
  float level = getPictureLevel();
  for (uint16_t color = 0; color < 256; color++) {
    if (color < _gifPaletteSize) {
      const uint8_t* rgb = _gifRawPalette + color * 3;
      _gifPalette[color] = toPanelColor(rgb[0], rgb[1], rgb[2], level);
    } else {
      _gifPalette[color] = RgbColor(0, 0, 0);
    }
  }
  _gifPaletteClip = _gifOpenClip;
  _gifPaletteLightness = kLightness;
}

static void pickRandomGifClip() {
  if (_gifCount <= 1) {
    _gifClip = 0;
    return;
  }
  uint8_t next;
  do {
    next = random(0, _gifCount);
  } while (next == _gifClip);
  _gifClip = next;
}

void loadGifSelection() {
  Preferences preferences;
  if (preferences.begin("gif", true)) {
    _gifSelection = static_cast<int8_t>(preferences.getChar("sel", kGifRandom));
    preferences.end();
  }
  if (_gifSelection < kGifRandom || _gifSelection >= kGifMax) {
    _gifSelection = kGifRandom;
  }
}

int8_t getGifSelection() {
  return _gifSelection;
}

static void saveGifSelection() {
  Preferences preferences;
  if (preferences.begin("gif", false)) {
    preferences.putChar("sel", _gifSelection);
    preferences.end();
  }
}

// selection: numer GIF-a albo -1 (losowo). Wlacza tez tryb GIF.
void setGifSelection(int8_t selection) {
  if (selection < 0 || selection >= _gifCount) {
    selection = kGifRandom;
  }
  if (selection != _gifSelection) {
    _gifSelection = selection;
    saveGifSelection();
  }
  if (_animationMode == AnimationMode::Gif) {
    startGifMode();
  } else {
    selectAnimation(AnimationMode::Gif);
  }
}

void startGifMode() {
  uint32_t now = millis();
  if (getActiveGifSelection() >= 0 && getActiveGifSelection() < _gifCount) {
    _gifClip = getActiveGifSelection();
  } else {
    pickRandomGifClip();
  }
  _gifFrame = 0;
  _gifFrameStartedAtMs = now;
  _gifClipStartedAtMs = now;
  closeGifClip(); // gifAnimation otworzy wybrany
}

static void advanceGifFrame(uint32_t now) {
  // Po dluzszej przerwie nie nadrabiamy klatek - po prostu gramy dalej.
  if (now - _gifFrameStartedAtMs > 1000UL) {
    _gifFrameStartedAtMs = now;
  }
  while (now - _gifFrameStartedAtMs >= _gifDelays[_gifFrame]) {
    _gifFrameStartedAtMs += _gifDelays[_gifFrame];
    _gifFrame++;
    if (_gifFrame >= _gifFrameCount) {
      _gifFrame = 0;
      if (getActiveGifSelection() == kGifRandom && _gifCount > 1
        && now - _gifClipStartedAtMs >= kGifClipDurationMs) {
        pickRandomGifClip();
        _gifClipStartedAtMs = now;
        _gifFrameStartedAtMs = now;
        return;
      }
    }
  }
}

static bool loadGifFrame(uint16_t frame) {
  if (_gifLoadedFrame == frame) {
    return true;
  }
  if (!_gifFile.seek(_gifFramesOffset + static_cast<uint32_t>(frame) * kGifFramePixels)
    || _gifFile.read(_gifFrameData, kGifFramePixels) != kGifFramePixels) {
    return false;
  }
  _gifLoadedFrame = frame;
  return true;
}

void gifAnimation(const AnimationParam& param) {
  uint32_t now = millis();
  leds.ClearTo(kBlack);
  if (_gifCount == 0) {
    RgbColor color = HslColor(0.58f, 0.5f, kLightness * 0.7f);
    drawSmallText("BRAK", 24, 22, 1, color);
    drawSmallText("GIF" "\x85" "W", 22, 30, 1, color); // GIFOW, O z kreska (kPolishFont)
  } else {
    if (_gifOpenClip != _gifClip) {
      openGifClip(_gifClip);
      _gifFrame = 0;
      _gifFrameStartedAtMs = now;
    }
    if (_gifOpenClip == _gifClip) {
      advanceGifFrame(now);
      if (_gifOpenClip != _gifClip) {
        openGifClip(_gifClip); // losowo: nowy GIF
        _gifFrame = 0;
      }
      if (_gifPaletteClip != _gifOpenClip || _gifPaletteLightness != kLightness) {
        buildGifPalette();
      }
      if (_gifOpenClip == _gifClip && loadGifFrame(_gifFrame)) {
        drawPicture(sampleGifPixel);
      }
    }
  }
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(0);
  }
}

// --- Wgrywanie i usuwanie (web_control.cpp) ---

void beginGifUpload() {
  stopGifMode(); // zadnego otwartego pliku przy zmianach na partycji
  _gifUploadOk = _gifCount < kGifMax;
  if (_gifUploadOk) {
    _gifUploadFile = FFat.open(kGifUploadPath, FILE_WRITE);
    _gifUploadOk = static_cast<bool>(_gifUploadFile);
  }
}

void writeGifUpload(const uint8_t* data, size_t length) {
  if (_gifUploadOk && _gifUploadFile.write(data, length) != length) {
    _gifUploadOk = false; // brak miejsca
  }
}

void abortGifUpload() {
  if (_gifUploadFile) {
    _gifUploadFile.close();
  }
  FFat.remove(kGifUploadPath);
  _gifUploadOk = false;
}

// Koniec wgrywania: sprawdza plik, dopisuje jako kolejny numer i go wybiera.
bool finishGifUpload() {
  if (_gifUploadFile) {
    _gifUploadFile.close();
  }
  if (_gifUploadOk) {
    File file = FFat.open(kGifUploadPath, FILE_READ);
    uint16_t frames;
    uint16_t paletteSize;
    _gifUploadOk = file && readGifHeader(file, &frames, &paletteSize, _gifNames[_gifCount]);
    file.close();
  }
  if (!_gifUploadOk || !FFat.rename(kGifUploadPath, getGifPath(_gifCount))) {
    FFat.remove(kGifUploadPath);
    if (_animationMode == AnimationMode::Gif) {
      startGifMode();
    }
    return false;
  }
  _gifCount++;
  _gifUploadOk = false; // nastepne /gifup bez pliku nie zaliczy sie jako udane
  setGifSelection(static_cast<int8_t>(_gifCount - 1));
  return true;
}

bool deleteGif(uint8_t index) {
  if (index >= _gifCount) {
    return false;
  }
  stopGifMode();
  FFat.remove(getGifPath(index));
  for (uint8_t next = index + 1; next < _gifCount; next++) {
    FFat.rename(getGifPath(next), getGifPath(next - 1));
    memcpy(_gifNames[next - 1], _gifNames[next], kGifNameBytes);
  }
  _gifCount--;
  if (_gifSelection == index) {
    _gifSelection = kGifRandom;
  } else if (_gifSelection > index) {
    _gifSelection--;
  }
  saveGifSelection();
  if (_animationMode == AnimationMode::Gif) {
    startGifMode();
  }
  return true;
}

// Wolne miejsce na partycji (strona: ile klatek jeszcze sie zmiesci).
uint32_t getGifFreeBytes() {
  return FFat.freeBytes();
}
