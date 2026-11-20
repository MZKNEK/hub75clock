// Wersja dla Adafruit MatrixPortal S3 (klon) + panel HUB75 64x64.
// Tu tylko setup/loop i wybor trybu. Wspolne typy, stale i funkcje modulow
// sa w zegar.h, kazdy modul w osobnym pliku .cpp (panel LED: panel.cpp).
// Klasy kolorow i animator z NeoPixelBus sa w NeoCompat.h - sama biblioteka
// NeoPixelBus nie jest potrzebna.
#include "zegar.h"

AnimationMode _animationMode{AnimationMode::Clock};
bool _claudeAutoShown{false};

NeoPixelAnimator animations(10); // NeoPixel animation management object

void setup() {

  Serial.begin(115200);
  loadGifSelection();
  // randomSeed() niepotrzebne: random() na ESP32 korzysta ze sprzetowego RNG.

  leds.Begin();
  leds.ClearTo(kBlack);
  leds.Show();

  loadBrightness(); // ustawia kLightness (brightness.cpp)
  selectAnimation(AnimationMode::Clock);
  setupButtons();
  setupImages();
  setupGifs(); // po setupImages - ono montuje FFat
  loadClaudeUsage();
  loadClaudeAutoSwitch();
  loadNotifyBar();
  loadShow(); // Automat wlaczony przed restartem - startuje znowu
  loadCountersUrl();
  watchWifiDisconnects();
  setupWeather();
}

void loop() {
  updatePanelSafety();
  updateOta();
  updateWebControl();
  updateButtons();
  updateClaudeAutoReturn();
  updateShow();
  updateLiveAnnouncements();
  updateNightMode();
  updateClock();
  animations.UpdateAnimations();
  endModeTransitionIfDone();
  drawNotifyBar(); // Automat: godzina i liczniki na dole (notify_bar.cpp)
  if (isScreenOff()) {
    leds.ClearTo(kBlack); // wiadomosci pokazuja sie i tak
    if (isNightClockShown()) {
      drawNightClock(); // brightness.cpp: "W nocy: ciemny zegar"
    }
  }
  drawMessageOverlay();
  leds.Show();
  delay(10);
}

void selectAnimation(AnimationMode mode) {
  if (mode == AnimationMode::Show) {
    startShow(); // mode_show.cpp - sam wybiera tryb z listy
    return;
  }
  if (!_showSwitching) {
    stopShow(); // reczna zmiana trybu (przyciski, strona, panel Claude)
  }
  beginModeTransition();
  stopImageMode(); // zwolnij bufory trybow z obrazkami (nowy tryb przydzieli swoje)
  stopGifMode();
  stopMusicMode();
  stopMazeMode();
  leds.ClearTo(kBlack);

  _animationMode = mode;
  _clockEnabled = false;
  _claudeAutoShown = false;

  if (mode == AnimationMode::Clock) {
    initClock();
  } else if (mode == AnimationMode::Random) {
    animations.StartAnimation(0, kRandomAnimationDurationMs, randomAnimation);
  } else if (mode == AnimationMode::Claude) {
    animations.StartAnimation(0, 1000, claudeAnimation);
  } else if (mode == AnimationMode::Lava) {
    animations.StartAnimation(0, 1000, lavaAnimation);
  } else if (mode == AnimationMode::Life) {
    startLifeMode();
    animations.StartAnimation(0, 1000, lifeAnimation);
  } else if (mode == AnimationMode::Music) {
    animations.StartAnimation(0, 1000, musicAnimation);
  } else if (mode == AnimationMode::Image) {
    startImageMode();
    animations.StartAnimation(0, 1000, imageAnimation);
  } else if (mode == AnimationMode::Analog) {
    animations.StartAnimation(0, 1000, analogAnimation);
  } else if (mode == AnimationMode::Maze) {
    startMazeMode();
    animations.StartAnimation(0, 1000, mazeAnimation);
  } else if (mode == AnimationMode::Pc) {
    animations.StartAnimation(0, 1000, pcAnimation);
  } else if (mode == AnimationMode::Rates) {
    animations.StartAnimation(0, 1000, ratesAnimation);
  } else {
    startGifMode();
    animations.StartAnimation(0, 1000, gifAnimation);
  }
}
