# Dodaje do Claude Code hooki, ktore pokazuja stan AI na zegarze LED.
#
# Uruchom w PowerShell (z folderu szkicu):
#   powershell -ExecutionPolicy Bypass -File narzedzia\dodaj_hooki_zegara.ps1
# albo z adresem IP, jesli nazwa .local nie dziala:
#   powershell -ExecutionPolicy Bypass -File narzedzia\dodaj_hooki_zegara.ps1 -Adres 192.168.1.50
#
# Skrypt: sprawdza, czy zegar odpowiada, robi kopie settings.json,
# usuwa ewentualne stare hooki zegara i dopisuje nowe. Inne ustawienia
# i inne hooki zostaja bez zmian. Ponowne uruchomienie niczego nie dubluje.
# Kopiuje tez statusline_zegar.ps1 do %USERPROFILE%\.claude i ustawia go jako
# statusLine (dane do panelu Claude: limity, kontekst, koszt) - chyba ze masz
# juz inny statusLine, wtedy go nie rusza.

param(
  [string]$Adres = "zegar-matrix.local",
  [switch]$BezSprawdzania
)

$ErrorActionPreference = "Stop"

function ConvertTo-Plain($value) {
  if ($null -eq $value) { return $null }
  if ($value -is [string] -or $value -is [ValueType]) { return $value }
  if ($value -is [System.Management.Automation.PSCustomObject]) {
    $table = [ordered]@{}
    foreach ($property in $value.PSObject.Properties) {
      $table[$property.Name] = ConvertTo-Plain $property.Value
    }
    return $table
  }
  if ($value -is [System.Collections.IEnumerable]) {
    $list = @()
    foreach ($item in $value) { $list += ,(ConvertTo-Plain $item) }
    return ,$list
  }
  return $value
}

function New-ClockHook([string]$state) {
  return [ordered]@{
    type    = "command"
    command = "curl.exe"
    args    = @("-s", "-m", "2", "http://$Adres/ai?s=$state")
    async   = $true
  }
}

# Start i koniec pracy: hook_zegar.ps1 wysyla stan + model + tokeny kontekstu
# (dziala tez w aplikacji desktop, ktora nie uruchamia statusline).
function New-ClockScriptHook([string]$state) {
  $hookArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
    (Join-Path $env:USERPROFILE ".claude\hook_zegar.ps1"), "-Stan", $state)
  if ($Adres -ne "zegar-matrix.local") { $hookArgs += @("-Adres", $Adres) }
  return [ordered]@{
    type    = "command"
    command = "powershell.exe"
    args    = $hookArgs
    async   = $true
  }
}

function Test-IsClockGroup($group) {
  if ($null -eq $group -or -not ($group -is [System.Collections.IDictionary])) { return $false }
  foreach ($hook in @($group["hooks"])) {
    if ($hook -is [System.Collections.IDictionary]) {
      $text = (@($hook["command"]) + @($hook["args"])) -join " "
      if ($text -match "/ai\?s=|hook_zegar\.ps1") { return $true }
    }
  }
  return $false
}

# 1. Czy zegar odpowiada?
if (-not $BezSprawdzania) {
  Write-Host "Sprawdzam zegar pod http://$Adres/ai ..."
  $answer = & curl.exe -s -m 4 "http://$Adres/ai"
  if ($LASTEXITCODE -ne 0 -or -not ($answer -match '"state"')) {
    Write-Host ""
    Write-Host "Zegar nie odpowiada pod adresem '$Adres'." -ForegroundColor Yellow
    Write-Host "Uruchom skrypt ponownie z adresem IP zegara, np.:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File narzedzia\dodaj_hooki_zegara.ps1 -Adres 192.168.1.50"
    exit 1
  }
  Write-Host "OK, zegar odpowiada: $answer" -ForegroundColor Green
}

# 2. Wczytaj ustawienia Claude Code (albo zacznij od pustych).
$folder = Join-Path $env:USERPROFILE ".claude"
$path = Join-Path $folder "settings.json"
if (-not (Test-Path $folder)) { New-Item -ItemType Directory -Path $folder | Out-Null }

$settings = [ordered]@{}
if (Test-Path $path) {
  $raw = [System.IO.File]::ReadAllText($path)
  if ($raw.Trim().Length -gt 0) {
    $settings = ConvertTo-Plain ($raw | ConvertFrom-Json)
  }
  $backup = "$path.kopia-" + (Get-Date -Format "yyyyMMdd-HHmmss")
  Copy-Item $path $backup
  Write-Host "Kopia zapasowa: $backup"
}

if (-not $settings.Contains("hooks") -or $null -eq $settings["hooks"]) {
  $settings["hooks"] = [ordered]@{}
}
$hooks = $settings["hooks"]

# 3. Nowe hooki zegara.
$wanted = [ordered]@{
  "UserPromptSubmit" = [ordered]@{ hooks = @(New-ClockScriptHook "work") }
  "PreToolUse"       = [ordered]@{ matcher = "*"; hooks = @(New-ClockHook "work") }
  "Notification"     = [ordered]@{ matcher = "permission_prompt"; hooks = @(New-ClockHook "wait") }
  "Stop"             = [ordered]@{ hooks = @(New-ClockScriptHook "done") }
}
Copy-Item (Join-Path $PSScriptRoot "hook_zegar.ps1") (Join-Path $folder "hook_zegar.ps1") -Force

foreach ($eventName in $wanted.Keys) {
  $groups = @()
  if ($hooks.Contains($eventName) -and $null -ne $hooks[$eventName]) {
    foreach ($group in @($hooks[$eventName])) {
      if (-not (Test-IsClockGroup $group)) { $groups += ,$group }
    }
  }
  $groups += ,$wanted[$eventName]
  $hooks[$eventName] = $groups
}

# 4. Statusline -> panel Claude na zegarze.
$statusSource = Join-Path $PSScriptRoot "statusline_zegar.ps1"
$statusTarget = Join-Path $folder "statusline_zegar.ps1"
Copy-Item $statusSource $statusTarget -Force
# Ukosniki /, bo Git Bash zjada backslashe w komendzie.
$statusCommand = "powershell -NoProfile -ExecutionPolicy Bypass -File " + ($statusTarget -replace '\\', '/')
if ($Adres -ne "zegar-matrix.local") { $statusCommand += " -Adres $Adres" }
$currentStatus = $settings["statusLine"]
if ($null -ne $currentStatus -and -not ("$($currentStatus["command"])" -match "statusline_zegar")) {
  Write-Host "Masz juz wlasny statusLine - zostawiam go, panel Claude nie dostanie limitow." -ForegroundColor Yellow
} else {
  $settings["statusLine"] = [ordered]@{ type = "command"; command = $statusCommand }
}

# 5. Zapis (UTF-8 bez BOM - tak jak oczekuje Claude Code).
$json = ConvertTo-Json -InputObject $settings -Depth 32
$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($path, $json, $utf8)

Write-Host ""
Write-Host "Gotowe. Hooki zegara dopisane do $path" -ForegroundColor Green
Write-Host "Zrestartuj Claude Code (albo otworz nowa sesje), zeby je wczytal."
Write-Host "Test recznie:  curl.exe http://$Adres/ai?s=work   (potem s=done)"
