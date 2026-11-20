# Statusline Claude Code -> panel "Claude" na zegarze LED.
#
# Claude Code uruchamia ten skrypt po kazdej odpowiedzi i podaje na wejsciu
# JSON z danymi sesji (model, kontekst, koszt, limity 5 h / 7 dni). Skrypt:
#  - wysyla je do zegara:  GET http://zegar-matrix.local/claude?...
#    (w tle, tylko gdy cos sie zmienilo albo minelo 60 s),
#  - wypisuje krotka linie statusu (widac ja w terminalu Claude Code).
#
# Limity 5 h / 7 dni Claude Code podaje tylko na kontach Pro/Max i dopiero
# po pierwszej odpowiedzi w sesji.
#
# W settings.json:
#   "statusLine": { "type": "command",
#     "command": "powershell -NoProfile -ExecutionPolicy Bypass -File C:/Users/<uzytkownik>/.claude/statusline_zegar.ps1" }
# (sciezka z ukosnikami /, bo Git Bash zjada backslashe)

param(
  [string]$Adres = "zegar-matrix.local"
)

$ErrorActionPreference = "SilentlyContinue"
$invariant = [Globalization.CultureInfo]::InvariantCulture

$data = [Console]::In.ReadToEnd() | ConvertFrom-Json
if ($null -eq $data) { exit 0 }

function Format-Number($value, [string]$format = "0.#") {
  return ([double]$value).ToString($format, $invariant)
}

$query = @()
$line = @()

# "Opus 5.5 (1M context)" -> "Opus 5.5"
$model = ($data.model.display_name -replace '\s*\(.*$', '').Trim()
if ($model) {
  $query += "m=" + [Uri]::EscapeDataString($model)
  $line += $model
}

$context = $data.context_window.used_percentage
if ($null -ne $context) {
  $query += "c=" + (Format-Number $context)
  $line += "kontekst " + [math]::Round([double]$context) + "%"
}

foreach ($window in @(
    @{ Name = "five_hour"; Arg = "h"; Label = "5h"; Reset = "HH:mm" },
    @{ Name = "seven_day"; Arg = "w"; Label = "7d"; Reset = "dd.MM HH:mm" })) {
  $limit = $data.rate_limits.($window.Name)
  if ($null -eq $limit -or $null -eq $limit.used_percentage) { continue }
  $query += $window.Arg + "=" + (Format-Number $limit.used_percentage)
  $text = "$($window.Label) " + [math]::Round([double]$limit.used_percentage) + "%"
  if ($limit.resets_at) {
    $query += $window.Arg + "r=" + [long]$limit.resets_at
    $reset = [DateTimeOffset]::FromUnixTimeSeconds([long]$limit.resets_at).LocalDateTime
    $text += " (reset " + $reset.ToString($window.Reset) + ")"
  }
  $line += $text
}

$cost = $data.cost.total_cost_usd
if ($null -ne $cost) {
  $query += "usd=" + (Format-Number $cost "0.00")
  $line += "$" + (Format-Number $cost "0.00")
}

# Wysylka do zegara: w tle (statusline musi szybko skonczyc) i bez powtarzania
# tych samych danych czesciej niz co 60 s.
if ($query.Count -gt 0) {
  $url = "http://$Adres/claude?" + ($query -join "&")
  $cache = Join-Path $env:TEMP "zegar_claude_statusline.txt"
  $previous = Get-Item $cache
  $same = $previous -and ((Get-Date) - $previous.LastWriteTime).TotalSeconds -lt 60 `
    -and ([System.IO.File]::ReadAllText($cache) -eq $url)
  if (-not $same) {
    [System.IO.File]::WriteAllText($cache, $url)
    Start-Process -FilePath "curl.exe" -ArgumentList @("-s", "-m", "3", "-o", "NUL", "`"$url`"") -WindowStyle Hidden
  }
}

[Console]::Out.WriteLine($line -join " | ")
