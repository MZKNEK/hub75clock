// Google Apps Script dla DRUGIEGO (kolejnego) konta Gmail: samo liczenie
// nieprzeczytanych maili, bez MangaDex. Zwraca JSON {"mail":N}.
// Zegar tego adresu NIE pyta bezposrednio - pyta go skrypt glownego konta
// (narzedzia/zegar_liczniki.gs) i dodaje liczbe do swojej.
//
// Wdrozenie (na drugim koncie Google): script.google.com -> Nowy projekt,
// wkleic ten plik, Wdroz -> Nowe wdrozenie -> Aplikacja internetowa,
// Wykonaj jako: Ja, Dostep: Kazdy -> Autoryzuj dostep. Adres /exec wpisac
// na GLOWNYM koncie: Ustawienia projektu -> Wlasciwosci skryptu ->
// MAIL_EXTRA_URLS (kilka kont: adresy rozdzielone przecinkami).
// Po zmianie kodu: Wdroz -> Zarzadzaj wdrozeniami -> olowek -> Wersja: Nowa
// wersja -> Wdroz (adres /exec zostaje ten sam).
//
// Wylaczanie: usunac adres z MAIL_EXTRA_URLS na glownym koncie; tutaj Wdroz ->
// Zarzadzaj wdrozeniami -> Archiwizuj; myaccount.google.com/permissions (na tym
// koncie) -> Usun dostep.

// Co liczyc (wyszukiwanie jak w Gmailu) - tak samo jak na glownym koncie.
// Tylko karta "Glowne": 'is:unread in:inbox category:primary'
const MAIL_QUERY = 'is:unread in:inbox';

function doGet() {
  return ContentService.createTextOutput(JSON.stringify({mail: countMail()}))
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

// Do testu z edytora (Uruchom -> testMail, wynik w Dzienniku wykonania).
function testMail() {
  Logger.log('mail: ' + countMail());
}

// Diagnostyka (Uruchom -> diagnozaMaili): konto, na ktorym dziala skrypt, i WSZYSTKIE
// nieprzeczytane wiadomosci (do 30 watkow) z informacja, czy sa w Odebranych.
// Wynik tylko w Twoim Dzienniku wykonania.
function diagnozaMaili() {
  Logger.log('konto: ' + Session.getActiveUser().getEmail());
  Logger.log('zapytanie zegara: ' + MAIL_QUERY + ' -> ' + countMail());
  for (const thread of GmailApp.search('is:unread', 0, 30)) {
    const where = thread.isInInbox() ? 'ODEBRANE' : (thread.isInSpam() ? 'SPAM'
      : 'poza Odebranymi (' + thread.getLabels().map(label => label.getName()).join(', ') + ')');
    for (const message of thread.getMessages().filter(m => m.isUnread())) {
      Logger.log(where + ' | ' + message.getDate().toLocaleString() + ' | ' + message.getSubject());
    }
  }
}
