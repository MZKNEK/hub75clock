// Stan AI (Claude Code przez hooki) i automatyczne przelaczanie na panel Claude.
// Ustawianie:  GET http://<IP zegara>/ai?s=work | wait | done | idle
//              (albo /claude?s=... z hooka)
// Odczyt:      GET http://<IP zegara>/ai        -> {"state":"work"}
//
// Gdy Claude zaczyna pracowac (work / wait), zegar sam przechodzi z dowolnego
// trybu na panel Claude, a po kClaudeAutoReturnMs bez nowych zdarzen (i gdy
// juz nie pracuje) wraca do poprzedniego trybu. Panel Claude wlaczony recznie
// (przyciski, strona) zostaje - selectAnimation() kasuje
// _claudeAutoShown, wiec kazda reczna zmiana trybu konczy automat.
// Automat mozna wylaczyc na stronie (GET /ai?auto=0|1, Preferences "claude"
// "auto") - stan Claude dalej przychodzi, tylko tryb sie nie zmienia.
#include "zegar.h"
#include <Preferences.h>

AiState _aiState{AiState::Idle};
uint32_t _aiChangedAtMs{0};
static uint32_t _aiUpdatedAtMs{0};

static constexpr uint32_t kAiWorkTimeoutMs{30UL * 60UL * 1000UL};  // brak odswiezenia -> wolny
static constexpr uint32_t kAiWaitTimeoutMs{60UL * 60UL * 1000UL};
static constexpr uint32_t kAiDoneShowMs{20UL * 60UL * 1000UL};     // "GOTOWE" przez 20 min
static constexpr uint32_t kClaudeAutoReturnMs{20UL * 60UL * 1000UL};

static AnimationMode _modeBeforeClaude{AnimationMode::Clock};
static bool _claudeAutoEnabled{true};

void loadClaudeAutoSwitch() {
  Preferences preferences;
  if (preferences.begin("claude", true)) {
    _claudeAutoEnabled = preferences.getBool("auto", true);
    preferences.end();
  }
}

// Wylaczenie, gdy panel jest pokazany automatycznie, od razu wraca do poprzedniego trybu.
void setClaudeAutoSwitch(bool enabled) {
  _claudeAutoEnabled = enabled;
  Preferences preferences;
  if (preferences.begin("claude", false)) {
    preferences.putBool("auto", enabled);
    preferences.end();
  }
  if (!enabled && _claudeAutoShown) {
    selectAnimation(_modeBeforeClaude); // kasuje _claudeAutoShown
  }
}

bool isClaudeAutoSwitchEnabled() {
  return _claudeAutoEnabled;
}

static const char* const kAiStateNames[] {"idle", "work", "wait", "done"};

const char* getAiStateName() {
  return kAiStateNames[static_cast<uint8_t>(_aiState)];
}

static bool isAiBusy() {
  return _aiState == AiState::Working || _aiState == AiState::Waiting;
}

// Tryb nocny (brightness.cpp): Claude pracuje/czeka = ekran zostaje wlaczony.
bool isClaudeBusy() {
  expireAiState();
  return isAiBusy();
}

bool setAiStateByName(const String& name) {
  for (uint8_t state = 0; state < 4; state++) {
    if (name == kAiStateNames[state]) {
      AiState next = static_cast<AiState>(state);
      if (next != _aiState) {
        _aiChangedAtMs = millis();
      }
      _aiState = next;
      _aiUpdatedAtMs = millis();
      if (_claudeAutoEnabled && isAiBusy() && _animationMode != AnimationMode::Claude) {
        // Automat wraca jako Automat (kontynuuje liste), nie jako biezacy tryb z listy.
        _modeBeforeClaude = isShowRunning() ? AnimationMode::Show : _animationMode;
        selectAnimation(AnimationMode::Claude);
        _claudeAutoShown = true;
      }
      return true;
    }
  }
  return false;
}

void expireAiState() {
  uint32_t now = millis();
  if ((_aiState == AiState::Working && now - _aiUpdatedAtMs > kAiWorkTimeoutMs)
    || (_aiState == AiState::Waiting && now - _aiUpdatedAtMs > kAiWaitTimeoutMs)
    || (_aiState == AiState::Done && now - _aiChangedAtMs > kAiDoneShowMs)) {
    _aiState = AiState::Idle;
  }
}

// Wolane w loop(): powrot z automatycznie pokazanego panelu Claude.
void updateClaudeAutoReturn() {
  if (!_claudeAutoShown) {
    return;
  }
  expireAiState();
  if (!isAiBusy() && millis() - _aiUpdatedAtMs > kClaudeAutoReturnMs) {
    selectAnimation(_modeBeforeClaude); // kasuje _claudeAutoShown
  }
}
