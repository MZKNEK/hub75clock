// Liczniki z Google bez komputera: zegar co kCountersRefreshMs pyta Apps Script
// uzytkownika (narzedzia/zegar_liczniki.gs, wdrozony jako aplikacja internetowa)
// i dostaje {"mail":2,"manga":5,"live":1,"live_names":"Kanal|Inny"} - mail
// i manga to te same liczby co z /mail i /manga; live = kanaly YouTube na zywo.
// Gdy w live_names pojawi sie nowy kanal, zegar pokazuje "LIVE: kanal"
// (pierwsze pobranie po starcie nic nie oglasza; przy wylaczonym ekranie tez nie).
// Adres /exec wpisuje sie na stronie zegara (POST /counters), trzymany jest
// w Preferences "counters" i nie jest nigdzie pokazywany (kto go zna, widzi
// liczby). Pobieranie idzie w zadaniu pogody (weather.cpp), po kolei z pogoda,
// zeby nie trzymac w pamieci dwoch polaczen HTTPS naraz.
#include "zegar.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

int16_t _mangaUnread{-1};
uint32_t _mangaUpdatedAtMs{0};
int16_t _mailUnread{-1};
uint32_t _mailUpdatedAtMs{0};
int16_t _liveCount{-1};
uint32_t _liveUpdatedAtMs{0};
int16_t _comixUnread{-1};
uint32_t _comixUpdatedAtMs{0};

static constexpr uint32_t kCountersRefreshMs{5UL * 60UL * 1000UL};
static constexpr uint32_t kCountersRetryMs{60UL * 1000UL};
static constexpr size_t kCountersUrlSize{200};

static char _countersUrl[kCountersUrlSize]{};
static portMUX_TYPE _countersMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool _countersFetchNow{false};
static uint32_t _countersNextFetchAtMs{0};
static uint32_t _countersFetchedAtMs{0};  // ostatnie udane pobranie
static int _countersLastStatus{0};        // kod HTTP albo blad HTTPClient
static constexpr size_t kLiveNamesSize{160};
static char _liveNames[kLiveNamesSize]{};        // ostatnio pobrane, "A|B"
static bool _liveNamesKnown{false};              // bylo juz pobranie z live
static char _liveAnnouncement[kLiveNamesSize]{}; // dla loop(): "A, B"

void loadCountersUrl() {
  Preferences preferences;
  if (preferences.begin("counters", true)) {
    String url = preferences.getString("url", "");
    preferences.end();
    strlcpy(_countersUrl, url.c_str(), sizeof(_countersUrl));
  }
}

bool hasCountersUrl() {
  return _countersUrl[0] != '\0';
}

// Pusty adres = wylacz. Zwraca false dla adresu spoza script.google.com.
bool setCountersUrl(const String& url) {
  String trimmed = url;
  trimmed.trim();
  if (trimmed.length() > 0 && (!trimmed.startsWith("https://script.google.com/")
    || trimmed.length() >= kCountersUrlSize)) {
    return false;
  }
  portENTER_CRITICAL(&_countersMux);
  strlcpy(_countersUrl, trimmed.c_str(), sizeof(_countersUrl));
  portEXIT_CRITICAL(&_countersMux);
  Preferences preferences;
  if (preferences.begin("counters", false)) {
    preferences.putString("url", trimmed);
    preferences.end();
  }
  _countersFetchedAtMs = 0;
  _countersLastStatus = 0;
  _countersFetchNow = true;
  return true;
}

String getCountersStatusJson() {
  return String("{\"url_set\":") + (hasCountersUrl() ? "true" : "false")
    + ",\"last_status\":" + _countersLastStatus
    + ",\"fetched_s_ago\":" + (_countersFetchedAtMs == 0 ? -1
      : static_cast<long>((millis() - _countersFetchedAtMs) / 1000UL))
    + ",\"mail\":" + _mailUnread + ",\"manga\":" + _mangaUnread
    + ",\"live\":" + _liveCount + ",\"comix\":" + _comixUnread + "}";
}

uint32_t getCountersFetchedAgoSeconds() {
  return _countersFetchedAtMs == 0 ? UINT32_MAX : (millis() - _countersFetchedAtMs) / 1000UL;
}

int getCountersLastStatus() {
  return _countersLastStatus;
}

// Czy na liscie "A|B" jest nazwa name (length znakow).
static bool containsLiveName(const char* list, const char* name, size_t length) {
  for (const char* start = list; *start != '\0';) {
    const char* end = strchr(start, '|');
    size_t size = end != nullptr ? static_cast<size_t>(end - start) : strlen(start);
    if (size == length && strncmp(start, name, length) == 0) {
      return true;
    }
    if (end == nullptr) {
      break;
    }
    start = end + 1;
  }
  return false;
}

// live_names "A|B": kanaly, ktorych nie bylo w poprzednim pobraniu, ida do
// _liveAnnouncement jako "A, B".
static void noteLiveNames(const String& body) {
  char names[kLiveNamesSize]{};
  int at = body.indexOf("\"live_names\":\"");
  if (at >= 0) {
    at += 14;
    int end = body.indexOf('"', at);
    if (end > at) {
      memcpy(names, body.c_str() + at, min(static_cast<size_t>(end - at), sizeof(names) - 1));
    }
  }
  char started[kLiveNamesSize]{};
  if (_liveNamesKnown) {
    for (const char* start = names; *start != '\0';) {
      const char* end = strchr(start, '|');
      size_t length = end != nullptr ? static_cast<size_t>(end - start) : strlen(start);
      size_t used = strlen(started);
      if (length > 0 && !containsLiveName(_liveNames, start, length)
        && used + length + 2 < sizeof(started)) {
        if (used > 0) {
          strcpy(started + used, ", ");
          used += 2;
        }
        memcpy(started + used, start, length);
        started[used + length] = '\0';
      }
      if (end == nullptr) {
        break;
      }
      start = end + 1;
    }
  }
  portENTER_CRITICAL(&_countersMux);
  strlcpy(_liveNames, names, sizeof(_liveNames));
  if (started[0] != '\0') {
    strlcpy(_liveAnnouncement, started, sizeof(_liveAnnouncement));
  }
  portEXIT_CRITICAL(&_countersMux);
  _liveNamesKnown = true;
}

// "\"mail\":12" -> 12. false, gdy brak klucza albo wartosc to null.
static bool readCounterValue(const String& body, const char* key, long* value) {
  int at = body.indexOf(key);
  if (at < 0) {
    return false;
  }
  const char* text = body.c_str() + at + strlen(key);
  while (*text == ' ') {
    text++;
  }
  if (*text < '0' || *text > '9') {
    return false;
  }
  *value = strtol(text, nullptr, 10);
  return true;
}

static void fetchCounters() {
  char url[kCountersUrlSize];
  portENTER_CRITICAL(&_countersMux);
  memcpy(url, _countersUrl, sizeof(url));
  portEXIT_CRITICAL(&_countersMux);
  if (url[0] == '\0') {
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // tylko dwie liczby - bez sprawdzania certyfikatu
  HTTPClient http;
  // Apps Script odsyla 302 na script.googleusercontent.com; MangaDex po
  // wygasnieciu pamieci podrecznej liczy sie kilka sekund.
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  if (!http.begin(client, url)) {
    _countersLastStatus = -100;
    return;
  }
  int status = http.GET();
  _countersLastStatus = status;
  if (status != HTTP_CODE_OK) {
    Serial.printf("Liczniki: blad HTTP %d\n", status);
    http.end();
    return;
  }
  String body = http.getString();
  http.end();

  long value;
  bool any = false;
  if (readCounterValue(body, "\"mail\":", &value)) {
    _mailUnread = static_cast<int16_t>(value > 999 ? 999 : value);
    _mailUpdatedAtMs = millis();
    any = true;
  }
  if (readCounterValue(body, "\"manga\":", &value)) {
    _mangaUnread = static_cast<int16_t>(value > 999 ? 999 : value);
    _mangaUpdatedAtMs = millis();
    any = true;
  }
  if (readCounterValue(body, "\"live\":", &value)) {
    _liveCount = static_cast<int16_t>(value > 999 ? 999 : value);
    _liveUpdatedAtMs = millis();
    noteLiveNames(body);
    any = true;
  }
  if (any) {
    _countersFetchedAtMs = millis();
  }
}

// Wolane w loop(): wiadomosc o nowych streamach (wiadomosci rysuje rdzen 1,
// a liczniki pobiera rdzen 0). Przy wylaczonym ekranie (noc) nic nie pokazuje.
void updateLiveAnnouncements() {
  if (_liveAnnouncement[0] == '\0') {
    return;
  }
  char text[kLiveNamesSize];
  portENTER_CRITICAL(&_countersMux);
  strlcpy(text, _liveAnnouncement, sizeof(text));
  _liveAnnouncement[0] = '\0';
  portEXIT_CRITICAL(&_countersMux);
  if (!isScreenOff()) {
    showMessage(String("LIVE: ") + text, 0, 0.0f);
  }
}

// Wolane co sekunde z zadania pogody (rdzen 0).
void updateOnlineCounters() {
  if (!hasCountersUrl() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!_countersFetchNow && static_cast<int32_t>(millis() - _countersNextFetchAtMs) < 0) {
    return;
  }
  _countersFetchNow = false;
  fetchCounters();
  _countersNextFetchAtMs = millis()
    + (_countersLastStatus == HTTP_CODE_OK ? kCountersRefreshMs : kCountersRetryMs);
}
