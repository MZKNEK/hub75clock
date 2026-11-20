// Tryb "Komputer": obciazenie komputera. Dane wysyla co 2 s
// narzedzia/muzyka_zegar.ps1 (liczniki wydajnosci Windows) - patrz web_control.cpp:
//   GET /pc?c=<CPU %>&g=<GPU %>&r=<RAM %>&ru=<RAM MB>&rt=<RAM MB>&v=<VRAM MB>&vt=<VRAM MB>
// Bez danych przez kPcStaleMs: "KOMPUTER WYLACZONY" + godzina. Tylko w RAM.
//
//   y  1     CPU %, z prawej godzina; y 7 pasek
//   y 13     GPU %, z prawej VRAM "2.6/16G"; y 19 pasek
//   y 25     RAM %, z prawej "14/32G"; y 31 pasek
//   y 37-61  wykres ostatnich ~2 min: CPU (turkus) i GPU (fiolet)
#include "zegar.h"

static constexpr uint32_t kPcStaleMs{15000UL};
static constexpr int16_t kPcLeftX{2};
static constexpr int16_t kPcValueX{16};
static constexpr int16_t kPcRightX{62}; // pierwsza kolumna za tekstem wyrownanym do prawej
static constexpr int16_t kPcBarWidth{60};
static constexpr int16_t kPcGraphTop{37};
static constexpr int16_t kPcGraphHeight{25};
static constexpr uint8_t kPcHistory{60};
static constexpr float kPcCpuHue{0.5f};
static constexpr float kPcGpuHue{0.8f};

struct PcStats {
  uint8_t cpu{0};
  uint8_t gpu{0};
  uint8_t ram{0};
  uint32_t ramUsedMb{0};
  uint32_t ramTotalMb{0};
  uint32_t vramUsedMb{0};
  uint32_t vramTotalMb{0};
};
static PcStats _pc{};
static uint32_t _pcReceivedAtMs{0};
static uint8_t _pcCpuHistory[kPcHistory]{};
static uint8_t _pcGpuHistory[kPcHistory]{};
static uint8_t _pcHistoryCount{0};

void updatePcStats(uint8_t cpu, uint8_t gpu, uint8_t ram, uint32_t ramUsedMb,
  uint32_t ramTotalMb, uint32_t vramUsedMb, uint32_t vramTotalMb) {
  _pc.cpu = cpu > 100 ? 100 : cpu;
  _pc.gpu = gpu > 100 ? 100 : gpu;
  _pc.ram = ram > 100 ? 100 : ram;
  _pc.ramUsedMb = ramUsedMb;
  _pc.ramTotalMb = ramTotalMb;
  _pc.vramUsedMb = vramUsedMb;
  _pc.vramTotalMb = vramTotalMb;
  // Po przerwie (komputer byl wylaczony) wykres od nowa.
  if (_pcReceivedAtMs == 0 || millis() - _pcReceivedAtMs > kPcStaleMs) {
    _pcHistoryCount = 0;
  }
  if (_pcHistoryCount == kPcHistory) {
    memmove(_pcCpuHistory, _pcCpuHistory + 1, kPcHistory - 1);
    memmove(_pcGpuHistory, _pcGpuHistory + 1, kPcHistory - 1);
    _pcHistoryCount--;
  }
  _pcCpuHistory[_pcHistoryCount] = _pc.cpu;
  _pcGpuHistory[_pcHistoryCount] = _pc.gpu;
  _pcHistoryCount++;
  _pcReceivedAtMs = millis();
}

bool isPcStatsFresh() {
  return _pcReceivedAtMs != 0 && millis() - _pcReceivedAtMs <= kPcStaleMs;
}

String getPcStatsJson() {
  bool fresh = _pcReceivedAtMs != 0 && millis() - _pcReceivedAtMs <= kPcStaleMs;
  return String("{\"fresh\":") + (fresh ? "true" : "false") + ",\"cpu\":" + _pc.cpu
    + ",\"gpu\":" + _pc.gpu + ",\"ram\":" + _pc.ram + ",\"vram_mb\":" + _pc.vramUsedMb + "}";
}

// "2.6/16G" (ponizej 10 GB z jednym miejscem po przecinku).
static void formatPcMemory(char* buffer, size_t size, uint32_t usedMb, uint32_t totalMb) {
  float used = usedMb / 1024.0f;
  unsigned total = static_cast<unsigned>(lroundf(totalMb / 1024.0f));
  if (used < 9.95f) {
    snprintf(buffer, size, "%.1f/%uG", used, total);
  } else {
    snprintf(buffer, size, "%u/%uG", static_cast<unsigned>(lroundf(used)), total);
  }
}

// 0% = zielony, 100% = czerwony (jak paski panelu Claude).
static float getPcLevelHue(uint8_t percent) {
  return 0.33f * (1.0f - percent / 100.0f);
}

static void drawPcRow(const char* label, uint8_t percent, const char* right, int16_t y) {
  RgbColor labelColor = HslColor(0.58f, 0.5f, kLightness * 0.6f);
  drawSmallText(label, kPcLeftX, y, 1, labelColor);
  char text[6];
  snprintf(text, sizeof(text), "%u%%", percent);
  drawSmallText(text, kPcValueX, y, 1, HslColor(getPcLevelHue(percent), 1.0f, kLightness));
  if (right != nullptr && right[0] != '\0') {
    drawSmallText(right, kPcRightX - getDashTextWidth(right), y, 1, labelColor);
  }
  int16_t filled = static_cast<int16_t>(percent * kPcBarWidth / 100);
  fillClockRect(kPcLeftX, y + 6, filled, 2,
    HslColor(getPcLevelHue(percent), 1.0f, kLightness * 0.8f));
  fillClockRect(kPcLeftX + filled, y + 6, kPcBarWidth - filled, 2,
    HslColor(0.62f, 1.0f, kLightness * 0.3f));
}

static int16_t getPcGraphY(uint8_t percent) {
  return kPcGraphTop + kPcGraphHeight - 1
    - static_cast<int16_t>(percent * (kPcGraphHeight - 1) / 100);
}

// Wykres: najnowsza probka z prawej. GPU rysowane pierwsze, CPU na wierzchu.
static void drawPcGraph() {
  RgbColor grid = HslColor(0.62f, 1.0f, kLightness * 0.25f);
  for (int16_t x = kPcLeftX; x < kPcLeftX + kPcBarWidth; x += 2) {
    setDashPixel(x, kPcGraphTop, grid);
    setDashPixel(x, kPcGraphTop + kPcGraphHeight / 2, grid);
  }
  fillClockRect(kPcLeftX, kPcGraphTop + kPcGraphHeight - 1, kPcBarWidth, 1, grid);
  int16_t startX = kPcLeftX + kPcBarWidth - _pcHistoryCount;
  const uint8_t* series[2] {_pcGpuHistory, _pcCpuHistory};
  const float hues[2] {kPcGpuHue, kPcCpuHue};
  for (uint8_t s = 0; s < 2; s++) {
    RgbColor color = HslColor(hues[s], 1.0f, kLightness);
    for (uint8_t i = 0; i < _pcHistoryCount; i++) {
      int16_t y = getPcGraphY(series[s][i]);
      // Pionowa kreska do poprzedniej probki - linia bez dziur przy skokach.
      int16_t previous = i > 0 ? getPcGraphY(series[s][i - 1]) : y;
      int16_t top = y < previous ? y : previous;
      int16_t bottom = y < previous ? previous : y;
      fillClockRect(startX + i, top, 1, bottom == top ? 1 : bottom - top, color);
      setDashPixel(startX + i, y, color);
    }
  }
}

static void drawPcClock(int16_t x, int16_t y, bool alignRight) {
  struct tm timeInfo;
  if (!_hasValidTime || !readLocalTime(&timeInfo)) {
    return;
  }
  char clockText[6];
  snprintf(clockText, sizeof(clockText), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
  uint32_t secondsOfDay = timeInfo.tm_hour * 3600UL + timeInfo.tm_min * 60UL;
  drawSmallText(clockText, alignRight ? x - getDashTextWidth(clockText) : x, y, 1,
    getHourColor(secondsOfDay, kLightness * 0.8f));
}

void pcAnimation(const AnimationParam& param) {
  leds.ClearTo(kBlack);
  if (_pcReceivedAtMs != 0 && millis() - _pcReceivedAtMs <= kPcStaleMs) {
    drawPcRow("CPU", _pc.cpu, nullptr, 1);
    drawPcClock(kPcRightX, 1, true);
    char memory[12];
    formatPcMemory(memory, sizeof(memory), _pc.vramUsedMb, _pc.vramTotalMb);
    drawPcRow("GPU", _pc.gpu, _pc.vramTotalMb > 0 ? memory : nullptr, 13);
    formatPcMemory(memory, sizeof(memory), _pc.ramUsedMb, _pc.ramTotalMb);
    drawPcRow("RAM", _pc.ram, memory, 25);
    drawPcGraph();
  } else {
    RgbColor color = HslColor(0.58f, 0.5f, kLightness * 0.6f);
    static const char* const kOffText = "WY\x83" "\x80" "CZONY"; // WYLACZONY z ogonkami
    drawSmallText("KOMPUTER", (kColumns - getDashTextWidth("KOMPUTER")) / 2, 20, 1, color);
    drawSmallText(kOffText, (kColumns - getDashTextWidth(kOffText)) / 2, 27, 1, color);
    drawPcClock((kColumns - getDashTextWidth("00:00")) / 2, 38, false);
  }
  drawNetworkStatus();
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(0);
  }
}
