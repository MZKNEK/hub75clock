// Lokalne ustawienia zegara. Skopiuj ten plik jako config.h i wpisz swoje dane.
// config.h jest w .gitignore - hasla i polozenie nie trafiaja do repozytorium.
#pragma once

// Siec Wi-Fi (2,4 GHz).
static constexpr char kWifiSsid[] = "nazwa-sieci";
static constexpr char kWifiPassword[] = "haslo-wifi";

// Haslo do wgrywania przez Wi-Fi (OTA) - Arduino IDE / espota pyta o nie.
static constexpr char kOtaPassword[] = "zmien-mnie";

// Polozenie do pogody (Open-Meteo), np. Warszawa.
static constexpr float kWeatherLatitude{52.23f};
static constexpr float kWeatherLongitude{21.01f};
