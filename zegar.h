// Wspolny naglowek zegara: stale ekranu, typy, stan dzielony miedzy modulami
// i funkcje, ktore moduly wolaja nawzajem (pogrupowane wg pliku .cpp).
// Kazdy plik .cpp dolacza ten naglowek i tylko te biblioteki, ktorych sam
// uzywa - zmiana jednego modulu kompiluje tylko jego (i laczenie).
// Zmienne: tu tylko "extern" (definicja w jednym pliku .cpp) albo stale -
// "static" zmienna w naglowku dalaby osobna kopie w kazdym pliku.
#pragma once

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include "NeoCompat.h"

// --- Ekran i kolory -----------------------------------------------------------

// Ekran 64x64. pixelIndex(wiersz, kolumna) daje indeks piksela (wiersz 0 = gora).
static constexpr uint16_t kRows{64};
static constexpr uint16_t kColumns{64};
static constexpr uint16_t kPixelCount{kRows * kColumns};
#define pixelIndex(row, column) ((row) * kColumns + (column))

// Wspolne ustawienia jasnosci dla wszystkich animacji (kLightness ustawia
// brightness.cpp wg suwaka na stronie).
static constexpr float kDefaultLightness{0.013f};
extern float kLightness;
static const RgbColor kBlack{0, 0, 0};

// Inna nazwa niz stary zegar ("zegar"), zeby sie nie gryzly w sieci.
static constexpr char kDeviceHostname[] = "zegar-matrix";

// --- Tryby ----------------------------------------------------------------------

// Lokalne dane (Wi-Fi, haslo OTA, polozenie) - config.h poza repozytorium.
#if !__has_include("config.h")
#error "Brak config.h - skopiuj config.example.h jako config.h i wpisz swoje dane"
#endif

enum class AnimationMode : uint8_t {
  // Nowe tryby dopisywac przed Show - lista Automatu trzyma numery trybow.
  Clock, Random, Gif, Claude, Lava, Life, Music, Image, Pc, Rates,
  Analog, Maze, Show
};
extern AnimationMode _animationMode;
// Panel Claude wlaczony sam przez hooki (clock_ai_status.cpp) - wroci do
// poprzedniego trybu. Kazde selectAnimation() to kasuje (reczna zmiana).
extern bool _claudeAutoShown;
extern NeoPixelAnimator animations;

// zegar_matrixportal.ino
void selectAnimation(AnimationMode mode);

// --- Panel LED (panel.cpp) -------------------------------------------------------

// Bufor 64x64 RgbColor -> panel HUB75 przez ESP32-HUB75-MatrixPanel-DMA (to samo
// API, ktorego animacje uzywaly z NeoPixelBus). Szczegoly w panel.cpp.
class MatrixLeds {
 public:
  void Begin();
  bool IsActive() const;
  // Sprzetowa jasnosc 0-255 (czas swiecenia OE) - przyciemnia wszystkie kolory
  // rowno, bez utraty odcieni (brightness.cpp).
  void SetBrightness(uint8_t brightness);
  // OTA: zapis flash przy dzialajacym panelu dawal mocne miganie.
  // Po Stop() NIE wolac Show().
  void Stop();
  void Resume();
  void Show();

  void ClearTo(const RgbColor& color) {
    for (uint16_t pixel = 0; pixel < kPixelCount; pixel++) {
      _pixels[pixel] = color;
    }
  }

  void SetPixelColor(uint16_t pixel, const RgbColor& color) {
    if (pixel < kPixelCount) {
      _pixels[pixel] = color;
    }
  }

  RgbColor GetPixelColor(uint16_t pixel) const {
    return pixel < kPixelCount ? _pixels[pixel] : RgbColor(0, 0, 0);
  }

 private:
  RgbColor _pixels[kPixelCount]{};
  uint32_t _shownHash{0};
  bool _shownValid{false};
  bool _stopped{false};
  uint8_t _brightness{255};
};
extern MatrixLeds leds;

// Plynne przejscie miedzy trybami: begin w selectAnimation, apply na koncu
// klatki kazdego trybu, end z loop() (nie kazdy tryb rysuje przejscie).
void beginModeTransition();
void endModeTransitionIfDone();
void applyModeTransition();
void setPanelTuningAndRestart(int clockPhase, int latch, int mhz);
String getPanelTuningJson();
bool isPanelActive();
void updatePanelSafety();

// --- Czas i Wi-Fi (network_time.cpp) ---------------------------------------------

static constexpr uint8_t kNetworkConnecting{3};
static constexpr uint8_t kNetworkWaitingForNtp{4};
extern uint8_t _networkErrorCode; // 1 = Wi-Fi, 2 = NTP, 3 = connecting, 4 = waiting
enum class SyncState : uint8_t { Idle, ConnectingToWifi, WaitingForNtp };
extern SyncState _syncState;
extern bool _hasValidTime;
// Adres IP pokazywany na zegarze przez chwile po polaczeniu z Wi-Fi.
extern char _ipText[16];
extern uint32_t _wifiConnectedAtMs;

bool readLocalTime(struct tm* info);
void syncClockFromNtp();
void updateNtpSync();

// --- Pulpit, czcionka, liczniki (clock.cpp) --------------------------------------

extern uint32_t _elapsedSeconds;
extern bool _clockEnabled;

void initClock();
void updateClock();
void clockAnimation(const AnimationParam& param);
uint16_t getClockAnimationDurationMs();
float getClockAnimationProgress(const AnimationParam& param);
HslColor getHourColor(uint32_t seconds, float lightness);
HslColor getMinuteColor(float lightness);
HslColor getSeparatorColor(float lightness);
void drawNightClock();
// Rysowanie uzywane przez inne ekrany.
void fillClockRect(int16_t x, int16_t y, int16_t width, int16_t height,
  const RgbColor& color);
void setDashPixel(int16_t x, int16_t y, const RgbColor& color);
uint8_t getSmallCharAdvance(char character, uint8_t scale);
// Tekst czcionka 3x5 (cyfry, wielkie litery z polskimi, - / . : % + ! ? i '*'
// jako stopien).
void drawSmallText(const char* text, int16_t x, int16_t y, uint8_t scale,
  const RgbColor& color);
int16_t getDashTextWidth(const char* text);
void drawNetworkStatus();

// Liczniki MangaDex / Gmail / YouTube na zywo (pulpit, panel Claude, pasek
// Automatu). Licznik znika przy 0 i gdy dane dawno nie przyszly.
static constexpr uint32_t kMangaStaleMs{6UL * 60UL * 60UL * 1000UL};
static constexpr uint32_t kMailStaleMs{60UL * 60UL * 1000UL};
static constexpr uint32_t kLiveStaleMs{30UL * 60UL * 1000UL};
static constexpr int16_t kCounterGap{4};
static constexpr float kMangaHue{0.04f}; // pomarancz MangaDex
static constexpr float kMailHue{0.0f};   // czerwien Gmail
// Czysta zielen = jedna dioda w pikselu. Fiolet (czerwona + niebieska) na P2
// rozpadal sie na osobne kolory; czerwien YouTube zajeta przez Gmail.
static constexpr float kLiveHue{0.333f};
// comix.to (muzyka_zegar.ps1 -> /comix): fiolet z przewaga niebieskiego (0,8 - czerwona
// dioda na ok. 20%), zeby mniej sie rozpadal niz rowny R+B.
static constexpr uint32_t kComixStaleMs{6UL * 60UL * 60UL * 1000UL};
static constexpr float kComixHue{0.8f};
bool isCounterVisible(int16_t count, uint32_t updatedAtMs, uint32_t staleMs);
void formatCounter(char* buffer, size_t size, int16_t count);

// --- Liczniki z Apps Script (web_counters.cpp) -----------------------------------

// -1 = brak danych. Ustawiane przez web_counters.cpp albo recznie (/manga, /mail, /live).
extern int16_t _mangaUnread;
extern uint32_t _mangaUpdatedAtMs;
extern int16_t _mailUnread;
extern uint32_t _mailUpdatedAtMs;
extern int16_t _liveCount;
extern uint32_t _liveUpdatedAtMs;
extern int16_t _comixUnread;      // tylko z komputera (/comix)
extern uint32_t _comixUpdatedAtMs;

void loadCountersUrl();
bool hasCountersUrl();
bool setCountersUrl(const String& url);
String getCountersStatusJson();
uint32_t getCountersFetchedAgoSeconds();
int getCountersLastStatus();
void updateOnlineCounters();
void updateLiveAnnouncements();

// --- Pogoda (weather.cpp) --------------------------------------------------------

struct WeatherData {
  bool valid{false};
  float temperature{0.0f};
  float maxTemp{0.0f};
  float minTemp{0.0f};
  int code{-1};
  bool isDay{true};
  int16_t sunriseMinutes{-1};
  int16_t sunsetMinutes{-1};
  uint32_t fetchedAtMs{0};
  // Prognoza na kolejne dni (jutro, pojutrze, ...): srednia temperatura
  // i najwieksza szansa opadow w ciagu dnia.
  static constexpr uint8_t kForecastDays{3};
  float dayMeanTemp[kForecastDays]{};
  uint8_t dayRainChance[kForecastDays]{}; // %
  uint8_t forecastCount{0};
};

WeatherData getWeather();
void setupWeather();

// --- Stan Claude i auto-panel (clock_ai_status.cpp) ------------------------------

enum class AiState : uint8_t { Idle, Working, Waiting, Done };
extern AiState _aiState;
extern uint32_t _aiChangedAtMs;
static constexpr uint32_t kAiDoneBlinkMs{3000UL}; // "GOTOWE" na poczatku miga

void loadClaudeAutoSwitch();
void setClaudeAutoSwitch(bool enabled);
bool isClaudeAutoSwitchEnabled();
bool isClaudeBusy();
void expireAiState();
void updateClaudeAutoReturn();
const char* getAiStateName();
bool setAiStateByName(const String& name);

// --- Panel Claude (clock_claude_panel.cpp) ---------------------------------------

// Dane ze statusline Claude Code (/claude?h=..&hr=..&w=..&wr=..&c=..&usd=..&m=..)
// albo z hooka start/koniec pracy (/claude?s=..&m=..&ct=.. - kontekst w tokenach).
struct ClaudeLimit {
  bool valid{false};
  float usedPercent{0.0f};
  uint32_t resetsAt{0}; // Unix epoch (s)
};
struct ClaudeUsage {
  ClaudeLimit fiveHour{};
  ClaudeLimit sevenDay{};
  bool contextValid{false};
  bool contextIsTokens{false}; // hook zna tylko liczbe tokenow, nie %
  float contextUsedPercent{0.0f};
  uint32_t contextTokens{0};
  bool costValid{false};
  float costUsd{0.0f};
  char model[24]{};
  uint32_t updatedAtMs{0};
};
extern ClaudeUsage _claude;

void claudeAnimation(const AnimationParam& param);
void loadClaudeUsage();
void saveClaudeUsage();

// --- Jasnosc i tryb nocny (brightness.cpp) ---------------------------------------

void loadBrightness();
void setBrightnessLevel(uint8_t level);
uint8_t getBrightnessLevel();
bool isScreenOff();
void updateNightMode();
void setNightMode(uint8_t start, uint8_t end, bool dim, uint8_t clockLevel);
uint8_t getNightClockLevel();
void wakeUntilMorning();
uint8_t getNightOffStart();
uint8_t getNightOffEnd();
bool isNightDimEnabled();
bool isNightOffTime();
bool isNightWoken();
bool isDarkNow();
bool isNightClockShown();
String getNightStatusJson();
void notePcIdleSeconds(uint32_t idleSeconds);
bool isPcActivityRecent();

// --- GIF-y i rysowanie obrazkow (gif_animation.cpp) ------------------------------

static constexpr uint8_t kGifMax{30};
// Obraz 64x64 (GIF, Obrazek): kolor piksela o numerze y * 64 + x - do rysowania
// calosci albo zmniejszonego pod pasek Automatu (drawPicture).
typedef RgbColor (*PictureSampler)(uint16_t pixel);

void gifAnimation(const AnimationParam& param);
void startGifMode();
void stopGifMode();
void loadGifSelection();
void setupGifs();
uint8_t getGifCount();
const char* getGifName(uint8_t index);
int8_t getGifSelection();
void setGifSelection(int8_t selection);
void setGifShowOverride(int8_t selection);
void beginGifUpload();
void writeGifUpload(const uint8_t* data, size_t length);
void abortGifUpload();
bool finishGifUpload();
bool deleteGif(uint8_t index);
uint32_t getGifFreeBytes();
// Kolory obrazkow (GIF, Obrazek, okladka): gamma 2.0 i jasnosc.
float getPictureLevel();
RgbColor toPanelColor(uint8_t red, uint8_t green, uint8_t blue, float level);
void drawPicture(PictureSampler sample);

// --- Obrazki (mode_image.cpp) ----------------------------------------------------

static constexpr uint8_t kImageMax{20};
static constexpr size_t kImageBytes{kPixelCount * 3};

void imageAnimation(const AnimationParam& param);
void setupImages();
void startImageMode();
void stopImageMode();
uint8_t getImageCount();
int8_t getImageSelection();
void setImageSelection(int8_t selection);
void setImageShowOverride(int8_t selection);
bool addImage(const String& hex);
bool deleteImage(uint8_t index);
bool decodeHexByte(char high, char low, uint8_t* value);

// --- Automat (mode_show.cpp) i pasek powiadomien (notify_bar.cpp) ---------------

static constexpr uint8_t kShowMaxEntries{20};
extern bool _showSwitching; // selectAnimation() wolane przez Automat

void startShow();
void stopShow();
void updateShow();
void loadShow();
bool isShowRunning();
uint8_t getShowCount();
bool getShowEntry(uint8_t index, uint8_t* mode, int8_t* option, uint16_t* seconds);
uint8_t setShowEntriesFromWeb(const String* values, const uint16_t* seconds, uint8_t count);

void loadNotifyBar();
void setNotifyBarMode(uint8_t mode);
uint8_t getNotifyBarMode();
void setNotifyBarFit(bool fit);
bool getNotifyBarFit();
void drawNotifyBar();
uint8_t getEffectRows();

// --- Pozostale ekrany ------------------------------------------------------------

// pattern_animations.cpp
static constexpr uint16_t kRandomAnimationDurationMs{300};
void randomAnimation(const AnimationParam& param);
// mode_lava.cpp, mode_life.cpp, mode_analog.cpp, mode_maze.cpp
void lavaAnimation(const AnimationParam& param);
void lifeAnimation(const AnimationParam& param);
void startLifeMode();
void analogAnimation(const AnimationParam& param);
void mazeAnimation(const AnimationParam& param);
void startMazeMode();
void stopMazeMode();
// mode_music.cpp
void musicAnimation(const AnimationParam& param);
void stopMusicMode();
void updateNowPlaying(const String& artist, const String& title, uint32_t positionMs,
  uint32_t durationMs, bool playing);
void clearNowPlaying();
bool setNowPlayingCover(const String& hex);
String getNowPlayingJson();
bool isMusicPlaying();
// mode_pc.cpp
void pcAnimation(const AnimationParam& param);
void updatePcStats(uint8_t cpu, uint8_t gpu, uint8_t ram, uint32_t ramUsedMb,
  uint32_t ramTotalMb, uint32_t vramUsedMb, uint32_t vramTotalMb);
String getPcStatsJson();
bool isPcStatsFresh();
// mode_rates.cpp
void ratesAnimation(const AnimationParam& param);
void updateRates();
String getRatesJson();

// --- Wiadomosci, strona, przyciski, OTA ------------------------------------------

// message_overlay.cpp
void showMessage(const String& text, uint32_t seconds, float hue);
bool isMessageActive();
void hideMessage();
void drawMessageOverlay();
void foldTextForDisplay(const String& text, char* out, size_t size);
// web_control.cpp
void updateWebControl();
void watchWifiDisconnects();
// buttons.cpp
void setupButtons();
void updateButtons();
// ota.cpp
void updateOta();
