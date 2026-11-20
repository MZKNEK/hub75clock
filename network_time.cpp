// Wi-Fi and NTP time synchronization.
#include "zegar.h"
#include "config.h" // kWifiSsid, kWifiPassword
#include <WiFi.h>

static constexpr uint32_t kWifiConnectTimeoutMs{15000};
static constexpr uint32_t kNtpTimeoutMs{3000};
uint8_t _networkErrorCode{kNetworkConnecting};
SyncState _syncState{SyncState::Idle};
static uint32_t _syncStartedAt;
bool _hasValidTime{false};
char _ipText[16]{};
uint32_t _wifiConnectedAtMs{0};

void syncClockFromNtp() {
  _networkErrorCode = kNetworkConnecting;
  _syncState = SyncState::ConnectingToWifi;
  _syncStartedAt = millis();

  if (kWifiSsid[0] == '\0') {
    _networkErrorCode = 1;
    _syncState = SyncState::Idle;
    return;
  }

  WiFi.mode(WIFI_STA);
  // Bez oszczedzania energii: radio stale gotowe (szybsze odpowiedzi, mniej
  // zerwan po wybudzaniu). Przy Protomatterze budzenie ~10x/s dawalo tez
  // wedrujacy pas na panelu. Kosztuje ok. 0,3-0,5 W.
  WiFi.setSleep(false);
  WiFi.setHostname(kDeviceHostname);
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
    _networkErrorCode = kNetworkWaitingForNtp;
    _syncState = SyncState::WaitingForNtp;
    _syncStartedAt = millis();
    return;
  }
  WiFi.begin(kWifiSsid, kWifiPassword);
}

void updateNtpSync() {
  if (_syncState == SyncState::ConnectingToWifi) {
    if (WiFi.status() == WL_CONNECTED) {
      configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
      _networkErrorCode = kNetworkWaitingForNtp;
      _syncState = SyncState::WaitingForNtp;
      _syncStartedAt = millis();
      Serial.println("Wi-Fi OK, IP: " + WiFi.localIP().toString());
      snprintf(_ipText, sizeof(_ipText), "%s", WiFi.localIP().toString().c_str());
      _wifiConnectedAtMs = millis();
    } else if (millis() - _syncStartedAt >= kWifiConnectTimeoutMs) {
      _networkErrorCode = 1;
      _syncState = SyncState::Idle;
      Serial.println("Nie udalo sie polaczyc z Wi-Fi - uzywam poprzedniego czasu.");
    }
    return;
  }

  if (_syncState != SyncState::WaitingForNtp) {
    return;
  }

  struct tm timeInfo;
  if (readLocalTime(&timeInfo)) {
    _networkErrorCode = 0;
    _elapsedSeconds = timeInfo.tm_hour * 3600UL + timeInfo.tm_min * 60UL + timeInfo.tm_sec;
    _hasValidTime = true;
    _syncState = SyncState::Idle;
    Serial.println("Czas zostal zsynchronizowany przez NTP.");
  } else if (millis() - _syncStartedAt >= kNtpTimeoutMs) {
    _networkErrorCode = 2;
    _syncState = SyncState::Idle;
    Serial.println("Nie udalo sie pobrac czasu NTP - uzywam poprzedniego czasu.");
  }
}

// Zamiast getLocalTime(&t, 0): ta przy timeoucie 0 zwraca false, gdy miedzy
// jej dwoma odczytami millis() minie milisekunda (petla nie wykona sie ani
// razu) - przez to co kilka sekund znikaly na klatke kropki dni tygodnia.
bool readLocalTime(struct tm* info) {
  time_t now = time(nullptr);
  localtime_r(&now, info);
  return info->tm_year > (2016 - 1900);
}
