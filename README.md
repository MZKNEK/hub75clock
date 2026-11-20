# hub75clock

Zegar i pulpit na panelu LED 64x64 (HUB75, P2) sterowanym przez Adafruit
MatrixPortal ESP32-S3: godzina, pogoda, liczniki (Gmail, YouTube na żywo, MangaDex,
comix), „Teraz grane”, obciążenie komputera, kursy walut i animacje. Sterowanie
ze strony WWW zegara.

## Uruchomienie

1. Arduino IDE / arduino-cli, rdzeń **esp32 3.3.7**, płytka
   **Adafruit MatrixPortal ESP32-S3**, partycje *TinyUF2*, PSRAM włączony.
2. Biblioteka **ESP32-HUB75-MatrixPanel-DMA 3.1.0**.
3. Skopiuj `config.example.h` jako `config.h` i wpisz Wi-Fi, hasło OTA i położenie.
4. Wgraj. Potem już przez Wi-Fi (OTA, host `zegar-matrix`).

Strona sterowania: `http://zegar-matrix.local` (albo adres IP zegara).

## Narzędzia (`narzedzia/`)

- `muzyka_zegar.ps1` – Windows: „Teraz grane”, obciążenie PC, MangaDex i comix.
- `zegar_liczniki.gs` – Google Apps Script: liczniki Gmail i YouTube.
- `hook_zegar.ps1`, `settings_claude_code.example.json` – stan Claude Code na zegarze.
- `gif2g64.py` – GIF na format zegara.

Obudowa do druku 3D: `obudowa/`.

Licencja: MIT.
