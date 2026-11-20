// Google Apps Script: liczniki dla zegara LED - nieprzeczytane maile Gmail
// i liczba kanalow z subskrypcji YouTube, ktore teraz nadaja na zywo.
// Zegar sam pyta ten skrypt co 5 min (adres /exec wpisany na stronie zegara)
// i dostaje JSON: {"mail":2,"live":1,"live_names":"Kanal|Inny"}
// (live: null, gdy sprawdzanie YouTube nie dziala).
//
// MangaDex: od 2026-11 NIE tutaj. MangaDex odsyla zapytania z Apps Script na strone
// "bot challenge" (UrlFetchApp zawsze wysyla User-Agent "Mozilla/5.0 (compatible;
// Google-Apps-Script ...)", wlasnego nie da sie ustawic). Liczy to komputer:
// narzedzia/muzyka_zegar.ps1 (-UstawMangaDex) -> /manga?n= na zegarze. Wlasciwosci
// MANGADEX_* mozna tu skasowac (nieuzywane).
//
// YouTube (jednorazowo): w edytorze Uslugi (+) -> YouTube Data API v3 -> Dodaj,
// potem Uruchom -> wlaczSprawdzanieLive (zgoda na dostep do YouTube). To tworzy
// wyzwalacz: sprawdzLive co 5 min (wynik w pamieci podrecznej, doGet go czyta).
// Przy zgodzie wybierz KONTO GOOGLE (z adresem e-mail), nie kanal - wybor kanalu
// zapetla autoryzacje. Gdy subskrypcje sa na kanale marki: wlasciwosc YT_CHANNEL_ID.
// Wylaczanie YouTube: Uruchom -> wylaczSprawdzanieLive; dostep Google jak nizej.
//
// Wdrozenie: aplikacja internetowa, Wykonaj jako: Ja, Dostep: Kazdy.
// Po zmianie kodu: Wdroz -> Zarzadzaj wdrozeniami -> olowek -> Wersja: Nowa
// wersja -> Wdroz (adres /exec zostaje ten sam).
//
// Wlasciwosci skryptu (Ustawienia projektu, zebatka):
//   MAIL_EXTRA_URLS     (opcjonalnie) adresy /exec skryptow z INNYCH kont Gmail
//                       (narzedzia/zegar_gmail_dodatkowy.gs), rozdzielone przecinkami -
//                       ich maile sa dodawane do "mail" (na zegarze jedna liczba).
//   YT_CHANNEL_ID       (opcjonalnie) patrz liveChannels_.
//
// Wylaczanie: na stronie zegara "Usun adres" (sekcja Liczniki); tutaj Wdroz ->
// Zarzadzaj wdrozeniami -> Archiwizuj; myaccount.google.com/permissions -> Usun
// dostep.

// Co liczyc w Gmailu (wyszukiwanie jak w Gmailu). Tylko karta "Glowne":
//   'is:unread in:inbox category:primary'
const MAIL_QUERY = 'is:unread in:inbox';

function doGet() {
  const result = {mail: countAllMail(), live: null};
  const live = CacheService.getScriptCache().get('yt_live');
  if (live !== null) {
    const names = JSON.parse(live);
    result.live = names.length;
    // Dla zegara: bez cudzyslowow, backslashy i "|" (separator), max ~150 znakow.
    result.live_names = names.map(name => name.replace(/["\|\u0000-\u001f]/g, '').slice(0, 40))
      .join('|').slice(0, 150);
  }
  return ContentService.createTextOutput(JSON.stringify(result))
    .setMimeType(ContentService.MimeType.JSON);
}

// Nieprzeczytane WIADOMOSCI (nie watki): 3 maile w jednej rozmowie = 3.
// Zegar pokazuje najwyzej 99, wiec wiecej nie trzeba liczyc.
function countMail() {
  let count = 0;
  for (const thread of GmailApp.search(MAIL_QUERY, 0, 100)) {
    count += thread.getMessages().filter(message => message.isUnread()).length;
    if (count >= 99) break;
  }
  return count;
}

// Maile z tego konta + z kont dodatkowych (MAIL_EXTRA_URLS). Konto, ktore nie
// odpowiada, jest pomijane - liczba z pozostalych dalej trafia na zegar.
function countAllMail() {
  let total = countMail();
  const urls = (PropertiesService.getScriptProperties().getProperty('MAIL_EXTRA_URLS') || '')
    .split(',').map(url => url.trim()).filter(url => url);
  for (const url of urls) {
    try {
      const response = UrlFetchApp.fetch(url, {muteHttpExceptions: true});
      const mail = JSON.parse(response.getContentText()).mail;
      if (typeof mail === 'number') {
        total += mail;
      }
    } catch (error) {
      // nie odpowiada / nie JSON (np. strona logowania) - pomijamy
    }
  }
  return total;
}

// Do testu z edytora (Uruchom -> testCounters, wynik w Dzienniku wykonania).
function testCounters() {
  Logger.log('mail (to konto): ' + countMail());
  Logger.log('mail (razem z MAIL_EXTRA_URLS): ' + countAllMail());
}

// --- YouTube: ktore kanaly z subskrypcji nadaja teraz na zywo ---------------
// Lista subskrypcji (API, 1 jednostka / 50 kanalow) odswiezana co 6 h. Ostatnie
// filmy kanalow z kanalow RSS (bez limitu API, ale UrlFetch ma ok. 20 000 zapytan
// dziennie - odstep rosnie przy wielu subskrypcjach). Co 5 min jedno zapytanie
// videos.list na 50 filmow: liveBroadcastContent == 'live'. Trwajace i
// zaplanowane transmisje sa w RSS jak zwykle filmy. Nowy stream bez zapowiedzi
// pojawia sie w RSS po starcie - wtedy opoznienie do czasu odswiezenia RSS.
const LIVE_CHANNELS_SECONDS = 6 * 3600;
const LIVE_VIDEOS_PER_CHANNEL = 3;
const LIVE_RSS_FETCHES_PER_DAY = 10000; // polowa limitu UrlFetch na reszte

function liveChannels_() {
  const cache = CacheService.getScriptCache();
  const cached = cache.get('yt_channels');
  if (cached !== null) {
    return JSON.parse(cached);
  }
  // YT_CHANNEL_ID (Wlasciwosci skryptu, opcjonalnie): subskrypcje tego kanalu
  // zamiast "mine". Dla kanalu na koncie marki (Brand Account) - przy zgodzie
  // wybiera sie wtedy zwykle konto Google, nie kanal (wybor kanalu zapetla
  // autoryzacje Apps Script). Subskrypcje kanalu musza byc publiczne.
  const channelId = PropertiesService.getScriptProperties().getProperty('YT_CHANNEL_ID');
  const owner = channelId ? {channelId: channelId.trim()} : {mine: true};
  const channels = [];
  let pageToken;
  do {
    const page = YouTube.Subscriptions.list('snippet',
      Object.assign({maxResults: 50, pageToken: pageToken}, owner));
    (page.items || []).forEach(item => channels.push(item.snippet.resourceId.channelId));
    pageToken = page.nextPageToken;
  } while (pageToken);
  // Pustej listy nie zapamietujemy - po poprawce (np. YT_CHANNEL_ID) od razu nowa proba.
  if (channels.length > 0) {
    cache.put('yt_channels', JSON.stringify(channels), LIVE_CHANNELS_SECONDS);
  }
  return channels;
}

// Id ostatnich filmow wszystkich kanalow (z RSS), trzymane kilkanascie minut.
function liveCandidates_(channels) {
  const cache = CacheService.getScriptCache();
  const cached = cache.get('yt_candidates');
  if (cached !== null) {
    return JSON.parse(cached);
  }
  const ids = [];
  for (let i = 0; i < channels.length; i += 50) {
    const requests = channels.slice(i, i + 50).map(id => ({
      url: 'https://www.youtube.com/feeds/videos.xml?channel_id=' + id, muteHttpExceptions: true}));
    UrlFetchApp.fetchAll(requests).forEach(response => {
      if (response.getResponseCode() !== 200) return;
      const found = response.getContentText().match(/<yt:videoId>[\w-]{11}<\/yt:videoId>/g) || [];
      found.slice(0, LIVE_VIDEOS_PER_CHANNEL).forEach(tag => ids.push(tag.slice(12, 23)));
    });
  }
  const minutes = Math.max(15, Math.ceil(channels.length * 1440 / LIVE_RSS_FETCHES_PER_DAY));
  cache.put('yt_candidates', JSON.stringify(ids), minutes * 60);
  return ids;
}

// Wyzwalacz co 5 min. Wynik: nazwy kanalow na zywo (kazdy kanal raz).
function sprawdzLive() {
  const ids = liveCandidates_(liveChannels_());
  const live = {};
  for (let i = 0; i < ids.length; i += 50) {
    const page = YouTube.Videos.list('snippet', {id: ids.slice(i, i + 50).join(',')});
    (page.items || []).forEach(video => {
      if (video.snippet.liveBroadcastContent === 'live') {
        live[video.snippet.channelId] = video.snippet.channelTitle;
      }
    });
  }
  const names = Object.values(live);
  // 15 min: gdy wyzwalacz przestanie dzialac, zegar dostanie live: null i schowa liczbe.
  CacheService.getScriptCache().put('yt_live', JSON.stringify(names), 15 * 60);
  return names;
}

function wlaczSprawdzanieLive() {
  wylaczSprawdzanieLive();
  ScriptApp.newTrigger('sprawdzLive').timeBased().everyMinutes(5).create();
  const channelId = (PropertiesService.getScriptProperties().getProperty('YT_CHANNEL_ID') || '').trim();
  Logger.log(channelId ? 'kanal z YT_CHANNEL_ID: ' + channelId : 'kanal: zalogowane konto (mine)');
  const names = sprawdzLive();
  const count = liveChannels_().length;
  Logger.log('subskrypcji: ' + count + ', na zywo: ' + names.length
    + (names.length ? ' (' + names.join(', ') + ')' : ''));
  if (count === 0) {
    Logger.log('0 subskrypcji: YT_CHANNEL_ID musi byc identyfikatorem KANALU (UC..., nie'
      + ' "identyfikator uzytkownika"), a subskrypcje kanalu publiczne (YouTube ->'
      + ' Ustawienia -> Prywatnosc -> wylacz "Ukryj wszystkie moje subskrypcje").');
  }
}

function wylaczSprawdzanieLive() {
  ScriptApp.getProjectTriggers()
    .filter(trigger => trigger.getHandlerFunction() === 'sprawdzLive')
    .forEach(trigger => ScriptApp.deleteTrigger(trigger));
  CacheService.getScriptCache().removeAll(['yt_live', 'yt_channels', 'yt_candidates']);
}
