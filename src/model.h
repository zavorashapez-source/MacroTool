#pragma once
#include "util.h"
#include <utility>

// Virtual-key codes 14/15 are unassigned in Windows; the app uses them for the wheel.
constexpr int kWheelUp = 14;
constexpr int kWheelDown = 15;
// Pseudo key for relative mouse movement in recordings and the tester feed.
constexpr int kMouseMove = -1;

std::wstring KeyName(int vk);
bool KeyParse(std::wstring_view s, int& vk);
// Short human label ("XButton2" -> "Mouse 5").
std::wstring Pretty(std::wstring_view name);
std::wstring KeyLabel(std::wstring_view trigger);

struct Macro {
    int id = 0;  // runtime identity only, never saved
    std::wstring name = L"New Macro";
    std::wstring category;
    bool enabled = false;
    std::wstring trigger;
    std::wstring steps;
    std::wstring releaseSteps;
    int holdMs = 20;
    int stepDelay = 35;
    bool repeat = false;
    int repeatDelay = 30;
    bool blockTrigger = true;
    bool toggle = false;
};

using Binds = std::vector<std::pair<std::wstring, std::wstring>>;

struct CategoryInfo {
    const wchar_t* name;
    const wchar_t* glyph;
};
constexpr int kCategoryCount = 5;
extern const CategoryInfo kCategories[kCategoryCount];
bool IsCategory(std::wstring_view name);

struct Config {
    bool enabled = true;
    std::wstring toggleKey = L"F8";
    bool highPriority = true;
    bool toggleSound = true;
    std::wstring timing = L"FrameHuman";  // "Frame" (legacy) or "FrameHuman" (better)
    std::wstring pingHost;
    std::vector<std::wstring> appliedTweaks;
    std::wstring prevPowerScheme;
    std::wstring maxPowerScheme;
    std::vector<std::wstring> knownDefaults;
    std::wstring theme = L"Dark";
    bool animations = true;  // force UI animations on even if Windows "reduce motion" is set
    bool sidebarCollapsed = false;
    bool tutorialDone = false;
    bool onlyInGame = true;  // only fire while Fortnite is the focused window
    bool osd = true;         // show a brief on/off popup over the game when toggled
    bool menu = true;        // in-game click menu on menuKey
    std::wstring menuKey = L"Insert";
    bool editOnRelease = false;  // matches Fortnite's "Edit/Reset on release"; macros adapt
    // After an edit/build macro ends, if the screen centre still shows the blue edit
    // overlay the macro got stuck in edit mode, so tap the reset key to drop out.
    bool editGuard = false;
    std::wstring editResetKey = L"G";
    // Spaces out rapid edit/build fires so a spammed macro can't outrun the game and
    // jam in edit mode (prevention; works alongside editGuard's recovery).
    bool editKeyCheck = false;
    Binds binds;
    std::vector<Macro> macros;

    static Config Defaults();
    static std::wstring FilePath();
    static std::wstring ProfileDir();
    // Loads macros.json next to the exe. A corrupt file is moved to macros.json.bad and
    // `warning` explains it.
    static Config Load(std::wstring* warning);
    static bool LoadFile(const std::wstring& path, Config& out, std::wstring* err);
    bool SaveTo(const std::wstring& path) const;
    bool Save() const;

    // Gives category-less macros a home and adds binds introduced by newer versions.
    void Normalize();
    void AssignIds();
};

// Screenshot/preview runs must never touch the user's files.
void SetConfigReadOnly(bool readOnly);

std::string MacroToJson(const Macro& m);

struct Step {
    int key = 0;
    wchar_t kind = L't';  // t = tap, d = down, u = up, w = wait, m = mouse move
    int ms = 0;
    int dx = 0, dy = 0;
    bool operator==(const Step& o) const {
        return key == o.key && kind == o.kind && ms == o.ms && dx == o.dx && dy == o.dy;
    }
};

std::vector<Step> ParseSteps(std::wstring_view text, const Binds& binds);  // throws FormatError
int ParseKey(std::wstring name, const Binds& binds);  // throws FormatError
// With Fortnite's "Edit/Reset on release" the mouse release already finishes the edit, so the
// {Edit} tap a macro uses to confirm would reopen edit mode. Drops that tap from press + release.
void AdaptForEditOnRelease(std::vector<Step>& press, std::vector<Step>& release, int editVk);

struct InputEvent {
    int key = 0;  // kMouseMove for movement
    bool down = false;
    long long ms = 0;
    int dx = 0, dy = 0;
};

std::vector<InputEvent> RecorderClean(const std::vector<InputEvent>& raw);
std::wstring RecorderToSteps(const std::vector<InputEvent>& ev, const Binds& binds, int minWait = 5);

std::wstring ShareExport(const Macro& m);
Macro ShareImport(std::wstring code);  // throws FormatError

// A whole-setup share code: all macros plus the game binds, as one string.
struct MacroPack {
    std::vector<Macro> macros;
    Binds binds;
};
std::wstring PackExport(const std::vector<Macro>& macros, const Binds& binds);
MacroPack PackImport(std::wstring code);  // throws FormatError
bool IsPackCode(const std::wstring& code);

// Token ranges for syntax colouring in step editors.
struct StepToken {
    size_t start, len;
    int kind;  // 0 key, 1 bind, 2 wait, 3 down/up, 4 error, 5 mouse move
};
std::vector<StepToken> TokenizeSteps(std::wstring_view text, const Binds& binds);
