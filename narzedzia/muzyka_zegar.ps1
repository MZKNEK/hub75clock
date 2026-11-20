# Ekran "Teraz grane" na zegarze: co 2 s czyta odtwarzacz Windows (to, co
# pokazuje panel glosnosci - Spotify, YouTube w przegladarce...) i wysyla na
# zegar wykonawce, tytul, pozycje i okladke:
#   GET  http://<zegar>/np?a=..&t=..&p=<ms>&d=<ms>&s=1|0   (zmiana, co 10 s, przewiniecie)
#   GET  http://<zegar>/np?off=1                          (nic nie gra)
#   POST http://<zegar>/cover   (okladka 48x48 RGB jako hex, przy zmianie utworu)
# Kazde /np ma tez &idle=<s> - ile sekund temu ruszano myszka/klawiatura
# (tryb nocny zegara: aktywny komputer = ekran zostaje wlaczony).
# Co 2 s takze obciazenie komputera (ekran "Komputer" na zegarze):
#   GET  http://<zegar>/pc?c=<CPU %>&g=<GPU %>&r=<RAM %>&ru=<RAM MB>&rt=<RAM MB>&v=<VRAM MB>&vt=<VRAM MB>
# (liczniki wydajnosci Windows; temperatur bez dodatkowych programow nie ma).
# Co 30 min nieprzeczytane rozdzialy obserwowanych mang MangaDex (gdy ustawione):
#   GET  http://<zegar>/manga?n=<liczba>   (ostatnia liczba powtarzana co 5 min)
# Liczy to komputer, bo od 2026-11 MangaDex odsyla zapytania z Google Apps Script
# (User-Agent "Mozilla/5.0 ... Google-Apps-Script", nie do zmiany) na strone
# "bot challenge". Tu idzie wlasny User-Agent klienta API, jak prosi MangaDex.
# Liczenie jak dawniej w zegar_liczniki.gs: feed obserwowanych z ostatnich N dni
# minus znaczniki przeczytania; jeden numer = jeden rozdzial; tylko wyzsze niz
# ostatni przeczytany.
# Ustawienie (raz, w zwyklym oknie PowerShell - pyta o login, haslo, client id/secret
# personal API client, liczy na probe i wysyla na zegar):
#   powershell -ExecutionPolicy Bypass -File narzedzia\muzyka_zegar.ps1 -UstawMangaDex
# Dane w %APPDATA%\zegar\mangadex.xml - haslo, sekret i refresh token zaszyfrowane
# DPAPI (odczyta je tylko Twoje konto Windows na tym komputerze). Test bez zmian:
# -TestMangaDex (lista nieprzeczytanych). Wylaczenie: -UsunMangaDex (kasuje plik).
# Co 30 min nieprzeczytane powiadomienia comix.to o rozdzialach -> GET /comix?n=
# (fioletowa liczba). Ciasteczka czytane za kazdym razem z Firefoksa (wystarczy
# byc tam zalogowanym, nic nie trzeba ustawiac). Test: -TestComix.
# Dziala w petli az do wylogowania; drugi egzemplarz od razu sie konczy.
# Dziennik (start, bledy, koniec): %TEMP%\muzyka_zegar.log (do ~200 KB, potem .old)
# - okno jest ukryte, wiec tylko tam widac, dlaczego skrypt sie zatrzymal.
#
# Wymaga Windows PowerShell 5.1 (powershell.exe; pwsh 7 nie ma klas WinRT).
# Test reczny (Ctrl+C konczy):
#   powershell -ExecutionPolicy Bypass -File narzedzia\muzyka_zegar.ps1 -Adres 192.168.1.121
# Uruchamianie po zalogowaniu: skrot w folderze Autostart (shell:startup), patrz CLAUDE.md,
# a do tego zadanie Harmonogramu "Zegar muzyka (straznik)" - przy logowaniu i co 5 min
# uruchamia skrypt, jesli padl (mutex konczy drugi egzemplarz od razu).
#
# Wylaczanie: usunac zadanie Harmonogramu "Zegar muzyka (straznik)"
# (Unregister-ScheduledTask -TaskName 'Zegar muzyka (straznik)'), skrot "Zegar muzyka"
# z folderu Autostart, potem zakonczyc proces (Menedzer zadan -> Windows PowerShell).

param(
  [string]$Adres = "zegar-matrix.local",
  [int]$OdstepMs = 2000,
  [int]$MangaMinuty = 30,
  [switch]$UstawMangaDex,
  [switch]$TestMangaDex,
  [switch]$UsunMangaDex,
  [switch]$TestComix
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue" # pasek postepu w PS 5.1 bardzo spowalnia pobieranie

$logPath = Join-Path $env:TEMP "muzyka_zegar.log"
function Write-Log([string]$message) {
  try {
    if ((Test-Path $logPath) -and (Get-Item $logPath).Length -gt 200KB) {
      Move-Item $logPath "$logPath.old" -Force
    }
    Add-Content -Path $logPath -Value "$(Get-Date -Format s) $message" -Encoding UTF8
  } catch { }
}
# Blad poza petla (np. przy starcie) - zapis i koniec.
trap {
  Write-Log "KONIEC (blad): $($_.Exception.Message) | $($_.InvocationInfo.PositionMessage)"
  break
}

# --- MangaDex: nieprzeczytane rozdzialy obserwowanych mang ---
$mangaConfigPath = Join-Path $env:APPDATA "zegar\mangadex.xml"
$mangaApi = "https://api.mangadex.org"
$mangaAuth = "https://auth.mangadex.org/realms/mangadex/protocol/openid-connect/token"
$mangaUserAgent = "zegar-liczniki/1.0 (PowerShell)"
$mangaReadBatch = 30

function ConvertTo-PlainText([Security.SecureString]$secure) {
  if (-not $secure) { return $null }
  (New-Object Management.Automation.PSCredential "x", $secure).GetNetworkCredential().Password
}

function Save-MangaConfig([hashtable]$config) {
  $folder = Split-Path $mangaConfigPath
  if (-not (Test-Path $folder)) { New-Item -ItemType Directory -Path $folder | Out-Null }
  $config | Export-Clixml -Path $mangaConfigPath # SecureString -> DPAPI
}

# Zapytanie do MangaDex; wynik z JSON-a. Bez podazania za przekierowaniem
# (302 na /edge/challenge = blokada botow - ma byc widac w bledzie).
function Invoke-MangaDex([string]$method, [string]$url, $body, [string]$token) {
  $headers = @{}
  if ($token) { $headers.Authorization = "Bearer $token" }
  $request = @{Uri = $url; Method = $method; Headers = $headers; UserAgent = $mangaUserAgent
    UseBasicParsing = $true; MaximumRedirection = 0; TimeoutSec = 30}
  if ($body) { $request.Body = $body }
  $path = ([Uri]$url).AbsolutePath
  try {
    $response = Invoke-WebRequest @request
  } catch {
    $failed = $_.Exception.Response
    $code = if ($failed) { [int]$failed.StatusCode } else { 0 }
    $location = if ($failed -and $failed.Headers["Location"]) { " -> " + $failed.Headers["Location"] } else { "" }
    $detail = if ($_.ErrorDetails.Message) { [string]$_.ErrorDetails.Message } else { $_.Exception.Message }
    throw "MangaDex ${path}: $code$location $($detail.Substring(0, [Math]::Min(200, $detail.Length)))"
  }
  if ($response.StatusCode -ne 200) {
    throw "MangaDex ${path}: $($response.StatusCode) -> $($response.Headers['Location'])"
  }
  $response.Content | ConvertFrom-Json
}

# Token dostepu: najpierw zapamietany refresh token, potem login i haslo.
function Get-MangaDexToken([hashtable]$config) {
  $client = @{client_id = $config.ClientId; client_secret = (ConvertTo-PlainText $config.ClientSecret)}
  $attempts = @()
  if ($config.RefreshToken) {
    $attempts += , (@{grant_type = "refresh_token"; refresh_token = (ConvertTo-PlainText $config.RefreshToken)} + $client)
  }
  $attempts += , (@{grant_type = "password"; username = $config.Username
    password = (ConvertTo-PlainText $config.Password)} + $client)
  $lastError = ""
  foreach ($body in $attempts) {
    try {
      $answer = Invoke-MangaDex "Post" $mangaAuth $body $null
      if ($answer.access_token) {
        $config.RefreshToken = ConvertTo-SecureString $answer.refresh_token -AsPlainText -Force
        Save-MangaConfig $config
        return $answer.access_token
      }
      $lastError = "logowanie ($($body.grant_type)): brak access_token"
    } catch {
      $lastError = "logowanie ($($body.grant_type)): $($_.Exception.Message)"
    }
  }
  throw $lastError
}

# Opis rozdzialu z feedu (z includes[]) do listy -TestMangaDex: tytul, numer, jezyk,
# grupy, uploader (z id - do porownania z blokadami), daty, strony, link.
function Format-MangaDexChapter($chapter) {
  $attributes = $chapter.attributes
  $relations = @($chapter.relationships)
  $manga = $relations | Where-Object { $_.type -eq "manga" } | Select-Object -First 1
  $title = $null
  if ($manga.attributes.title) {
    $titles = $manga.attributes.title.PSObject.Properties
    $title = ($titles | Where-Object { $_.Name -eq "en" } | Select-Object -First 1).Value
    if (-not $title) { $title = ($titles | Select-Object -First 1).Value }
  }
  if (-not $title) { $title = "manga $($manga.id)" }
  $groups = @($relations | Where-Object { $_.type -eq "scanlation_group" } |
    ForEach-Object { "$($_.attributes.name) [$($_.id)]" })
  $uploader = $relations | Where-Object { $_.type -eq "user" } | Select-Object -First 1
  $label = if ($attributes.chapter) { "rozdz. $($attributes.chapter)" } else { "oneshot" }
  # Nawiasy obowiazkowe: przecinek wiaze silniej niz "+".
  $lines = @(
    ("* $title - $label ($($attributes.translatedLanguage))" + $(if ($attributes.title) { ": $($attributes.title)" } else { "" })),
    "    grupy: $(if ($groups) { $groups -join ', ' } else { '(brak)' })",
    "    uploader: $($uploader.attributes.username) [$($uploader.id)]",
    ("    publishAt: $($attributes.publishAt)  readableAt: $($attributes.readableAt)  stron: $($attributes.pages)" +
      $(if ($attributes.externalUrl) { "  zewnetrzny: $($attributes.externalUrl)" } else { "" }) +
      $(if ($attributes.isUnavailable) { "  NIEDOSTEPNY" } else { "" })),
    "    https://mangadex.org/chapter/$($chapter.id)"
  )
  [string]::Join("`r`n", [string[]]$lines)
}

# Filtry z ustawien konta (te same, ktore stosuje strona mangadex.org; feed API sam
# ich nie bierze): userPreferences.groupBlacklist / userBlacklist (id), showSafe /
# showSuggestive / showErotic / showHentai -> contentRating[], filteredLanguages
# (jezyki tlumaczen; puste = te z -UstawMangaDex), originLanguages.
function Get-MangaDexFilters([string]$token) {
  $preferences = (Invoke-MangaDex "Get" "$mangaApi/settings" $null $token).settings.userPreferences
  if (-not $preferences) { throw "brak settings.userPreferences" }
  $idSet = {
    param($list)
    $set = @{}
    foreach ($entry in @($list)) {
      $id = if ($entry -is [string]) { $entry } else { [string]$entry.id }
      if ($id) { $set[$id] = $true }
    }
    $set
  }
  $ratings = @()
  if ($preferences.showSafe) { $ratings += "safe" }
  if ($preferences.showSuggestive) { $ratings += "suggestive" }
  if ($preferences.showErotic) { $ratings += "erotica" }
  if ($preferences.showHentai) { $ratings += "pornographic" }
  [pscustomobject]@{
    Groups = (& $idSet $preferences.groupBlacklist)
    Users = (& $idSet $preferences.userBlacklist)
    Ratings = $ratings
    Languages = @(@($preferences.filteredLanguages) | Where-Object { $_ -is [string] -and $_ -match '^[a-zA-Z\-]{2,5}$' })
    Origin = @(@($preferences.originLanguages) | Where-Object { $_ -is [string] -and $_ -match '^[a-zA-Z\-]{2,5}$' })
  }
}

# Liczba nieprzeczytanych; $details (lista, opcjonalnie) dostaje opis kazdego.
function Get-MangaDexUnread([hashtable]$config, $details) {
  $token = Get-MangaDexToken $config
  $languages = @(([string]$config.Languages -split ",") | ForEach-Object { $_.Trim() } | Where-Object { $_ })
  if ($languages.Count -eq 0) { $languages = @("en") }
  $days = [int]$config.Days
  if ($days -le 0) { $days = 14 }
  $filters = $null
  $filtersNote = ""
  try {
    $filters = Get-MangaDexFilters $token
    if ($filters.Languages.Count) { $languages = $filters.Languages }
  } catch {
    $filtersNote = " (BEZ filtrow konta: $($_.Exception.Message))"
  }

  # 1. Rozdzialy obserwowanych mang z ostatnich N dni.
  $since = [DateTime]::UtcNow.AddDays(-$days).ToString("yyyy-MM-ddTHH:mm:ss")
  $query = "limit=500&order%5BreadableAt%5D=desc&includeFutureUpdates=0&includeUnavailable=0&publishAtSince=" +
    [Uri]::EscapeDataString($since) +
    (($languages | ForEach-Object { "&translatedLanguage%5B%5D=" + [Uri]::EscapeDataString($_) }) -join "")
  if ($filters) {
    $query += (($filters.Ratings | ForEach-Object { "&contentRating%5B%5D=$_" }) -join "") +
      (($filters.Origin | ForEach-Object { "&originalLanguage%5B%5D=" + [Uri]::EscapeDataString($_) }) -join "")
  }
  if ($null -ne $details) {
    # Do listy: tytul mangi, nazwy grup i uploadera zamiast samych id.
    $query += "&includes%5B%5D=manga&includes%5B%5D=scanlation_group&includes%5B%5D=user"
  }
  $feed = Invoke-MangaDex "Get" "$mangaApi/user/follows/manga/feed?$query" $null $token
  $invariant = [Globalization.CultureInfo]::InvariantCulture
  $chapters = @(foreach ($chapter in $feed.data) {
    $manga = $chapter.relationships | Where-Object { $_.type -eq "manga" } | Select-Object -First 1
    $parsed = 0.0
    $number = $null
    if ([double]::TryParse([string]$chapter.attributes.chapter, [Globalization.NumberStyles]::Float,
        $invariant, [ref]$parsed)) {
      $number = $parsed
    }
    # Jak na stronie: rozdzial zablokowanej grupy (dowolnej z jego grup) albo uploadera
    # jest ukryty. Zostaje na liscie tylko do ustalenia, co przeczytane.
    $hidden = $false
    if ($filters) {
      foreach ($relation in $chapter.relationships) {
        if (($relation.type -eq "scanlation_group" -and $filters.Groups.ContainsKey([string]$relation.id)) -or
            ($relation.type -eq "user" -and $filters.Users.ContainsKey([string]$relation.id))) {
          $hidden = $true
        }
      }
    }
    [pscustomobject]@{Id = $chapter.id; Manga = [string]$manga.id; Number = $number
      Label = $chapter.attributes.chapter; PublishAt = [string]$chapter.attributes.publishAt
      Hidden = $hidden; Raw = $chapter}
  })
  if ($null -ne $details) {
    $hiddenCount = @($chapters | Where-Object { $_.Hidden }).Count
    $details.Add("dni: $days, jezyki: $($languages -join ','), rozdzialow w feedzie: $($chapters.Count)" +
      ", ukrytych przez zablokowane grupy/uploaderow: $hiddenCount$filtersNote")
    if ($filters) {
      $details.Add("filtry konta: zablokowane grupy $($filters.Groups.Count), uploaderzy $($filters.Users.Count)" +
        ", oceny: $($filters.Ratings -join ','), jezyk oryginalu: $(if ($filters.Origin.Count) { $filters.Origin -join ',' } else { 'dowolny' })")
    }
  }

  # 2. Znaczniki przeczytania, po 30 mang na zapytanie (dlugosc adresu).
  $read = @{}
  $mangaIds = @($chapters | ForEach-Object { $_.Manga } | Where-Object { $_ } | Select-Object -Unique)
  for ($i = 0; $i -lt $mangaIds.Count; $i += $mangaReadBatch) {
    $batch = $mangaIds[$i..([Math]::Min($i + $mangaReadBatch, $mangaIds.Count) - 1)]
    $readQuery = "grouped=true" + (($batch | ForEach-Object { "&ids%5B%5D=$_" }) -join "")
    $answer = Invoke-MangaDex "Get" "$mangaApi/manga/read?$readQuery" $null $token
    if ($answer.data -is [pscustomobject]) { # bez znacznikow przychodzi pusta tablica
      foreach ($entry in $answer.data.PSObject.Properties) {
        foreach ($id in $entry.Value) { $read[[string]$id] = $true }
      }
    }
  }

  # 3. Nieprzeczytane: unikalne numery rozdzialow, wyzsze niz ostatni przeczytany.
  $unread = 0
  foreach ($group in ($chapters | Group-Object Manga)) {
    $items = @($group.Group)
    $readNumbers = @($items | Where-Object { $read[$_.Id] -and $null -ne $_.Number } | ForEach-Object { $_.Number })
    $lastRead = if ($readNumbers.Count) { ($readNumbers | Measure-Object -Maximum).Maximum } else { $null }
    $seen = @{}
    foreach ($chapter in @($items | Where-Object { -not $_.Hidden })) {
      $key = if ($null -ne $chapter.Number) { "n" + $chapter.Number.ToString($invariant) } else { $chapter.Id }
      if ($seen[$key]) { continue }
      $seen[$key] = $true
      # Przeczytany w tlumaczeniu dowolnej grupy (oneshot bez numeru: po id).
      $alreadyRead = if ($null -ne $chapter.Number) {
        @($items | Where-Object { $_.Number -eq $chapter.Number -and $read[$_.Id] }).Count -gt 0
      } else { [bool]$read[$chapter.Id] }
      if ($alreadyRead) { continue }
      if ($null -ne $lastRead -and $null -ne $chapter.Number -and $chapter.Number -le $lastRead) { continue }
      $unread++
      if ($null -ne $details) {
        $label = if ($chapter.Label) { $chapter.Label } else { "oneshot" }
        $details.Add((Format-MangaDexChapter $chapter.Raw))
      }
    }
  }
  $unread
}

# --- comix.to: liczba nieprzeczytanych powiadomien (strona bez publicznego API) ---
# Zapytanie jak strona po zalogowaniu, z ciasteczkami comix.to brane PRZY KAZDYM
# odczycie z Firefoksa (cookies.sqlite w profilu - Firefox nie szyfruje ciasteczek),
# wiec po ponownym zalogowaniu w Firefoksie nic nie trzeba ustawiac. Czytane sa tylko
# wiersze comix.to (bez kontenerow), bez ciasteczek Cloudflare (cf_clearance itp.).
# Wlasny User-Agent - bez udawania przegladarki i bez obchodzenia Cloudflare.
# Przekierowanie / 401 = brak zalogowania w Firefoksie.
$comixSite = [Uri]"https://comix.to/"
$comixUnreadUrl = "https://comix.to/api/v1/user/notifications/unread-count"

# SQLite wbudowany w Windows (winsqlite3.dll) - bez instalowania czegokolwiek.
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class ZegarSqlite {
  [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.StdCall)]
  static extern int sqlite3_open_v2(byte[] filename, out IntPtr db, int flags, IntPtr vfs);
  [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.StdCall)]
  static extern int sqlite3_prepare_v2(IntPtr db, byte[] sql, int bytes, out IntPtr statement, IntPtr tail);
  [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.StdCall)]
  static extern int sqlite3_step(IntPtr statement);
  [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.StdCall)]
  static extern IntPtr sqlite3_column_text(IntPtr statement, int column);
  [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.StdCall)]
  static extern int sqlite3_column_bytes(IntPtr statement, int column);
  [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.StdCall)]
  static extern int sqlite3_finalize(IntPtr statement);
  [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.StdCall)]
  static extern int sqlite3_close(IntPtr db);
  [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.StdCall)]
  static extern IntPtr sqlite3_errmsg(IntPtr db);

  static string Text(IntPtr statement, int column) {
    IntPtr pointer = sqlite3_column_text(statement, column);
    if (pointer == IntPtr.Zero) return "";
    byte[] buffer = new byte[sqlite3_column_bytes(statement, column)];
    Marshal.Copy(pointer, buffer, 0, buffer.Length);
    return Encoding.UTF8.GetString(buffer);
  }

  // Wiersze (kolumny jako tekst) zapytania; flags 2 = SQLITE_OPEN_READWRITE (na kopii pliku).
  public static List<string[]> Query(string path, string sql, int columns) {
    IntPtr db;
    if (sqlite3_open_v2(Encoding.UTF8.GetBytes(path + "\0"), out db, 2, IntPtr.Zero) != 0) {
      sqlite3_close(db);
      throw new Exception("SQLite: nie mozna otworzyc " + path);
    }
    try {
      IntPtr statement;
      byte[] query = Encoding.UTF8.GetBytes(sql + "\0");
      if (sqlite3_prepare_v2(db, query, -1, out statement, IntPtr.Zero) != 0) {
        throw new Exception("SQLite: " + Marshal.PtrToStringAnsi(sqlite3_errmsg(db)));
      }
      var rows = new List<string[]>();
      try {
        while (sqlite3_step(statement) == 100) { // SQLITE_ROW
          var row = new string[columns];
          for (int i = 0; i < columns; i++) row[i] = Text(statement, i);
          rows.Add(row);
        }
      } finally {
        sqlite3_finalize(statement);
      }
      return rows;
    } finally {
      sqlite3_close(db);
    }
  }
}
"@

# Ciasteczka comix.to z najswiezszego profilu Firefoksa: lista @{Name; Value}.
# Baza jest otwarta przez Firefoksa, wiec czytamy kopie (z dziennikiem -wal, w ktorym
# sa najnowsze zmiany) w prywatnym folderze tymczasowym, kasowanym od razu.
function Get-FirefoxComixCookies {
  $database = Get-ChildItem (Join-Path $env:APPDATA "Mozilla\Firefox\Profiles") -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Get-Item (Join-Path $_.FullName "cookies.sqlite") -ErrorAction SilentlyContinue } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $database) { throw "nie znaleziono cookies.sqlite Firefoksa" }
  $folder = Join-Path $env:TEMP ("zegar_ff_" + [Guid]::NewGuid().ToString("N"))
  New-Item -ItemType Directory -Path $folder | Out-Null
  try {
    $copy = Join-Path $folder "cookies.sqlite"
    Copy-Item $database.FullName $copy
    if (Test-Path "$($database.FullName)-wal") { Copy-Item "$($database.FullName)-wal" "$copy-wal" }
    $rows = [ZegarSqlite]::Query($copy, "SELECT name, value FROM moz_cookies WHERE host IN " +
      "('comix.to', '.comix.to', 'www.comix.to', '.www.comix.to') AND originAttributes = ''", 2)
  } finally {
    Remove-Item -Recurse -Force $folder -ErrorAction SilentlyContinue
  }
  @($rows | Where-Object { $_[0] -notmatch '^(cf_|__cf|_cfuvid)' } |
    ForEach-Object { [pscustomobject]@{Name = $_[0]; Value = $_[1]} })
}

# Wynik: @{Count = liczba albo $null; Body = tekst odpowiedzi; Cookies = nazwy wyslanych}.
# Odpowiedz: {"status":"ok","result":{"all":N,"comics":N,"community":N}} - liczymy
# "comics" (nowe rozdzialy; "community" = komentarze/odpowiedzi, "all" = suma).
function Get-ComixUnread {
  $cookies = Get-FirefoxComixCookies
  if ($cookies.Count -eq 0) { throw "comix: brak ciasteczek comix.to w Firefoksie - zaloguj sie tam" }
  $session = New-Object Microsoft.PowerShell.Commands.WebRequestSession
  foreach ($cookie in $cookies) {
    $session.Cookies.Add($comixSite, (New-Object Net.Cookie($cookie.Name, $cookie.Value, "/")))
  }
  $request = @{Uri = $comixUnreadUrl; WebSession = $session; UserAgent = $mangaUserAgent
    Headers = @{Accept = "application/json"; Referer = "https://comix.to/"}
    UseBasicParsing = $true; MaximumRedirection = 0; TimeoutSec = 30}
  try {
    $response = Invoke-WebRequest @request
  } catch {
    $failed = $_.Exception.Response
    $code = if ($failed) { [int]$failed.StatusCode } else { 0 }
    $location = if ($failed -and $failed.Headers["Location"]) { " -> " + $failed.Headers["Location"] } else { "" }
    throw "comix: $code$location $($_.Exception.Message) (zalogowany w Firefoksie?)"
  }
  if ($response.StatusCode -ne 200) {
    throw "comix: $($response.StatusCode) -> $($response.Headers['Location']) (zalogowany w Firefoksie?)"
  }
  $json = try { $response.Content | ConvertFrom-Json } catch { $null }
  $count = $json.result.comics
  @{Count = $(if ($count -is [int] -or $count -is [long]) { [int]$count } else { $null })
    Body = [string]$response.Content; Cookies = @($cookies | ForEach-Object { $_.Name })}
}

if ($TestComix) {
  $result = Get-ComixUnread
  Write-Host "Ciasteczka z Firefoksa (same nazwy): $($result.Cookies -join ', ')"
  Write-Host "Odpowiedz: $($result.Body.Substring(0, [Math]::Min(500, $result.Body.Length)))"
  Write-Host "Nieprzeczytane powiadomienia comix (comics): $(if ($null -ne $result.Count) { $result.Count } else { 'brak result.comics w odpowiedzi' })"
  if ($null -ne $result.Count) {
    & curl.exe -s -m 5 -o NUL "http://$Adres/comix?n=$($result.Count)"
    Write-Host "Wyslane na zegar ($Adres)."
  }
  exit
}

if ($UsunMangaDex) {
  if (Test-Path $mangaConfigPath) { Remove-Item $mangaConfigPath }
  Write-Host "Usuniete dane MangaDex ($mangaConfigPath). Liczba zniknie z zegara po 6 h."
  exit
}
if ($UstawMangaDex) {
  Write-Host "Dane MangaDex (personal API client: mangadex.org -> Settings -> API Clients)."
  Write-Host "Haslo i sekret beda zaszyfrowane dla Twojego konta Windows."
  $username = Read-Host "Login MangaDex"
  $password = Read-Host "Haslo MangaDex" -AsSecureString
  $clientId = Read-Host "Client ID (personal-client-...)"
  $clientSecret = Read-Host "Client secret" -AsSecureString
  $languages = Read-Host "Jezyki rozdzialow, np. en albo en,pl [en]"
  $days = Read-Host "Rozdzialy z ostatnich ilu dni [14]"
  Save-MangaConfig @{Username = $username.Trim(); Password = $password; ClientId = $clientId.Trim()
    ClientSecret = $clientSecret; RefreshToken = $null
    Languages = $(if ($languages.Trim()) { $languages.Trim() } else { "en" })
    Days = $(if ($days -match '^\d+$') { [int]$days } else { 14 })}
  Write-Host "Zapisane w $mangaConfigPath. Test:"
}
if ($UstawMangaDex -or $TestMangaDex) {
  if (-not (Test-Path $mangaConfigPath)) { throw "Brak $mangaConfigPath - najpierw -UstawMangaDex" }
  $config = Import-Clixml $mangaConfigPath
  $details = New-Object Collections.Generic.List[string]
  $count = Get-MangaDexUnread $config $details
  $details | ForEach-Object { Write-Host $_ }
  Write-Host "Nieprzeczytane: $count"
  & curl.exe -s -m 5 -o NUL "http://$Adres/manga?n=$count"
  Write-Host "Wyslane na zegar ($Adres). Dzialajacy skrypt w tle liczy sam co $MangaMinuty min."
  exit
}

$mutex = New-Object Threading.Mutex($false, "Local\ZegarMuzyka")
if (-not $mutex.WaitOne(0)) {
  Write-Host "Skrypt juz dziala."
  exit
}

Add-Type -AssemblyName System.Runtime.WindowsRuntime
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class ZegarIdle {
  [StructLayout(LayoutKind.Sequential)]
  struct LastInputInfo { public uint Size; public uint Time; }
  [DllImport("user32.dll")]
  static extern bool GetLastInputInfo(ref LastInputInfo info);
  // Sekundy od ostatniego ruchu myszki / klawiatury.
  public static uint Seconds() {
    var info = new LastInputInfo();
    info.Size = (uint)Marshal.SizeOf(info);
    if (!GetLastInputInfo(ref info)) return 0;
    return ((uint)Environment.TickCount - info.Time) / 1000;
  }
}
"@
$null = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType = WindowsRuntime]
$null = [Windows.Storage.Streams.IRandomAccessStreamWithContentType, Windows.Storage.Streams, ContentType = WindowsRuntime]

# Obiekty WinRT w PowerShell 5.1 przychodza jako __ComObject - metody .NET,
# ktore je przyjmuja, wolamy przez refleksje (zwykle wywolanie nie dopasuje typu).
$asTaskGeneric = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
  $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
  $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
$asStreamForRead = [System.IO.WindowsRuntimeStreamExtensions].GetMethod('AsStreamForRead',
  [Type[]]@([Windows.Storage.Streams.IInputStream]))

function Await($operation, [Type]$type) {
  $task = $asTaskGeneric.MakeGenericMethod($type).Invoke($null, @($operation))
  $task.Wait(5000) | Out-Null
  $task.Result
}

# --- Obciazenie komputera (liczniki wydajnosci Windows) ---
$cpuCounter = New-Object Diagnostics.PerformanceCounter("Processor Information", "% Processor Utility", "_Total")
$null = $cpuCounter.NextValue()
$gpuCounters = @()          # silniki 3D wszystkich procesow (lista co 15 s od nowa)
$gpuCountersAt = [DateTime]::MinValue
$vramCounters = @((New-Object Diagnostics.PerformanceCounterCategory("GPU Adapter Memory")).GetInstanceNames() |
  ForEach-Object { New-Object Diagnostics.PerformanceCounter("GPU Adapter Memory", "Dedicated Usage", $_) })
# Rozmiar VRAM z rejestru (Win32_VideoController obcina do 4 GB) - najwiekszy adapter.
$vramTotalMb = ((Get-ItemProperty "HKLM:\SYSTEM\ControlSet001\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0*" -ErrorAction SilentlyContinue |
  ForEach-Object { $_."HardwareInformation.qwMemorySize" } | Measure-Object -Maximum).Maximum) / 1MB

function Get-PcStatsQuery {
  if (([DateTime]::UtcNow - $script:gpuCountersAt).TotalSeconds -ge 15) {
    $category = New-Object Diagnostics.PerformanceCounterCategory("GPU Engine")
    $script:gpuCounters = @($category.GetInstanceNames() | Where-Object { $_ -like "*engtype_3D" } |
      ForEach-Object { New-Object Diagnostics.PerformanceCounter("GPU Engine", "Utilization Percentage", $_) })
    $script:gpuCounters | ForEach-Object { $null = $_.NextValue() } # pierwszy odczyt zawsze 0
    $script:gpuCountersAt = [DateTime]::UtcNow
  }
  $cpu = [Math]::Round($cpuCounter.NextValue())
  $gpu = 0
  foreach ($counter in $script:gpuCounters) {
    try { $gpu += $counter.NextValue() } catch { } # proces juz sie zakonczyl
  }
  $gpu = [Math]::Min(100, [Math]::Round($gpu))
  $os = Get-CimInstance Win32_OperatingSystem
  $ramTotalMb = [Math]::Round($os.TotalVisibleMemorySize / 1KB)
  $ramUsedMb = $ramTotalMb - [Math]::Round($os.FreePhysicalMemory / 1KB)
  $ram = [Math]::Round(100 * $ramUsedMb / $ramTotalMb)
  $vramMb = [Math]::Round((($vramCounters | ForEach-Object { $_.NextValue() }) | Measure-Object -Maximum).Maximum / 1MB)
  "c=$cpu&g=$gpu&r=$ram&ru=$ramUsedMb&rt=$ramTotalMb&v=$vramMb&vt=$([Math]::Round($vramTotalMb))"
}

$coverSize = 48
$baseUrl = "http://$Adres"
Write-Log "Start (PID $PID, zegar $Adres)"
# Normalne zamkniecie (wylogowanie, Menedzer zadan "Zakoncz zadanie" tego nie zapisze).
Register-EngineEvent PowerShell.Exiting -MessageData $logPath -Action {
  Add-Content -Path $Event.MessageData -Value "$(Get-Date -Format s) Koniec (zamkniecie PowerShell)" -Encoding UTF8
} | Out-Null

# Okladka -> kwadrat ze srodka, 48x48, RGB jako hex. $null gdy brak.
function Get-CoverHex($properties) {
  if (-not $properties.Thumbnail) { return $null }
  $stream = Await ($properties.Thumbnail.OpenReadAsync()) ([Windows.Storage.Streams.IRandomAccessStreamWithContentType])
  $memory = New-Object IO.MemoryStream
  $asStreamForRead.Invoke($null, @($stream)).CopyTo($memory)
  $memory.Position = 0
  $source = New-Object System.Drawing.Bitmap ([IO.Stream]$memory)
  $side = [Math]::Min($source.Width, $source.Height)
  $small = New-Object System.Drawing.Bitmap $coverSize, $coverSize
  $graphics = [System.Drawing.Graphics]::FromImage($small)
  $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $graphics.DrawImage($source, (New-Object System.Drawing.Rectangle 0, 0, $coverSize, $coverSize),
    [int](($source.Width - $side) / 2), [int](($source.Height - $side) / 2), $side, $side,
    [System.Drawing.GraphicsUnit]::Pixel)
  $graphics.Dispose()
  $hex = New-Object Text.StringBuilder ($coverSize * $coverSize * 6)
  for ($y = 0; $y -lt $coverSize; $y++) {
    for ($x = 0; $x -lt $coverSize; $x++) {
      $pixel = $small.GetPixel($x, $y)
      [void]$hex.Append($pixel.R.ToString("x2")).Append($pixel.G.ToString("x2")).Append($pixel.B.ToString("x2"))
    }
  }
  $small.Dispose()
  $source.Dispose()
  return $hex.ToString()
}

function Send-Get([string]$url) {
  & curl.exe -s -m 3 -o NUL $url
}

$manager = $null
$lastKey = $null          # "wykonawca|tytul|gra" ostatnio wyslane
$lastSentAt = [DateTime]::MinValue
$lastSentPositionMs = 0
$lastCover = $null        # ostatnio wyslana okladka (hex)
$trackChangedAt = [DateTime]::MinValue
$mangaNextAt = [DateTime]::MinValue     # kiedy liczyc MangaDex od nowa
$mangaCount = $null                     # ostatni wynik (powtarzany co 5 min - np. po restarcie zegara)
$mangaSentAt = [DateTime]::MinValue

# MangaDex co $MangaMinuty min (blad: ponowna proba za 10 min). Liczenie trwa
# kilka sekund - wtedy jedno odswiezenie "Teraz grane" przychodzi pozniej.
function Update-MangaDex {
  $now = [DateTime]::UtcNow
  if ((Test-Path $mangaConfigPath) -and $now -ge $script:mangaNextAt) {
    $script:mangaNextAt = $now.AddMinutes($MangaMinuty)
    try {
      $count = Get-MangaDexUnread (Import-Clixml $mangaConfigPath) $null
      if ($count -ne $script:mangaCount) { Write-Log "MangaDex: $count" }
      $script:mangaCount = $count
      $script:mangaSentAt = [DateTime]::MinValue
    } catch {
      Write-Log "Blad (MangaDex): $($_.Exception.Message)"
      $script:mangaNextAt = $now.AddMinutes(10)
    }
  }
  if ($null -ne $script:mangaCount -and ($now - $script:mangaSentAt).TotalMinutes -ge 5) {
    Send-Get "$baseUrl/manga?n=$($script:mangaCount)"
    $script:mangaSentAt = $now
  }
}

$comixNextAt = [DateTime]::MinValue
$comixCount = $null
$comixSentAt = [DateTime]::MinValue
$comixLastError = ""

# comix.to co $MangaMinuty min, jak MangaDex (ciasteczka z Firefoksa przy kazdym odczycie). Blad (np. brak
# zalogowania) zapisywany do dziennika tylko gdy inny niz poprzedni; liczba przestaje
# byc wysylana i znika z zegara po 6 h.
function Update-Comix {
  $now = [DateTime]::UtcNow
  if ($now -ge $script:comixNextAt) {
    $script:comixNextAt = $now.AddMinutes($MangaMinuty)
    try {
      $count = (Get-ComixUnread).Count
      if ($null -eq $count) { throw "comix: brak result.comics w odpowiedzi" }
      if ($count -ne $script:comixCount) { Write-Log "comix: $count" }
      $script:comixCount = $count
      $script:comixSentAt = [DateTime]::MinValue
      $script:comixLastError = ""
    } catch {
      $message = $_.Exception.Message
      if ($message -ne $script:comixLastError) { Write-Log "Blad (comix): $message" }
      $script:comixLastError = $message
      $script:comixCount = $null
    }
  }
  if ($null -ne $script:comixCount -and ($now - $script:comixSentAt).TotalMinutes -ge 5) {
    Send-Get "$baseUrl/comix?n=$($script:comixCount)"
    $script:comixSentAt = $now
  }
}

while ($true) {
  try {
    if (-not $manager) {
      $manager = Await ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager])
    }
    $now = [DateTime]::UtcNow
    $session = $manager.GetCurrentSession()
    $status = if ($session) { [string]$session.GetPlaybackInfo().PlaybackStatus } else { "" }

    if ($status -ne "Playing" -and $status -ne "Paused") {
      if ($lastKey -ne "off" -or ($now - $lastSentAt).TotalSeconds -ge 10) {
        Send-Get "$baseUrl/np?off=1&idle=$([ZegarIdle]::Seconds())"
        if ($lastKey -ne "off") { Write-Host "Nic nie gra" }
        $lastKey = "off"
        $lastSentAt = $now
      }
    } else {
      $properties = Await ($session.TryGetMediaPropertiesAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties])
      $playing = $status -eq "Playing"
      $timeline = $session.GetTimelineProperties()
      $positionMs = $timeline.Position.TotalMilliseconds
      if ($playing) {
        # Pozycja jest z chwili ostatniej aktualizacji odtwarzacza.
        $positionMs += ([DateTimeOffset]::UtcNow - $timeline.LastUpdatedTime).TotalMilliseconds
      }
      $durationMs = ($timeline.EndTime - $timeline.StartTime).TotalMilliseconds
      if ($durationMs -lt 0) { $durationMs = 0 }
      $positionMs = [Math]::Max(0, [Math]::Min($positionMs, $durationMs))

      $track = "$($properties.Artist)|$($properties.Title)"
      $key = "$track|$playing"
      if (-not $lastKey -or -not $lastKey.StartsWith("$track|")) {
        $trackChangedAt = $now
      }
      # Gdzie powinna byc pozycja wedlug ostatniej wysylki (wykrywa przewiniecie).
      $expectedMs = $lastSentPositionMs + $(if ($playing) { ($now - $lastSentAt).TotalMilliseconds } else { 0 })
      if ($key -ne $lastKey -or ($now - $lastSentAt).TotalSeconds -ge 10 -or
          [Math]::Abs($positionMs - $expectedMs) -gt 3000) {
        $url = "$baseUrl/np?a=" + [Uri]::EscapeDataString([string]$properties.Artist) +
          "&t=" + [Uri]::EscapeDataString([string]$properties.Title) +
          "&p=" + [long]$positionMs + "&d=" + [long]$durationMs + "&s=" + $(if ($playing) { 1 } else { 0 }) +
          "&idle=" + [ZegarIdle]::Seconds()
        Send-Get $url
        if ($key -ne $lastKey) {
          Write-Host "$(if ($playing) { 'Gra' } else { 'Pauza' }): $($properties.Artist) - $($properties.Title)"
          if ($track -ne ($lastKey -replace '\|(True|False)$', '')) {
            $lastCover = $null # zegar skasowal okladke przy zmianie utworu
          }
        }
        $lastKey = $key
        $lastSentAt = $now
        $lastSentPositionMs = $positionMs
      }

      # Okladka: odtwarzacz potrafi podac nowa chwile po tytule, wiec przez
      # 15 s od zmiany utworu sprawdzamy, czy sie nie zmienila.
      if (-not $lastCover -or ($now - $trackChangedAt).TotalSeconds -lt 15) {
        $cover = Get-CoverHex $properties
        if ($cover -and $cover -ne $lastCover) {
          Invoke-WebRequest -Uri "$baseUrl/cover" -Method Post -Body $cover -ContentType "text/plain" `
            -UseBasicParsing -TimeoutSec 5 | Out-Null
          $lastCover = $cover
        }
      }
    }
  } catch {
    Write-Host "Blad: $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Log "Blad: $($_.Exception.Message)"
    $manager = $null
    $lastCover = $null
  }
  try {
    Send-Get "$baseUrl/pc?$(Get-PcStatsQuery)"
  } catch {
    Write-Host "Blad (obciazenie): $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Log "Blad (obciazenie): $($_.Exception.Message)"
  }
  try {
    Update-MangaDex
  } catch {
    Write-Log "Blad (MangaDex): $($_.Exception.Message)"
  }
  try {
    Update-Comix
  } catch {
    Write-Log "Blad (comix): $($_.Exception.Message)"
  }
  Start-Sleep -Milliseconds $OdstepMs
}
