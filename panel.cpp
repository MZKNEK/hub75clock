// Wyjscie na panel HUB75 64x64 przez ESP32-HUB75-MatrixPanel-DMA.
// Odswiezanie w calosci sprzetowe (LCD_CAM + DMA), 8 bitow koloru. Z Adafruit
// Protomatter (przerwanie na kazdy bitplan) Wi-Fi rozjezdzalo czasy: wedrujace
// pasy i mrugniecia - biblioteka usunieta 2026-10-27.
// build_opt.h: -DNO_CIE1931 (bez wlasnej korekcji jasnosci - nasze kolory sa
// liniowe), -DPIXEL_COLOR_DEPTH_BITS=8 (bez tego wersja 3.1.0 sie nie kompiluje).
// Jeden piksel bufora = jeden piksel panelu.
// Tu tez plynne przejscie miedzy trybami (bufor poprzedniej klatki).
#include "zegar.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Preferences.h>
#include <WiFi.h>

static constexpr int16_t kPanelWidth{64};
static constexpr int16_t kPanelHeight{64};

// Ogranicznik pradu: suma (R+G+B) wszystkich fizycznych pikseli w klatce.
// Caly panel na bialo = 4096 * 765 = ok. 3.1 mln (szacunkowo ~4 A).
// 190000 = ok. 0.25 A na panel (tyle bylo przy zasilaniu z portu USB komputera).
// Teraz zasilacz Baseus GaN2 Pro (USB-C, 5 V do 3 A): 1520000 = ok. 2 A na
// panel + ok. 0.3 A ESP32 z Wi-Fi - zapas do 3 A zasilacza i gniazda USB-C.
static constexpr uint32_t kPowerBudget{1520000UL};

// Piny HUB75 plytki MatrixPortal S3.
static constexpr int8_t kRgbPins[] {42, 41, 40, 38, 39, 37};
static constexpr int8_t kAddrPins[] {45, 36, 48, 35, 21};
static constexpr int8_t kClockPin{2};
static constexpr int8_t kLatchPin{47};
static constexpr int8_t kOePin{14};

// Zabezpieczenie: gdyby DMA zaklocalo Wi-Fi (README biblioteki ostrzega przed
// tym na MatrixPortal S3), po tylu ms bez Wi-Fi wyjscie DMA jest wylaczane -
// ekran czarny, ale Wi-Fi i OTA dzialaja (updatePanelSafety). Do restartu.
static constexpr uint32_t kDmaWifiTimeoutMs{10UL * 60UL * 1000UL};

static MatrixPanel_I2S_DMA* _dmaPanel{nullptr};
static bool _dmaPanelReady{false};    // begin() sie udal
static bool _panelDisabled{false};    // wylaczony przez brak Wi-Fi
static bool _dmaClockPhase{false};    // biezace ustawienia (do /panel)
static uint8_t _dmaLatchBlanking{0};
static uint32_t _dmaClockMhz{8};

// Poprzednia klatka do plynnego przejscia miedzy trybami (12 KB) - przydzielana
// tylko na czas przejscia (kModeTransitionDurationMs), potem zwalniana.
static constexpr uint16_t kModeTransitionDurationMs{450};
static RgbColor* _modeTransitionFrame{nullptr};
static uint32_t _modeTransitionStartedAtMs{0};
static bool _modeTransitionActive{false};

MatrixLeds leds;

void MatrixLeds::Begin() {
  HUB75_I2S_CFG::i2s_pins pins {
    kRgbPins[0], kRgbPins[1], kRgbPins[2], kRgbPins[3], kRgbPins[4], kRgbPins[5],
    kAddrPins[0], kAddrPins[1], kAddrPins[2], kAddrPins[3], kAddrPins[4],
    kLatchPin, kOePin, kClockPin};
  HUB75_I2S_CFG config(kPanelWidth, kPanelHeight, 1, pins);
  // Ten panel czyta dane na drugim zboczu: z domyslnym clkphase=true czesc
  // pikseli gubila kolor albo migala, a cyfry mialy ubytki (2026-10-27).
  config.clkphase = false;
  // Zegar wyjscia 8 MHz: nizszy = mniej zaklocen radia.
  config.i2sspeed = HUB75_I2S_CFG::HZ_8M;
  // Strojenie bez wgrywania (/panel?clkphase=0|1&lat=1..8&mhz=8|16|20, zapis
  // w Preferences "panel" + restart): brakujace/migajace pojedyncze diody =
  // zle zbocze zegara albo za krotka przerwa przy zatrzasnieciu.
  Preferences preferences;
  if (preferences.begin("panel", true)) {
    config.clkphase = preferences.getBool("clkph", config.clkphase);
    uint8_t latch = preferences.getUChar("lat", 0);
    if (latch > 0) {
      config.latch_blanking = latch;
    }
    uint8_t mhz = preferences.getUChar("mhz", 8);
    config.i2sspeed = mhz >= 20 ? HUB75_I2S_CFG::HZ_20M
      : (mhz >= 16 ? HUB75_I2S_CFG::HZ_16M : HUB75_I2S_CFG::HZ_8M);
    preferences.end();
  }
  _dmaClockPhase = config.clkphase;
  _dmaLatchBlanking = config.latch_blanking;
  _dmaClockMhz = static_cast<uint32_t>(config.i2sspeed) / 1000000UL;
  _dmaPanel = new MatrixPanel_I2S_DMA(config);
  // Nieudany begin(): ekran zostaje czarny, reszta (Wi-Fi, OTA) dziala.
  _dmaPanelReady = _dmaPanel->begin();
  Serial.printf("HUB75 DMA begin: %d\n", _dmaPanelReady ? 1 : 0);
  if (_dmaPanelReady) {
    _dmaPanel->setBrightness8(255);
    _dmaPanel->clearScreen();
  }
  ClearTo(RgbColor(0, 0, 0));
}

bool MatrixLeds::IsActive() const {
  return _dmaPanelReady && !_panelDisabled && !_stopped;
}

void MatrixLeds::SetBrightness(uint8_t brightness) {
  if (_dmaPanelReady && brightness != _brightness) {
    _dmaPanel->setBrightness8(brightness);
  }
  _brightness = brightness;
}

void MatrixLeds::Stop() {
  if (_dmaPanelReady && !_stopped) {
    _dmaPanel->stopDMAoutput();
  }
  _stopped = true;
}

void MatrixLeds::Resume() {
  if (_dmaPanelReady && _stopped && !_panelDisabled) {
    _dmaPanel->resumeDMAoutput();
  }
  _stopped = false;
  _shownValid = false; // narysuj klatke od nowa
}

static uint8_t scaleChannel(uint8_t value, float limit) {
  float scaled = value * limit + 0.5f;
  return scaled >= 255.0f ? 255 : static_cast<uint8_t>(scaled);
}

void MatrixLeds::Show() {
  if (!IsActive()) {
    return;
  }
  // Najpierw liczymy, ile "pradu" pobralaby klatka.
  uint32_t total = 0;
  for (uint16_t pixel = 0; pixel < kPixelCount; pixel++) {
    const RgbColor& color = _pixels[pixel];
    total += color.R + color.G + color.B;
  }
  float limit = total > kPowerBudget
    ? static_cast<float>(kPowerBudget) / total
    : 1.0f;

  // Klatka identyczna jak poprzednia (np. statyczny obrazek) - nic nie wysylamy.
  uint32_t hash = 2166136261UL; // FNV-1a
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(_pixels);
  for (size_t i = 0; i < sizeof(_pixels); i++) {
    hash = (hash ^ bytes[i]) * 16777619UL;
  }
  if (_shownValid && hash == _shownHash) {
    return;
  }
  _shownHash = hash;
  _shownValid = true;

  for (uint16_t pixel = 0; pixel < kPixelCount; pixel++) {
    const RgbColor& color = _pixels[pixel];
    _dmaPanel->drawPixelRGB888(pixel % kColumns, pixel / kColumns,
      scaleChannel(color.R, limit), scaleChannel(color.G, limit),
      scaleChannel(color.B, limit));
  }
}

// Strojenie DMA: -1 = bez zmian. Zapis w Preferences "panel" i restart.
void setPanelTuningAndRestart(int clockPhase, int latch, int mhz) {
  Preferences preferences;
  if (preferences.begin("panel", false)) {
    if (clockPhase >= 0) preferences.putBool("clkph", clockPhase != 0);
    if (latch >= 0) preferences.putUChar("lat", static_cast<uint8_t>(constrain(latch, 0, 8)));
    if (mhz >= 0) preferences.putUChar("mhz", static_cast<uint8_t>(mhz));
    preferences.end();
  }
  delay(200);
  ESP.restart();
}

String getPanelTuningJson() {
  return String(",\"clkphase\":") + (_dmaClockPhase ? 1 : 0) + ",\"latch_blanking\":"
    + _dmaLatchBlanking + ",\"mhz\":" + _dmaClockMhz;
}

bool isPanelActive() {
  return leds.IsActive();
}

// Zabezpieczenie (kDmaWifiTimeoutMs): Wi-Fi nie dziala od tylu ms (od startu
// albo ostatniego polaczenia) - wylacz wyjscie DMA. Ekran czarny do restartu,
// ale jesli to DMA zagluszalo radio, Wi-Fi wroci i da sie wgrac poprawke.
void updatePanelSafety() {
  static uint32_t lastWifiOkMs{0};
  if (_panelDisabled || !_dmaPanelReady) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    lastWifiOkMs = millis();
    return;
  }
  if (millis() - lastWifiOkMs > kDmaWifiTimeoutMs) {
    Serial.println("Brak Wi-Fi przez 10 min - wylaczam panel (DMA)");
    leds.Stop();
    _panelDisabled = true;
  }
}

void beginModeTransition() {
  if (_modeTransitionFrame == nullptr) {
    _modeTransitionFrame = static_cast<RgbColor*>(malloc(kPixelCount * sizeof(RgbColor)));
    if (_modeTransitionFrame == nullptr) {
      return; // za malo pamieci - przelaczenie bez przejscia
    }
  }
  for (uint16_t pixel = 0; pixel < kPixelCount; pixel++) {
    _modeTransitionFrame[pixel] = leds.GetPixelColor(pixel);
  }
  _modeTransitionStartedAtMs = millis();
  _modeTransitionActive = true;
}

// Koniec przejscia: zwalnia bufor. Wolane z applyModeTransition i z loop()
// (nie kazdy tryb rysuje przejscie).
void endModeTransitionIfDone() {
  if (_modeTransitionActive
    && millis() - _modeTransitionStartedAtMs < kModeTransitionDurationMs) {
    return;
  }
  _modeTransitionActive = false;
  free(_modeTransitionFrame);
  _modeTransitionFrame = nullptr;
}

static uint8_t blendModeTransitionChannel(
  uint8_t from, uint8_t to, float progress) {
  return static_cast<uint8_t>(from + (static_cast<int16_t>(to) - from) * progress);
}

void applyModeTransition() {
  if (!_modeTransitionActive) {
    return;
  }
  uint32_t elapsed = millis() - _modeTransitionStartedAtMs;
  if (elapsed >= kModeTransitionDurationMs) {
    endModeTransitionIfDone();
    return;
  }
  float progress = static_cast<float>(elapsed) / kModeTransitionDurationMs;
  for (uint16_t pixel = 0; pixel < kPixelCount; pixel++) {
    RgbColor from = _modeTransitionFrame[pixel];
    RgbColor to = leds.GetPixelColor(pixel);
    leds.SetPixelColor(pixel, RgbColor(
      blendModeTransitionChannel(from.R, to.R, progress),
      blendModeTransitionChannel(from.G, to.G, progress),
      blendModeTransitionChannel(from.B, to.B, progress)));
  }
}
