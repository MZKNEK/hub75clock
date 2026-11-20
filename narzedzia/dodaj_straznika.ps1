# Rejestruje zadanie Harmonogramu "Zegar muzyka (straznik)": przy logowaniu i potem
# co 5 min uruchamia muzyka_zegar.ps1 w ukrytym oknie (conhost --headless). Gdy skrypt
# juz dziala, nowy egzemplarz konczy sie od razu (mutex w muzyka_zegar.ps1), wiec
# zadanie tylko podnosi skrypt, ktory padl. Bez uprawnien administratora (zadanie
# Twojego konta). Uruchomienie ponownie zastepuje zadanie (np. po reinstalacji Windows):
#   powershell -ExecutionPolicy Bypass -File narzedzia\dodaj_straznika.ps1
# Usuniecie:
#   Unregister-ScheduledTask -TaskName 'Zegar muzyka (straznik)' -Confirm:$false

param(
  [string]$Adres = "192.168.1.121"
)

$ErrorActionPreference = "Stop"
$taskName = "Zegar muzyka (straznik)"
$script = Join-Path $PSScriptRoot "muzyka_zegar.ps1"
$user = "$env:USERDOMAIN\$env:USERNAME"

$action = New-ScheduledTaskAction -Execute (Join-Path $env:WINDIR "System32\conhost.exe") `
  -Argument "--headless powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$script`" -Adres $Adres"
$atLogon = New-ScheduledTaskTrigger -AtLogOn -User $user
# Co 5 min bez konca (bez -RepetitionDuration = bezterminowo).
$every5 = New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Minutes 5)
$settings = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) `
  -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $atLogon, $every5 `
  -Settings $settings -Principal $principal -Force | Out-Null
Write-Host "Zadanie '$taskName' zarejestrowane: przy logowaniu i co 5 min -> $script"
