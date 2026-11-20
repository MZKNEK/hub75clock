// Wgrywanie programu przez Wi-Fi (OTA).
// W Arduino IDE: Tools -> Port -> "zegar-matrix at 192.168.x.x" (port sieciowy),
// potem zwykly Upload. IDE zapyta o haslo kOtaPassword z config.h.
//
// Na czas wgrywania panel jest wylaczany: zapis do pamieci flash co chwile
// wstrzymuje odswiezanie i swiecace piksele mocno by migaly. Postep widac
// w Arduino IDE, po wgraniu zegar sam sie restartuje.
#include "zegar.h"
#include "config.h" // kOtaPassword
#include <WiFi.h>
#include <ArduinoOTA.h>

static bool _otaStarted{false};

static void setupOta() {
  ArduinoOTA.setHostname(kDeviceHostname);
  ArduinoOTA.setPassword(kOtaPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA: start - wylaczam panel");
    // Wazne: po Stop() nie wolno wolac leds.Show() az do Resume().
    leds.Stop();
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("OTA: koniec, restart");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA: blad %u\n", static_cast<unsigned>(error));
    // Wgrywanie przerwane - wlaczamy panel z powrotem.
    leds.Resume();
    selectAnimation(_animationMode);
  });

  ArduinoOTA.begin();
  _otaStarted = true;
  Serial.printf("OTA gotowe: %s.local\n", kDeviceHostname);
}

void updateOta() {
  if (!_otaStarted) {
    // OTA startuje, gdy tylko zegar polaczy sie z Wi-Fi.
    if (WiFi.status() == WL_CONNECTED) {
      setupOta();
    }
    return;
  }
  ArduinoOTA.handle();
}
