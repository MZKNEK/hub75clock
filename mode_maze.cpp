// Tryb "Labirynt": labirynt buduje sie na oczach (DFS z losowym wyborem),
// potem swiecaca kropka przechodzi najkrotsza droge od wejscia (lewy gorny rog)
// do wyjscia (prawy dolny), zostawiajac slad. Po chwili nowy labirynt.
// Siatka 31x31 komorek (piksele 0-62, komorki na nieparzystych wspolrzednych).
// Bufory (ok. 8 KB) tylko w tym trybie - stopMazeMode.
#include "zegar.h"

static constexpr int16_t kMazeCells{31};
static constexpr int16_t kMazePixels{kMazeCells * 2 + 1}; // 63
static constexpr uint16_t kMazeCellCount{kMazeCells * kMazeCells};
static constexpr uint8_t kMazeCarvePerFrame{3};
static constexpr uint32_t kMazeFrameMs{20};
static constexpr uint32_t kMazeWalkStepMs{30};
static constexpr uint32_t kMazeHoldMs{4000};

// Stan piksela.
static constexpr uint8_t kMazeWall{0};
static constexpr uint8_t kMazePath{1};
static constexpr uint8_t kMazeTrail{2};

enum class MazePhase : uint8_t { Build, Walk, Hold };

static uint8_t* _mazeGrid{nullptr};       // kMazePixels^2, stan piksela
static uint16_t* _mazeStack{nullptr};     // DFS: komorki; potem droga (piksele)
static uint16_t* _mazeParent{nullptr};    // BFS: poprzednia komorka
static uint16_t _mazeStackSize{0};
static uint16_t _mazeRouteLength{0};
static uint16_t _mazeRouteStep{0};
static MazePhase _mazePhase{MazePhase::Build};
static int16_t _mazeRows{kMazeCells}; // komorek w pionie (mniej pod paskiem Automatu)
static uint32_t _mazeTickAtMs{0};

static uint16_t mazePixel(int16_t x, int16_t y) {
  return y * kMazePixels + x;
}

static void resetMaze() {
  memset(_mazeGrid, kMazeWall, kMazePixels * kMazePixels);
  _mazeGrid[mazePixel(1, 1)] = kMazePath;
  _mazeStack[0] = 0;
  _mazeStackSize = 1;
  _mazePhase = MazePhase::Build;
  _mazeTickAtMs = millis();
}

void startMazeMode() {
  if (_mazeGrid == nullptr) {
    _mazeGrid = static_cast<uint8_t*>(malloc(kMazePixels * kMazePixels));
    // DFS / kolejka BFS: do 961 komorek; droga: komorki + przejscia, do 1921 pikseli.
    _mazeStack = static_cast<uint16_t*>(malloc(2 * kMazeCellCount * sizeof(uint16_t)));
    _mazeParent = static_cast<uint16_t*>(malloc(kMazeCellCount * sizeof(uint16_t)));
  }
  if (_mazeGrid != nullptr && _mazeStack != nullptr && _mazeParent != nullptr) {
    resetMaze();
  }
}

void stopMazeMode() {
  free(_mazeGrid);
  free(_mazeStack);
  free(_mazeParent);
  _mazeGrid = nullptr;
  _mazeStack = nullptr;
  _mazeParent = nullptr;
}

static const int8_t kMazeDirections[4][2] {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

// Jeden krok DFS: z komorki na szczycie stosu do losowego nieodwiedzonego sasiada.
static bool carveMazeStep() {
  while (_mazeStackSize > 0) {
    uint16_t cell = _mazeStack[_mazeStackSize - 1];
    int16_t cx = cell % kMazeCells;
    int16_t cy = cell / kMazeCells;
    uint8_t options[4];
    uint8_t count = 0;
    for (uint8_t d = 0; d < 4; d++) {
      int16_t nx = cx + kMazeDirections[d][0];
      int16_t ny = cy + kMazeDirections[d][1];
      if (nx >= 0 && nx < kMazeCells && ny >= 0 && ny < _mazeRows
        && _mazeGrid[mazePixel(nx * 2 + 1, ny * 2 + 1)] == kMazeWall) {
        options[count++] = d;
      }
    }
    if (count == 0) {
      _mazeStackSize--; // slepy zaulek - cofamy sie
      continue;
    }
    uint8_t d = options[random(0, count)];
    int16_t nx = cx + kMazeDirections[d][0];
    int16_t ny = cy + kMazeDirections[d][1];
    _mazeGrid[mazePixel(cx * 2 + 1 + kMazeDirections[d][0], cy * 2 + 1 + kMazeDirections[d][1])] = kMazePath;
    _mazeGrid[mazePixel(nx * 2 + 1, ny * 2 + 1)] = kMazePath;
    _mazeStack[_mazeStackSize++] = ny * kMazeCells + nx;
    return true;
  }
  return false; // gotowe
}

// BFS od wejscia do wyjscia; droga (piksele, z przejsciami) w _mazeStack.
static void solveMaze() {
  for (uint16_t i = 0; i < kMazeCellCount; i++) {
    _mazeParent[i] = UINT16_MAX;
  }
  uint16_t* queue = _mazeStack; // stos juz pusty - uzywamy jako kolejki
  uint16_t head = 0;
  uint16_t tail = 0;
  queue[tail++] = 0;
  _mazeParent[0] = 0;
  uint16_t exitCell = (_mazeRows - 1) * kMazeCells + kMazeCells - 1; // prawy dolny rog
  while (head < tail && _mazeParent[exitCell] == UINT16_MAX) {
    uint16_t cell = queue[head++];
    int16_t cx = cell % kMazeCells;
    int16_t cy = cell / kMazeCells;
    for (uint8_t d = 0; d < 4; d++) {
      int16_t nx = cx + kMazeDirections[d][0];
      int16_t ny = cy + kMazeDirections[d][1];
      uint16_t next = ny * kMazeCells + nx;
      if (nx >= 0 && nx < kMazeCells && ny >= 0 && ny < _mazeRows
        && _mazeParent[next] == UINT16_MAX
        && _mazeGrid[mazePixel(cx * 2 + 1 + kMazeDirections[d][0],
          cy * 2 + 1 + kMazeDirections[d][1])] == kMazePath) {
        _mazeParent[next] = cell;
        queue[tail++] = next;
      }
    }
  }
  // Od wyjscia wstecz: komorki i przejscia miedzy nimi, potem odwrocenie.
  uint16_t length = 0;
  uint16_t cell = exitCell;
  for (;;) {
    int16_t cx = cell % kMazeCells;
    int16_t cy = cell / kMazeCells;
    _mazeStack[length++] = mazePixel(cx * 2 + 1, cy * 2 + 1);
    if (cell == 0) {
      break;
    }
    uint16_t parent = _mazeParent[cell];
    int16_t px = parent % kMazeCells;
    int16_t py = parent / kMazeCells;
    _mazeStack[length++] = mazePixel(cx + px + 1, cy + py + 1); // przejscie
    cell = parent;
  }
  for (uint16_t i = 0; i < length / 2; i++) {
    uint16_t swap = _mazeStack[i];
    _mazeStack[i] = _mazeStack[length - 1 - i];
    _mazeStack[length - 1 - i] = swap;
  }
  _mazeRouteLength = length;
  _mazeRouteStep = 0;
}

static void updateMaze(uint32_t now) {
  switch (_mazePhase) {
    case MazePhase::Build:
      if (now - _mazeTickAtMs >= kMazeFrameMs) {
        _mazeTickAtMs = now;
        for (uint8_t i = 0; i < kMazeCarvePerFrame; i++) {
          if (!carveMazeStep()) {
            solveMaze();
            _mazePhase = MazePhase::Walk;
            break;
          }
        }
      }
      break;
    case MazePhase::Walk:
      while (now - _mazeTickAtMs >= kMazeWalkStepMs && _mazeRouteStep < _mazeRouteLength) {
        _mazeTickAtMs += kMazeWalkStepMs;
        _mazeGrid[_mazeStack[_mazeRouteStep++]] = kMazeTrail;
      }
      if (_mazeRouteStep >= _mazeRouteLength) {
        _mazePhase = MazePhase::Hold;
        _mazeTickAtMs = now;
      }
      break;
    case MazePhase::Hold:
      if (now - _mazeTickAtMs >= kMazeHoldMs) {
        resetMaze();
      }
      break;
  }
}

void mazeAnimation(const AnimationParam& param) {
  if (_mazeGrid == nullptr || _mazeStack == nullptr || _mazeParent == nullptr) {
    startMazeMode(); // brak pamieci przy starcie - kolejna proba
  }
  leds.ClearTo(kBlack);
  if (_mazeGrid != nullptr && _mazeStack != nullptr && _mazeParent != nullptr) {
    int16_t rows = (getEffectRows() - 1) / 2;
    if (rows != _mazeRows) {
      _mazeRows = rows; // pasek Automatu zmniejszyl/zwolnil pole - nowy labirynt
      resetMaze();
    }
    uint32_t now = millis();
    updateMaze(now);
    RgbColor path = HslColor(0.6f, 1.0f, kLightness * 0.35f);
    RgbColor trail = HslColor(0.13f, 1.0f, kLightness * 0.8f);
    for (int16_t y = 0; y < kMazePixels; y++) {
      for (int16_t x = 0; x < kMazePixels; x++) {
        uint8_t state = _mazeGrid[mazePixel(x, y)];
        if (state != kMazeWall) {
          setDashPixel(x, y, state == kMazeTrail ? trail : path);
        }
      }
    }
    // Glowica: przy budowaniu szczyt stosu, przy przejsciu kropka na czele sladu.
    int16_t headPixel = -1;
    if (_mazePhase == MazePhase::Build && _mazeStackSize > 0) {
      uint16_t cell = _mazeStack[_mazeStackSize - 1];
      headPixel = mazePixel((cell % kMazeCells) * 2 + 1, (cell / kMazeCells) * 2 + 1);
    } else if (_mazePhase == MazePhase::Walk && _mazeRouteStep > 0) {
      headPixel = _mazeStack[_mazeRouteStep - 1];
    }
    if (headPixel >= 0) {
      setDashPixel(headPixel % kMazePixels, headPixel / kMazePixels,
        HslColor(_mazePhase == MazePhase::Build ? 0.5f : 0.13f, 1.0f, kLightness * 1.8f));
    }
  }
  applyModeTransition();

  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(0);
  }
}
