// Panel Claude: stan pracy Claude Code i zuzycie limitow (jak w aplikacji).
// Stan (pracuje / czeka / gotowe) ustawiaja hooki przez /ai?s=...,
// limity, kontekst i model przysyla hook (narzedzia/hook_zegar.ps1) przez
// /claude?... - patrz web_control.cpp. Przy pracy Claude panel wlacza sie sam
// (clock_ai_status.cpp).
//
//   y  1-11  iskra Claude + stan, pod spodem model
//   y 14     linia
//   y 17-26  limit 5 h: zuzyte % + czas do resetu, pasek
//   y 30-39  limit 7 dni: to samo
//   y 43-47  kontekst rozmowy: zajete % + maly pasek (z hooka: tokeny, np. 217K)
//   y 51     linia
//   y 54-58  liczniki: MangaDex (pomaranczowy), Gmail (czerwony), YouTube na zywo
//            (zielony), z prawej godzina
// Paski i % pokazuja ZUZYCIE, jak w aplikacji Claude: krotki zielony = malo,
// dlugi czerwony = limit prawie wyczerpany.
#include "zegar.h"
#include <Preferences.h>

ClaudeUsage _claude{};

static constexpr int16_t kClaudeSparkX{2};
static constexpr int16_t kClaudeSparkY{1};
static constexpr int16_t kClaudeTextX{16};
static constexpr int16_t kClaudeStateY{1};
static constexpr int16_t kClaudeModelY{7};
static constexpr int16_t kClaudeTopDividerY{14};
static constexpr int16_t kClaudeFiveHourY{17};
static constexpr int16_t kClaudeSevenDayY{30};
static constexpr int16_t kClaudeContextY{43};
static constexpr int16_t kClaudeBottomDividerY{51};
static constexpr int16_t kClaudeBottomY{54};
static constexpr int16_t kClaudeLeftX{4};
static constexpr int16_t kClaudeValueX{18};
static constexpr int16_t kClaudeRightX{60}; // pierwsza kolumna za tekstem wyrownanym do prawej
static constexpr uint8_t kClaudeBarWidth{56};
static constexpr int16_t kClaudeContextBarX{36};
static constexpr uint8_t kClaudeContextBarWidth{24};
static constexpr float kClaudeHue{0.042f}; // pomaranczowy jak logo Claude

static int16_t getClaudeTextWidth(const char* text) {
  int16_t width = 0;
  for (const char* c = text; *c; c++) {
    width += getSmallCharAdvance(*c, 1);
  }
  return width > 0 ? width - 1 : 0;
}

// Tekst wielkimi literami, obciety do maxWidth pikseli.
static void drawClaudeText(const char* text, int16_t x, int16_t y, int16_t maxWidth,
  const RgbColor& color) {
  char buffer[32];
  size_t length = 0;
  int16_t width = 0;
  for (const char* c = text; *c != '\0' && length < sizeof(buffer) - 1; c++) {
    char upper = static_cast<char>(toupper(static_cast<unsigned char>(*c)));
    int16_t advance = getSmallCharAdvance(upper, 1);
    if (width + advance - 1 > maxWidth) {
      break;
    }
    buffer[length++] = upper;
    width += advance;
  }
  buffer[length] = '\0';
  drawSmallText(buffer, x, y, 1, color);
}

static void drawClaudeTextRight(const char* text, int16_t y, const RgbColor& color) {
  drawSmallText(text, kClaudeRightX - getClaudeTextWidth(text), y, 1, color);
}

static void drawClaudeLine(int16_t y) {
  RgbColor color = HslColor(0.58f, 1.0f, kLightness * 0.35f);
  for (int16_t x = 4; x < kColumns - 4; x++) {
    setDashPixel(x, y, color);
  }
}

// 0% zuzycia = zielony, 50% = zolty, 100% = czerwony.
static float getClaudeLevelHue(float usedPercent) {
  return 0.33f * (1.0f - constrain(usedPercent, 0.0f, 100.0f) / 100.0f);
}

static void drawClaudeBar(int16_t x, int16_t y, uint8_t width, float usedPercent,
  bool valid) {
  uint8_t filled = valid
    ? static_cast<uint8_t>(lroundf(constrain(usedPercent, 0.0f, 100.0f) * width / 100.0f))
    : 0;
  RgbColor fill = HslColor(getClaudeLevelHue(usedPercent), 1.0f, kLightness * 0.8f);
  RgbColor track = HslColor(0.62f, 1.0f, kLightness * 0.3f);
  for (uint8_t column = 0; column < width; column++) {
    fillClockRect(x + column, y, 1, 3, column < filled ? fill : track);
  }
}

static uint32_t getClaudeNow() {
  return _hasValidTime ? static_cast<uint32_t>(time(nullptr)) : 0;
}

// Czas do resetu: "4:59" albo "6D23H". Pusty, gdy nie wiadomo.
static void formatClaudeReset(char* buffer, size_t size, uint32_t resetsAt, uint32_t now) {
  buffer[0] = '\0';
  if (now == 0 || resetsAt <= now) {
    return;
  }
  unsigned minutes = static_cast<unsigned>((resetsAt - now + 59) / 60);
  if (minutes >= 24 * 60) {
    snprintf(buffer, size, "%uD%uH", minutes / 1440, (minutes % 1440) / 60);
  } else {
    snprintf(buffer, size, "%u:%02u", minutes / 60, minutes % 60);
  }
}

static void drawClaudeLimit(const char* label, const ClaudeLimit& limit, int16_t y,
  uint32_t now) {
  RgbColor labelColor = HslColor(0.58f, 0.5f, kLightness * 0.6f);
  drawSmallText(label, kClaudeLeftX, y, 1, labelColor);
  if (!limit.valid) {
    drawSmallText("--", kClaudeValueX, y, 1, HslColor(0.58f, 0.5f, kLightness * 0.55f));
    drawClaudeBar(kClaudeLeftX, y + 7, kClaudeBarWidth, 0.0f, false);
    return;
  }

  // Po czasie resetu okno zaczyna sie od nowa - nic nie zuzyte.
  bool expired = now != 0 && limit.resetsAt != 0 && limit.resetsAt <= now;
  float used = expired ? 0.0f : constrain(limit.usedPercent, 0.0f, 100.0f);
  char text[8];
  snprintf(text, sizeof(text), "%d%%", static_cast<int>(lroundf(used)));
  drawSmallText(text, kClaudeValueX, y, 1,
    HslColor(getClaudeLevelHue(used), 1.0f, kLightness));

  char reset[12];
  formatClaudeReset(reset, sizeof(reset), limit.resetsAt, now);
  drawClaudeTextRight(reset, y, labelColor);

  drawClaudeBar(kClaudeLeftX, y + 7, kClaudeBarWidth, used, true);
}

static void drawClaudeContext() {
  RgbColor labelColor = HslColor(0.58f, 0.5f, kLightness * 0.6f);
  drawSmallText("CTX", kClaudeLeftX, kClaudeContextY, 1, labelColor);
  if (!_claude.contextValid) {
    drawSmallText("--", kClaudeValueX, kClaudeContextY, 1,
      HslColor(0.58f, 0.5f, kLightness * 0.55f));
    drawClaudeBar(kClaudeContextBarX, kClaudeContextY + 1, kClaudeContextBarWidth, 0.0f, false);
    return;
  }
  if (_claude.contextIsTokens) {
    // Z hooka: znamy tylko liczbe tokenow, nie rozmiar okna -> bez paska.
    char tokens[12];
    snprintf(tokens, sizeof(tokens), "%uK",
      static_cast<unsigned>((_claude.contextTokens + 500) / 1000));
    drawSmallText(tokens, kClaudeValueX, kClaudeContextY, 1,
      HslColor(0.13f, 0.7f, kLightness * 0.8f));
    return;
  }
  float used = constrain(_claude.contextUsedPercent, 0.0f, 100.0f);
  char text[8];
  snprintf(text, sizeof(text), "%d%%", static_cast<int>(lroundf(used)));
  drawSmallText(text, kClaudeValueX, kClaudeContextY, 1,
    HslColor(getClaudeLevelHue(used), 1.0f, kLightness));
  drawClaudeBar(kClaudeContextBarX, kClaudeContextY + 1, kClaudeContextBarWidth, used, true);
}

static void drawClaudeBottomRow() {
  // Liczniki MangaDex i Gmail (clock.cpp) - same liczby, obok siebie.
  int16_t x = kClaudeLeftX;
  if (isCounterVisible(_mangaUnread, _mangaUpdatedAtMs, kMangaStaleMs)) {
    char text[4];
    formatCounter(text, sizeof(text), _mangaUnread);
    drawSmallText(text, x, kClaudeBottomY, 1, HslColor(kMangaHue, 1.0f, kLightness));
    x += getClaudeTextWidth(text) + kCounterGap;
  }
  if (isCounterVisible(_comixUnread, _comixUpdatedAtMs, kComixStaleMs)) {
    char text[4];
    formatCounter(text, sizeof(text), _comixUnread);
    drawSmallText(text, x, kClaudeBottomY, 1, HslColor(kComixHue, 1.0f, kLightness));
    x += getClaudeTextWidth(text) + kCounterGap;
  }
  if (isCounterVisible(_mailUnread, _mailUpdatedAtMs, kMailStaleMs)) {
    char text[4];
    formatCounter(text, sizeof(text), _mailUnread);
    drawSmallText(text, x, kClaudeBottomY, 1, HslColor(kMailHue, 1.0f, kLightness));
    x += getClaudeTextWidth(text) + kCounterGap;
  }
  if (isCounterVisible(_liveCount, _liveUpdatedAtMs, kLiveStaleMs)) {
    char text[4];
    formatCounter(text, sizeof(text), _liveCount);
    drawSmallText(text, x, kClaudeBottomY, 1, HslColor(kLiveHue, 1.0f, kLightness));
  }
  struct tm timeInfo;
  if (_hasValidTime && readLocalTime(&timeInfo)) {
    char clockText[6];
    snprintf(clockText, sizeof(clockText), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
    uint32_t secondsOfDay = timeInfo.tm_hour * 3600UL + timeInfo.tm_min * 60UL;
    drawClaudeTextRight(clockText, kClaudeBottomY,
      getHourColor(secondsOfDay, kLightness * 0.8f));
  }
}

// Iskra 11x11 w stylu logo Claude: 8 promieni wokol srodka.
static void drawClaudeSpark(float hue, float saturation, float lightness, float rotation) {
  constexpr uint8_t kRays{8};
  constexpr float kRadius{5.4f};
  for (int16_t py = 0; py < 11; py++) {
    for (int16_t px = 0; px < 11; px++) {
      float dx = px - 5.0f;
      float dy = py - 5.0f;
      float distance = sqrtf(dx * dx + dy * dy);
      if (distance > kRadius) {
        continue;
      }
      bool lit = distance <= 1.2f;
      if (!lit) {
        float ray = (atan2f(dy, dx) + rotation) * kRays / (2.0f * PI);
        float offset = fabsf(ray - roundf(ray)) * 2.0f * PI / kRays;
        lit = distance * offset < 0.65f; // odleglosc od osi promienia w px
      }
      if (lit) {
        setDashPixel(kClaudeSparkX + px, kClaudeSparkY + py, HslColor(hue, saturation,
          lightness * (1.0f - 0.45f * distance / kRadius)));
      }
    }
  }
}

static void drawClaudeHeader() {
  expireAiState();
  uint32_t now = millis();
  float wave = 0.5f + 0.5f * sinf((now % 1500UL) * 2.0f * PI / 1500.0f);
  const char* stateText;
  float hue;
  switch (_aiState) {
    case AiState::Working:
      stateText = "PRACUJE";
      hue = kClaudeHue;
      // Obraca sie o 1/8 obrotu na sekunde i lekko pulsuje.
      drawClaudeSpark(hue, 1.0f, kLightness * (0.9f + 0.4f * wave),
        (now % 1000UL) / 1000.0f * 2.0f * PI / 8.0f);
      break;
    case AiState::Waiting:
      stateText = "CZEKA";
      hue = 0.14f;
      drawClaudeSpark(hue, 1.0f, kLightness * (0.4f + 0.9f * wave), 0.0f);
      break;
    case AiState::Done: {
      stateText = "GOTOWE";
      hue = 0.33f;
      uint32_t sinceChange = now - _aiChangedAtMs;
      if (sinceChange >= kAiDoneBlinkMs || (sinceChange / 250UL) % 2 == 0) {
        drawClaudeSpark(hue, 1.0f, kLightness * 1.2f, 0.0f);
      }
      break;
    }
    case AiState::Idle:
    default:
      stateText = "WOLNY";
      hue = kClaudeHue;
      drawClaudeSpark(hue, 0.6f, kLightness * 0.6f, 0.0f);
      break;
  }
  drawSmallText(stateText, kClaudeTextX, kClaudeStateY, 1, HslColor(hue, 1.0f, kLightness));

  int16_t maxWidth = kColumns - kClaudeTextX - 1;
  if (_claude.updatedAtMs == 0) {
    drawClaudeText("brak danych", kClaudeTextX, kClaudeModelY, maxWidth,
      HslColor(0.58f, 0.5f, kLightness * 0.55f));
  } else if (_claude.model[0] != '\0') {
    drawClaudeText(_claude.model, kClaudeTextX, kClaudeModelY, maxWidth,
      HslColor(kClaudeHue, 0.4f, kLightness * 0.7f));
  }
}

void claudeAnimation(const AnimationParam& param) {
  leds.ClearTo(kBlack);
  uint32_t now = getClaudeNow();

  drawClaudeHeader();
  drawClaudeLine(kClaudeTopDividerY);
  drawClaudeLimit("5H", _claude.fiveHour, kClaudeFiveHourY, now);
  drawClaudeLimit("7D", _claude.sevenDay, kClaudeSevenDayY, now);
  drawClaudeContext();
  drawClaudeLine(kClaudeBottomDividerY);
  drawClaudeBottomRow();
  drawNetworkStatus();
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(0);
  }
}

// Ostatnie dane panelu w Preferences "claude" - po restarcie (np. po OTA) panel
// pokazuje je od razu zamiast "BRAK DANYCH". Stan pracy (_aiState) nie jest
// zapisywany. Zapis przy kazdym /claude z danymi (kilkadziesiat bajtow, NVS
// rozklada zapisy po sektorach).
void saveClaudeUsage() {
  ClaudeUsage saved = _claude;
  saved.updatedAtMs = 0; // millis() po restarcie nic nie znaczy
  Preferences preferences;
  if (preferences.begin("claude", false)) {
    preferences.putBytes("usage", &saved, sizeof(saved));
    preferences.end();
  }
}

void loadClaudeUsage() {
  Preferences preferences;
  if (!preferences.begin("claude", true)) {
    return;
  }
  ClaudeUsage saved;
  if (preferences.getBytesLength("usage") == sizeof(saved)
    && preferences.getBytes("usage", &saved, sizeof(saved)) == sizeof(saved)) {
    saved.model[sizeof(saved.model) - 1] = '\0';
    _claude = saved;
    _claude.updatedAtMs = millis() | 1; // sa dane (0 = "brak danych")
  }
  preferences.end();
}
