# Hook Claude Code -> zegar LED: stan pracy + dane do panelu "Claude".
#
# Wolany na poczatku pracy (UserPromptSubmit, -Stan work) i na koncu (Stop,
# -Stan done). Dziala tez w aplikacji desktop, ktora nie uruchamia statusline.
# Z hooka Claude Code dostaje sciezke zapisu rozmowy (transcript_path);
# z ostatniej odpowiedzi modelu bierzemy nazwe modelu i liczbe tokenow
# kontekstu. Limitow 5 h / 7 dni w zapisie rozmowy nie ma - po nie skrypt
# pyta NIEOFICJALNY endpoint uzycia (ten sam co /usage w Claude Code) tokenem
# logowania z %USERPROFILE%\.claude\.credentials.json. Aplikacja desktop tego
# pliku nie uzywa ani nie odswieza (loguje sie po swojemu); odswieza go tylko
# terminalowy `claude` (%USERPROFILE%\.local\bin\claude.exe) - skrypt woła
# `claude auth status`, gdy token wygasl. Bez terminalowego claude (albo po
# wygasnieciu refresh tokenu - wtedy `claude` -> /login) limity sa pomijane.
# Odpowiedz (bez sekretow) jest w %TEMP%\zegar_claude_limity.json, max co 2 min.
#
# Wysyla jedno zapytanie:
#   GET http://zegar-matrix.local/claude?s=work&m=Opus%205.5&ct=216703&h=16&hr=..&w=45&wr=..
#
# W settings.json (forma exec - Git Bash nie zje backslashy):
#   { "type": "command", "command": "powershell.exe", "async": true,
#     "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
#              "C:\\Users\\<uzytkownik>\\.claude\\hook_zegar.ps1", "-Stan", "work"] }
#
# Wylaczanie: usunac oba wpisy z hook_zegar.ps1 (UserPromptSubmit i Stop)
# z %USERPROFILE%\.claude\settings.json i zrestartowac Claude Code; potem
# mozna skasowac %USERPROFILE%\.claude\hook_zegar.ps1. Zegar przestaje sie
# wtedy sam przelaczac na panel Claude.

param(
  [ValidateSet("work", "wait", "done", "idle")]
  [string]$Stan = "work",
  [string]$Adres = "zegar-matrix.local"
)

$ErrorActionPreference = "SilentlyContinue"

$query = @("s=$Stan")
$hook = [Console]::In.ReadToEnd() | ConvertFrom-Json
$transcript = $hook.transcript_path

if ($transcript -and (Test-Path $transcript)) {
  # Zapis rozmowy bywa duzy - wystarczy jego koncowka: ostatnie 4 MB jako bajty.
  # NIE Get-Content -Tail: w PowerShell 5.1 przy liniach po kilkaset KB (wyniki
  # narzedzi) -Tail 300 trwal wiele minut na pelnym rdzeniu (2026-10-31, zapis
  # 9,7 MB) - hooki sie nakladaly, a na zegar nic nie dochodzilo.
  # Pierwsza linia jest zwykle ucieta - nie przejdzie ConvertFrom-Json, pomijana.
  $stream = [IO.File]::Open($transcript, "Open", "Read", "ReadWrite")
  $size = [int][Math]::Min($stream.Length, 4MB)
  [void]$stream.Seek(-$size, "End")
  $buffer = New-Object byte[] $size
  $read = 0
  while ($read -lt $size) {
    $count = $stream.Read($buffer, $read, $size - $read)
    if ($count -le 0) { break }
    $read += $count
  }
  $stream.Dispose()
  $lines = [Text.Encoding]::UTF8.GetString($buffer, 0, $read) -split "`n"
  for ($i = $lines.Count - 1; $i -ge 0; $i--) {
    if ($lines[$i] -notmatch '"type":"assistant"') { continue }
    $entry = $lines[$i] | ConvertFrom-Json
    $usage = $entry.message.usage
    if ($null -eq $usage -or $entry.isSidechain) { continue }

    # "claude-opus-5-5[1m]" -> "Opus 5.5"; daty w nazwie (20261001) pomijamy.
    $parts = ($entry.message.model -replace '\[.*\]$', '') -split '-' |
      Where-Object { $_ -and $_ -ne "claude" -and $_ -notmatch '^\d{8}$' }
    if ($parts.Count -gt 0) {
      $name = $parts[0].Substring(0, 1).ToUpper() + $parts[0].Substring(1)
      if ($parts.Count -gt 1) { $name += " " + ($parts[1..($parts.Count - 1)] -join ".") }
      $query += "m=" + [Uri]::EscapeDataString($name)
    }

    $tokens = [long]$usage.input_tokens + [long]$usage.cache_creation_input_tokens `
      + [long]$usage.cache_read_input_tokens
    if ($tokens -gt 0) { $query += "ct=$tokens" }
    break
  }
}

function Get-ClaudeUsage {
  $cache = Join-Path $env:TEMP "zegar_claude_limity.json"
  $cached = Get-Item $cache
  if ($cached -and ((Get-Date) - $cached.LastWriteTime).TotalSeconds -lt 120) {
    return [System.IO.File]::ReadAllText($cache) | ConvertFrom-Json
  }
  $credentialsFile = Join-Path $env:USERPROFILE ".claude\.credentials.json"
  $credentials = Get-Content $credentialsFile -Raw | ConvertFrom-Json
  $oauth = $credentials.claudeAiOauth
  if (-not $oauth.accessToken) { return $null }
  $nowMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
  if ($oauth.expiresAt -and [long]$oauth.expiresAt -le $nowMs + 300000) {
    # Wygasl (albo zaraz wygasnie): `claude auth status` sam odswieza token
    # w tym pliku. Wypis (z e-mailem) wyrzucamy.
    # claude.exe: instalator skryptem (.local\bin), winget albo cokolwiek w PATH.
    $cli = @(
      (Join-Path $env:USERPROFILE ".local\bin\claude.exe"),
      (Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\Anthropic.ClaudeCode_Microsoft.Winget.Source_8wekyb3d8bbwe\claude.exe"),
      (Get-Command claude.exe -ErrorAction SilentlyContinue).Source
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if ($cli) { & $cli auth status *> $null }
    $oauth = (Get-Content $credentialsFile -Raw | ConvertFrom-Json).claudeAiOauth
    if ($oauth.expiresAt -and [long]$oauth.expiresAt -le $nowMs) { return $null }
  }

  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  $usage = Invoke-RestMethod -Uri "https://api.anthropic.com/api/oauth/usage" -TimeoutSec 5 `
    -Headers @{ Authorization = "Bearer " + $oauth.accessToken; "anthropic-beta" = "oauth-2025-04-20" }
  if ($null -eq $usage) { return $null }
  $json = $usage | Select-Object five_hour, seven_day | ConvertTo-Json -Depth 3 -Compress
  [System.IO.File]::WriteAllText($cache, $json)
  return $json | ConvertFrom-Json
}

$usage = Get-ClaudeUsage
foreach ($window in @(@{ Name = "five_hour"; Arg = "h" }, @{ Name = "seven_day"; Arg = "w" })) {
  $limit = $usage.($window.Name)
  if ($null -eq $limit -or $null -eq $limit.utilization) { continue }
  $query += $window.Arg + "=" + ([double]$limit.utilization).ToString("0.#", [Globalization.CultureInfo]::InvariantCulture)
  if ($limit.resets_at) {
    $query += $window.Arg + "r=" + [DateTimeOffset]::Parse($limit.resets_at, [Globalization.CultureInfo]::InvariantCulture).ToUnixTimeSeconds()
  }
}

& curl.exe -s -m 3 -o NUL ("http://$Adres/claude?" + ($query -join "&"))
