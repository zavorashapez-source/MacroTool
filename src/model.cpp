#include "model.h"
#include "deflate.h"
#include "json.h"
#include <algorithm>
#include <cmath>
#include <cwctype>

// ---------------------------------------------------------------- keys

namespace {

struct KeyDef {
    int vk;
    std::wstring name;
};

// Names follow System.Windows.Forms.Keys so configs and share codes from the .NET
// version keep working. The first entry for a code is the one written out.
const std::vector<KeyDef>& KeyTable() {
    static const std::vector<KeyDef> table = [] {
        std::vector<KeyDef> t = {
            {1, L"LButton"}, {2, L"RButton"}, {3, L"Cancel"}, {4, L"MButton"}, {5, L"XButton1"}, {6, L"XButton2"},
            {8, L"Back"}, {9, L"Tab"}, {10, L"LineFeed"}, {12, L"Clear"}, {13, L"Return"}, {13, L"Enter"},
            {16, L"ShiftKey"}, {17, L"ControlKey"}, {18, L"Menu"}, {19, L"Pause"}, {20, L"Capital"},
            {20, L"CapsLock"}, {21, L"KanaMode"}, {21, L"HanguelMode"}, {21, L"HangulMode"}, {23, L"JunjaMode"},
            {24, L"FinalMode"}, {25, L"HanjaMode"}, {25, L"KanjiMode"}, {27, L"Escape"}, {28, L"IMEConvert"},
            {29, L"IMENonconvert"}, {30, L"IMEAccept"}, {30, L"IMEAceept"}, {31, L"IMEModeChange"},
            {32, L"Space"}, {33, L"PageUp"}, {33, L"Prior"}, {34, L"Next"}, {34, L"PageDown"}, {35, L"End"},
            {36, L"Home"}, {37, L"Left"}, {38, L"Up"}, {39, L"Right"}, {40, L"Down"}, {41, L"Select"},
            {42, L"Print"}, {43, L"Execute"}, {44, L"PrintScreen"}, {44, L"Snapshot"}, {45, L"Insert"},
            {46, L"Delete"}, {47, L"Help"},
        };
        for (int d = 0; d <= 9; d++) t.push_back({48 + d, L"D" + std::to_wstring(d)});
        for (int c = 0; c < 26; c++) t.push_back({65 + c, std::wstring(1, (wchar_t)(L'A' + c))});
        t.insert(t.end(), {{91, L"LWin"}, {92, L"RWin"}, {93, L"Apps"}, {95, L"Sleep"}});
        for (int d = 0; d <= 9; d++) t.push_back({96 + d, L"NumPad" + std::to_wstring(d)});
        t.insert(t.end(), {{106, L"Multiply"}, {107, L"Add"}, {108, L"Separator"}, {109, L"Subtract"},
                           {110, L"Decimal"}, {111, L"Divide"}});
        for (int f = 1; f <= 24; f++) t.push_back({111 + f, L"F" + std::to_wstring(f)});
        t.insert(t.end(),
                 {{144, L"NumLock"}, {145, L"Scroll"}, {160, L"LShiftKey"}, {161, L"RShiftKey"},
                  {162, L"LControlKey"}, {163, L"RControlKey"}, {164, L"LMenu"}, {165, L"RMenu"},
                  {166, L"BrowserBack"}, {167, L"BrowserForward"}, {168, L"BrowserRefresh"}, {169, L"BrowserStop"},
                  {170, L"BrowserSearch"}, {171, L"BrowserFavorites"}, {172, L"BrowserHome"}, {173, L"VolumeMute"},
                  {174, L"VolumeDown"}, {175, L"VolumeUp"}, {176, L"MediaNextTrack"}, {177, L"MediaPreviousTrack"},
                  {178, L"MediaStop"}, {179, L"MediaPlayPause"}, {180, L"LaunchMail"}, {181, L"SelectMedia"},
                  {182, L"LaunchApplication1"}, {183, L"LaunchApplication2"}, {186, L"OemSemicolon"},
                  {186, L"Oem1"}, {187, L"Oemplus"}, {188, L"Oemcomma"}, {189, L"OemMinus"}, {190, L"OemPeriod"},
                  {191, L"OemQuestion"}, {191, L"Oem2"}, {192, L"Oemtilde"}, {192, L"Oem3"},
                  {219, L"OemOpenBrackets"}, {219, L"Oem4"}, {220, L"OemPipe"}, {220, L"Oem5"},
                  {221, L"OemCloseBrackets"}, {221, L"Oem6"}, {222, L"OemQuotes"}, {222, L"Oem7"}, {223, L"Oem8"},
                  {226, L"OemBackslash"}, {226, L"Oem102"}, {229, L"ProcessKey"}, {231, L"Packet"}, {246, L"Attn"},
                  {247, L"Crsel"}, {248, L"Exsel"}, {249, L"EraseEof"}, {250, L"Play"}, {251, L"Zoom"},
                  {252, L"NoName"}, {253, L"Pa1"}, {254, L"OemClear"}});
        return t;
    }();
    return table;
}

}  // namespace

std::wstring KeyName(int vk) {
    if (vk == kWheelUp) return L"WheelUp";
    if (vk == kWheelDown) return L"WheelDown";
    for (auto& k : KeyTable())
        if (k.vk == vk) return k.name;
    return std::to_wstring(vk);
}

bool KeyParse(std::wstring_view s, int& vk) {
    std::wstring t = Trim(s);
    if (IEquals(t, L"wheelup")) {
        vk = kWheelUp;
        return true;
    }
    if (IEquals(t, L"wheeldown")) {
        vk = kWheelDown;
        return true;
    }
    int dummy;
    if (t.empty() || ParseInt(t, dummy)) return false;
    for (auto& k : KeyTable())
        if (IEquals(k.name, t)) {
            vk = k.vk;
            return true;
        }
    return false;
}

std::wstring Pretty(std::wstring_view k) {
    static const std::pair<const wchar_t*, const wchar_t*> map[] = {
        {L"LButton", L"Mouse 1"}, {L"RButton", L"Mouse 2"}, {L"MButton", L"Mouse 3"}, {L"XButton1", L"Mouse 4"},
        {L"XButton2", L"Mouse 5"}, {L"LShiftKey", L"LShift"}, {L"RShiftKey", L"RShift"},
        {L"LControlKey", L"LCtrl"}, {L"RControlKey", L"RCtrl"}, {L"LMenu", L"LAlt"}, {L"RMenu", L"RAlt"},
        {L"ShiftKey", L"Shift"}, {L"ControlKey", L"Ctrl"}, {L"Menu", L"Alt"}, {L"Oemtilde", L"~"},
        {L"Oem3", L"~"}, {L"Capital", L"Caps"}, {L"CapsLock", L"Caps"}, {L"WheelUp", L"Wheel Up"},
        {L"WheelDown", L"Wheel Down"}, {L"Return", L"Enter"}, {L"Escape", L"Esc"}, {L"Back", L"Backspace"},
        {L"Next", L"PgDn"}, {L"PageDown", L"PgDn"}, {L"Prior", L"PgUp"}, {L"PageUp", L"PgUp"},
        {L"Snapshot", L"PrtSc"}, {L"PrintScreen", L"PrtSc"}, {L"Delete", L"Del"}, {L"Insert", L"Ins"},
        {L"Scroll", L"ScrLk"}, {L"Oemcomma", L","}, {L"OemPeriod", L"."}, {L"OemMinus", L"-"},
        {L"Oemplus", L"="}, {L"OemQuestion", L"/"}, {L"OemSemicolon", L";"}, {L"OemQuotes", L"'"},
        {L"OemOpenBrackets", L"["}, {L"OemCloseBrackets", L"]"}, {L"OemPipe", L"\\"},
        {L"OemBackslash", L"\\"}, {L"LWin", L"Win"}, {L"RWin", L"RWin"}, {L"Apps", L"Menu"},
    };
    if (k.empty()) return L"None";
    for (auto& [from, to] : map)
        if (k == from) return to;
    if (k.size() == 2 && k[0] == L'D' && iswdigit(k[1])) return std::wstring(k.substr(1));
    if (k.size() == 7 && k.substr(0, 6) == L"NumPad") return L"Num " + std::wstring(k.substr(6));
    return std::wstring(k);
}

std::wstring KeyLabel(std::wstring_view trigger) {
    return trigger.empty() ? L"Set key" : Pretty(trigger);
}

// ---------------------------------------------------------------- categories & config

const CategoryInfo kCategories[kCategoryCount] = {
    {L"Editing", L""}, {L"Building", L""},    {L"Movement", L""},    {L"Utility", L""},
    {L"Custom", L""},
};

bool IsCategory(std::wstring_view name) {
    for (auto& c : kCategories)
        if (name == c.name) return true;
    return false;
}

static Macro M(const wchar_t* cat, const wchar_t* name, const wchar_t* steps) {
    Macro m;
    m.category = cat;
    m.name = name;
    m.steps = steps;
    return m;
}

Config Config::Defaults() {
    Config c;
    c.binds = {{L"Edit", L"G"},       {L"Wall", L"Q"},          {L"Floor", L"F"},       {L"Stair", L"C"},
               {L"Cone", L"V"},       {L"Trap", L"T"},          {L"Use", L"E"},         {L"Reload", L"R"},
               {L"Jump", L"Space"},   {L"Crouch", L"LControlKey"}, {L"Sprint", L"LShiftKey"},
               {L"Forward", L"W"},    {L"Pickaxe", L"D1"},      {L"Slot1", L"D2"},      {L"Slot2", L"D3"}};
    auto& v = c.macros;
    Macro m;
    m = M(L"Editing", L"Single Edit", L"{Edit}, LButton down, wait 5, LButton up, {Edit}"); m.stepDelay = 50; v.push_back(m);
    m = M(L"Editing", L"Drag Edit", L"{Edit}, LButton down"); m.releaseSteps = L"LButton up, {Edit}";
    m.holdMs = 5; m.stepDelay = 10; v.push_back(m);
    m = M(L"Editing", L"Edit Reset", L"{Edit}, RButton down, wait 5, RButton up, {Edit}"); m.stepDelay = 50; v.push_back(m);
    // Hold the click across a frame so the edit tile actually gets selected.
    m = M(L"Editing", L"Edit Spam", L"{Edit}, LButton down, wait 5, LButton up, {Edit}"); m.repeat = true; m.repeatDelay = 15;
    m.stepDelay = 50; v.push_back(m);
    // Retakes: one pickaxe hit on the piece, then place your own.
    v.push_back(M(L"Building", L"Wall Retake", L"{Pickaxe}, LButton, wait 200, {Pickaxe}, {Wall}, LButton"));
    v.push_back(M(L"Building", L"Floor Retake", L"{Pickaxe}, LButton, wait 200, {Pickaxe}, {Floor}, LButton"));
    v.push_back(M(L"Building", L"Stair Retake", L"{Pickaxe}, LButton, wait 200, {Pickaxe}, {Stair}, LButton"));
    v.push_back(M(L"Building", L"Cone Retake", L"{Pickaxe}, LButton, wait 200, {Pickaxe}, {Cone}, LButton"));
    v.push_back(M(L"Building", L"Box Up", L"{Wall}, LButton, {Floor}, LButton, {Cone}, LButton"));
    m = M(L"Building", L"Ramp Rush", L"{Stair}, LButton, {Wall}, LButton, {Jump}"); m.repeat = true;
    m.repeatDelay = 120; v.push_back(m);
    m = M(L"Building", L"Wall Spam", L"{Wall}, LButton"); m.repeat = true; m.repeatDelay = 25; v.push_back(m);
    m = M(L"Building", L"Floor Spam", L"{Floor}, LButton"); m.repeat = true; m.repeatDelay = 25; v.push_back(m);
    v.push_back(M(L"Movement", L"Crouch Peek", L"{Crouch}, wait 80, {Crouch}"));
    m = M(L"Movement", L"Crouch Spam", L"{Crouch}"); m.repeat = true; m.repeatDelay = 60; v.push_back(m);
    m = M(L"Movement", L"Jump Spam", L"{Jump}"); m.repeat = true; m.repeatDelay = 50; v.push_back(m);
    m = M(L"Movement", L"Auto Sprint", L"{Sprint} down"); m.releaseSteps = L"{Sprint} up"; m.toggle = true;
    m.blockTrigger = true; v.push_back(m);
    m = M(L"Movement", L"Auto Run", L"{Forward} down"); m.releaseSteps = L"{Forward} up"; m.toggle = true;
    v.push_back(m);
    m = M(L"Utility", L"Quick Fire", L"LButton"); m.repeat = true; m.repeatDelay = 30; v.push_back(m);
    m = M(L"Utility", L"Auto Clicker", L"LButton"); m.repeat = true; m.repeatDelay = 50; m.toggle = true;
    v.push_back(m);
    v.push_back(M(L"Utility", L"Shoot + Swap", L"LButton, wait 40, {Slot2}"));
    m = M(L"Utility", L"Fast Pickup", L"{Use}"); m.repeat = true; m.repeatDelay = 20; v.push_back(m);
    v.push_back(M(L"Utility", L"Pickaxe Swap", L"{Pickaxe}, {Slot1}"));
    v.push_back(M(L"Utility", L"Trap Place", L"{Trap}, LButton"));
    // Piece-control and push combos: key + click only, so frame sync lands each step clean.
    v.push_back(M(L"Building", L"Wall + Cone", L"{Wall}, LButton, {Cone}, LButton"));
    v.push_back(M(L"Building", L"Floor + Cone", L"{Floor}, LButton, {Cone}, LButton"));
    v.push_back(M(L"Building", L"Ramp + Wall", L"{Stair}, LButton, {Wall}, LButton"));
    v.push_back(M(L"Building", L"Ramp + Floor Push", L"{Stair}, LButton, {Floor}, LButton, {Wall}, LButton"));
    m = M(L"Building", L"Cone Jump", L"{Cone}, LButton, {Jump}"); m.repeat = true; m.repeatDelay = 110; v.push_back(m);
    v.push_back(M(L"Editing", L"Wall + Edit Peek",
                  L"{Wall}, LButton, {Edit}, LButton down, wait 5, LButton up, {Edit}"));
    v.push_back(M(L"Utility", L"Weapon Double Swap", L"{Slot1}, {Slot2}"));
    v.push_back(M(L"Utility", L"Shoot + Pickaxe", L"LButton, wait 40, {Pickaxe}"));
    c.AssignIds();
    return c;
}

std::wstring Config::FilePath() { return PathJoin(ExeDir(), L"macros.json"); }
std::wstring Config::ProfileDir() { return PathJoin(ExeDir(), L"profiles"); }

namespace {

using json::Value;

void GetBool(const Value& v, const char* k, bool& out) {
    if (auto* x = v.Get(k); x && x->t == Value::Bool) out = x->b;
}
void GetInt(const Value& v, const char* k, int& out) {
    if (auto* x = v.Get(k); x && x->t == Value::Num) out = (int)std::clamp(x->n, -2147483648.0, 2147483647.0);
}
void GetStr(const Value& v, const char* k, std::wstring& out) {
    auto* x = v.Get(k);
    if (!x) return;
    if (x->t == Value::Str) out = Widen(x->s);
    else if (x->t == Value::Null) out.clear();
}
void GetNum(const Value& v, const char* k, double& out, double lo, double hi) {
    if (auto* x = v.Get(k); x && x->t == Value::Num) out = std::clamp(x->n, lo, hi);
}
void GetList(const Value& v, const char* k, std::vector<std::wstring>& out) {
    auto* x = v.Get(k);
    if (!x || x->t != Value::Arr) return;
    out.clear();
    for (auto& e : x->a)
        if (e.t == Value::Str) out.push_back(Widen(e.s));
}

Macro MacroFrom(const Value& v) {
    Macro m;
    GetStr(v, "Name", m.name);
    GetStr(v, "Category", m.category);
    GetBool(v, "Enabled", m.enabled);
    GetStr(v, "Trigger", m.trigger);
    GetStr(v, "Steps", m.steps);
    GetStr(v, "ReleaseSteps", m.releaseSteps);
    GetInt(v, "HoldMs", m.holdMs);
    GetInt(v, "StepDelay", m.stepDelay);
    GetBool(v, "Repeat", m.repeat);
    GetInt(v, "RepeatDelay", m.repeatDelay);
    GetBool(v, "BlockTrigger", m.blockTrigger);
    GetBool(v, "Toggle", m.toggle);
    return m;
}

Value MacroValue(const Macro& m) {
    Value v = Value::Object();
    v.Set("Name", Value::String(Narrow(m.name)));
    v.Set("Category", Value::String(Narrow(m.category)));
    v.Set("Enabled", Value::Boolean(m.enabled));
    v.Set("Trigger", Value::String(Narrow(m.trigger)));
    v.Set("Steps", Value::String(Narrow(m.steps)));
    v.Set("ReleaseSteps", Value::String(Narrow(m.releaseSteps)));
    v.Set("HoldMs", Value::Number(m.holdMs));
    v.Set("StepDelay", Value::Number(m.stepDelay));
    v.Set("Repeat", Value::Boolean(m.repeat));
    v.Set("RepeatDelay", Value::Number(m.repeatDelay));
    v.Set("BlockTrigger", Value::Boolean(m.blockTrigger));
    v.Set("Toggle", Value::Boolean(m.toggle));
    return v;
}

Value StrList(const std::vector<std::wstring>& l) {
    Value a = Value::Array();
    for (auto& s : l) a.Push(Value::String(Narrow(s)));
    return a;
}

Config FromJson(const Value& v) {
    Config c = Config::Defaults();
    GetBool(v, "Enabled", c.enabled);
    GetStr(v, "ToggleKey", c.toggleKey);
    GetBool(v, "HighPriority", c.highPriority);
    GetBool(v, "ToggleSound", c.toggleSound);
    GetStr(v, "Timing", c.timing);
    GetStr(v, "PingHost", c.pingHost);
    GetList(v, "AppliedTweaks", c.appliedTweaks);
    GetStr(v, "PrevPowerScheme", c.prevPowerScheme);
    GetStr(v, "MaxPowerScheme", c.maxPowerScheme);
    GetList(v, "KnownDefaults", c.knownDefaults);
    GetStr(v, "Theme", c.theme);
    GetBool(v, "Animations", c.animations);
    GetBool(v, "SidebarCollapsed", c.sidebarCollapsed);
    GetBool(v, "TutorialDone", c.tutorialDone);
    GetBool(v, "OnlyInGame", c.onlyInGame);
    GetBool(v, "Osd", c.osd);
    GetBool(v, "Menu", c.menu);
    GetStr(v, "MenuKey", c.menuKey);
    GetBool(v, "EditOnRelease", c.editOnRelease);
    GetBool(v, "EditGuard", c.editGuard);
    GetStr(v, "EditResetKey", c.editResetKey);
    GetBool(v, "EditKeyCheck", c.editKeyCheck);
    if (auto* b = v.Get("Binds"); b && b->t == Value::Obj) {
        c.binds.clear();
        for (auto& m : b->o)
            if (m.v.t == Value::Str) c.binds.push_back({Widen(m.k), Widen(m.v.s)});
    }
    if (auto* ms = v.Get("Macros"); ms && ms->t == Value::Arr) {
        c.macros.clear();
        for (auto& e : ms->a)
            if (e.t == Value::Obj) c.macros.push_back(MacroFrom(e));
    }
    if (c.timing != L"Frame" && c.timing != L"FrameHuman") c.timing = L"FrameHuman";
    c.AssignIds();
    return c;
}

Value ToJson(const Config& c) {
    Value v = Value::Object();
    v.Set("Enabled", Value::Boolean(c.enabled));
    v.Set("ToggleKey", Value::String(Narrow(c.toggleKey)));
    v.Set("HighPriority", Value::Boolean(c.highPriority));
    v.Set("ToggleSound", Value::Boolean(c.toggleSound));
    v.Set("Timing", Value::String(Narrow(c.timing)));
    v.Set("PingHost", Value::String(Narrow(c.pingHost)));
    v.Set("AppliedTweaks", StrList(c.appliedTweaks));
    v.Set("PrevPowerScheme", Value::String(Narrow(c.prevPowerScheme)));
    v.Set("MaxPowerScheme", Value::String(Narrow(c.maxPowerScheme)));
    v.Set("KnownDefaults", StrList(c.knownDefaults));
    v.Set("Theme", Value::String(Narrow(c.theme)));
    v.Set("Animations", Value::Boolean(c.animations));
    v.Set("SidebarCollapsed", Value::Boolean(c.sidebarCollapsed));
    v.Set("TutorialDone", Value::Boolean(c.tutorialDone));
    v.Set("OnlyInGame", Value::Boolean(c.onlyInGame));
    v.Set("Osd", Value::Boolean(c.osd));
    v.Set("Menu", Value::Boolean(c.menu));
    v.Set("MenuKey", Value::String(Narrow(c.menuKey)));
    v.Set("EditOnRelease", Value::Boolean(c.editOnRelease));
    v.Set("EditGuard", Value::Boolean(c.editGuard));
    v.Set("EditResetKey", Value::String(Narrow(c.editResetKey)));
    v.Set("EditKeyCheck", Value::Boolean(c.editKeyCheck));
    Value b = Value::Object();
    for (auto& [k, val] : c.binds) b.Set(Narrow(k), Value::String(Narrow(val)));
    v.Set("Binds", b);
    Value ms = Value::Array();
    for (auto& m : c.macros) ms.Push(MacroValue(m));
    v.Set("Macros", ms);
    return v;
}

}  // namespace

void Config::AssignIds() {
    static int next = 1;
    for (auto& m : macros)
        if (m.id == 0) m.id = next++;
}

void Config::Normalize() {
    Config d = Defaults();
    for (auto& m : macros) {
        if (IsCategory(m.category)) continue;
        m.category = L"Custom";
        for (auto& dm : d.macros)
            if (dm.name == m.name) m.category = dm.category;
    }
    for (auto& [k, v] : d.binds) {
        bool has = std::any_of(binds.begin(), binds.end(), [&](auto& b) { return b.first == k; });
        if (!has) binds.push_back({k, v});
    }
    AssignIds();
}

Config Config::Load(std::wstring* warning) {
    std::wstring path = FilePath();
    Config c = Defaults();
    std::string bytes;
    if (ReadFileBytes(path, bytes)) {
        Value v;
        if (json::Parse(bytes, v) && v.t == Value::Obj) {
            c = FromJson(v);
        } else {
            MoveFileExW(path.c_str(), (path + L".bad").c_str(), MOVEFILE_REPLACE_EXISTING);
            if (warning) *warning = L"macros.json was corrupt, so it was saved as macros.json.bad and the defaults were loaded.";
        }
    }
    // The auto-build / sensitivity feature was removed; drop the macros it created.
    static const wchar_t* gone[] = {L"Right 90s", L"Left 90s", L"1x1 Box", L"Tunnel", L"Calibration 360",
                                    L"Double Edit: Side Walls", L"Double Edit: Wall + Floor"};
    auto& ms = c.macros;
    ms.erase(std::remove_if(ms.begin(), ms.end(),
                            [](const Macro& m) {
                                if (m.category == L"Auto Build") return true;
                                if (m.category != L"Double Edit") return false;
                                for (auto g : gone)
                                    if (m.name.rfind(g, 0) == 0) return true;
                                return false;
                            }),
             ms.end());
    // Old Edit Spam pressed and released the click in the same instant, so nothing got selected.
    for (auto& m : ms)
        if (m.name == L"Edit Spam" && m.steps == L"{Edit}, LButton, {Edit}") {
            m.steps = L"{Edit}, LButton down, wait 5, LButton up, {Edit}";
            if (m.repeatDelay == 60) m.repeatDelay = 15;
        }
    for (auto& m : ms) {
        if (m.name == L"Single Edit" && m.steps == L"{Edit}, LButton, {Edit}")
            m.steps = L"{Edit}, LButton down, wait 5, LButton up, {Edit}";
        if (m.name == L"Edit Reset" && m.steps == L"{Edit}, RButton, {Edit}")
            m.steps = L"{Edit}, RButton down, wait 5, RButton up, {Edit}";
    }
    // Retakes that still have the old built-in steps get the pickaxe version.
    for (auto& m : ms) {
        for (const wchar_t* piece : {L"Wall", L"Floor", L"Stair", L"Cone"}) {
            std::wstring p = piece;
            if (m.name == p + L" Retake" && m.steps == L"{" + p + L"}, LButton")
                m.steps = L"{Pickaxe}, LButton, wait 200, {Pickaxe}, {" + p + L"}, LButton";
        }
    }
    // The built-in "Drag Edit (Confirm on Release)" is now plain Drag Edit plus the Edit/reset on
    // release setting. Someone who used it gets that setting, and its key if Drag Edit was off.
    auto named = [&](const wchar_t* n) { return std::find_if(ms.begin(), ms.end(), [&](auto& m) { return m.name == n; }); };
    if (auto old = named(L"Drag Edit (Confirm on Release)");
        old != ms.end() && old->steps == L"{Edit}, LButton down" && old->releaseSteps == L"LButton up") {
        if (old->enabled) c.editOnRelease = true;
        if (auto drag = named(L"Drag Edit"); drag == ms.end()) {
            old->name = L"Drag Edit";
            old->releaseSteps = L"LButton up, {Edit}";
        } else {
            if (old->enabled && !drag->enabled) {
                drag->trigger = old->trigger;
                drag->enabled = true;
            }
            ms.erase(old);
        }
    }
    Config d = Defaults();
    c.Normalize();
    for (auto& dm : d.macros) {
        bool known = std::find(c.knownDefaults.begin(), c.knownDefaults.end(), dm.name) != c.knownDefaults.end();
        bool present = std::any_of(c.macros.begin(), c.macros.end(), [&](auto& m) { return m.name == dm.name; });
        if (!known && !present) c.macros.push_back(dm);
    }
    c.knownDefaults.clear();
    for (auto& dm : d.macros) c.knownDefaults.push_back(dm.name);
    for (auto& m : c.macros) m.id = 0;
    c.AssignIds();
    return c;
}

bool Config::LoadFile(const std::wstring& path, Config& out, std::wstring* err) {
    std::string bytes;
    if (!ReadFileBytes(path, bytes)) {
        if (err) *err = L"The file can't be read.";
        return false;
    }
    Value v;
    if (!json::Parse(bytes, v) || v.t != Value::Obj) {
        if (err) *err = L"The file isn't valid JSON.";
        return false;
    }
    out = FromJson(v);
    return true;
}

static bool g_readOnly = false;
void SetConfigReadOnly(bool ro) { g_readOnly = ro; }

bool Config::SaveTo(const std::wstring& path) const {
    if (g_readOnly) return true;
    return WriteFileAtomic(path, json::Write(ToJson(*this), true));
}

bool Config::Save() const { return SaveTo(FilePath()); }

std::string MacroToJson(const Macro& m) { return json::Write(MacroValue(m), false); }

// ---------------------------------------------------------------- steps

void AdaptForEditOnRelease(std::vector<Step>& press, std::vector<Step>& release, int editVk) {
    bool editing = false, finished = false;  // carried from press into release (Drag Edit)
    for (auto* list : {&press, &release}) {
        std::vector<Step> out;
        for (auto& s : *list) {
            if (s.kind == L'w') {
                out.push_back(s);
                continue;
            }
            bool editTap = s.key == editVk && s.kind == L't';
            if (editTap && finished) {
                finished = false;
                continue;
            }
            finished = false;
            if (editTap) editing = !editing;
            else if (editing && (s.kind == L'u' || s.kind == L't') && (s.key == VK_LBUTTON || s.key == VK_RBUTTON)) {
                editing = false;
                finished = true;
            }
            out.push_back(s);
        }
        *list = std::move(out);
    }
}

int ParseKey(std::wstring name, const Binds& binds) {
    if (name.size() >= 2 && name.front() == L'{' && name.back() == L'}') {
        std::wstring inner = name.substr(1, name.size() - 2);
        auto it = std::find_if(binds.begin(), binds.end(), [&](auto& kv) { return IEquals(kv.first, inner); });
        if (it == binds.end()) throw FormatError{L"Unknown bind " + name};
        name = it->second;
    }
    if (name.size() == 1 && name[0] >= L'0' && name[0] <= L'9') name = L"D" + name;
    int vk;
    if (!KeyParse(name, vk)) throw FormatError{L"Unknown key '" + name + L"'"};
    return vk;
}

std::vector<Step> ParseSteps(std::wstring_view text, const Binds& binds) {
    std::vector<Step> list;
    for (auto& entry : Split(text, L",\n", true, true)) {
        auto parts = Split(entry, L" ", false, true);
        std::wstring w0 = Lower(parts[0]);
        if (w0 == L"move") {
            int dx, dy;
            if (parts.size() != 3 || !ParseInt(parts[1], dx) || !ParseInt(parts[2], dy) || std::abs(dx) > 100000 ||
                std::abs(dy) > 100000)
                throw FormatError{L"'" + entry + L"': use 'move 120 -40' (mouse counts right and down)"};
            Step s{0, L'm', 0};
            s.dx = dx;
            s.dy = dy;
            list.push_back(s);
            continue;
        }
        if (IEquals(parts[0], L"wait")) {
            int ms;
            if (parts.size() != 2 || !ParseInt(parts[1], ms) || ms < 0)
                throw FormatError{L"'" + entry + L"': use 'wait 50'"};
            if (ms > 60000) throw FormatError{L"'" + entry + L"': the longest wait is 60000 ms"};
            list.push_back({0, L'w', ms});
            continue;
        }
        wchar_t kind = L't';
        if (parts.size() > 2) throw FormatError{L"'" + entry + L"': one key per step, then optionally 'down' or 'up'"};
        if (parts.size() == 2) {
            std::wstring t = Lower(parts[1]);
            if (t == L"down") kind = L'd';
            else if (t == L"up") kind = L'u';
            else throw FormatError{L"'" + entry + L"': expected 'down' or 'up' after the key"};
        }
        list.push_back({ParseKey(parts[0], binds), kind, 0});
    }
    return list;
}

std::vector<StepToken> TokenizeSteps(std::wstring_view t, const Binds& binds) {
    std::vector<StepToken> out;
    size_t i = 0, n = t.size();
    for (;;) {
        size_t s = i;
        while (i < n && t[i] != L',' && t[i] != L'\n') i++;
        std::vector<std::pair<size_t, size_t>> words;
        for (size_t j = s; j < i;) {
            while (j < i && iswspace(t[j])) j++;
            size_t w = j;
            while (j < i && !iswspace(t[j])) j++;
            if (j > w) words.push_back({w, j - w});
        }
        if (!words.empty()) {
            auto word = [&](size_t k) { return std::wstring(t.substr(words[k].first, words[k].second)); };
            auto add = [&](size_t k, int kind) { out.push_back({words[k].first, words[k].second, kind}); };
            std::wstring w0l = Lower(word(0));
            if (w0l == L"move") {
                int a, b;
                bool ok = words.size() == 3 && ParseInt(word(1), a) && ParseInt(word(2), b);
                for (size_t k = 0; k < words.size(); k++) add(k, ok ? 5 : 4);
            } else if (IEquals(word(0), L"wait")) {
                int ms;
                bool ok = words.size() == 2 && ParseInt(word(1), ms) && ms >= 0 && ms <= 60000;
                add(0, ok ? 2 : 4);
                for (size_t k = 1; k < words.size(); k++) add(k, ok ? 2 : 4);
            } else {
                bool keyOk = true;
                try {
                    ParseKey(word(0), binds);
                } catch (const FormatError&) {
                    keyOk = false;
                }
                std::wstring w0 = word(0);
                add(0, !keyOk ? 4 : (w0.size() > 1 && w0.front() == L'{') ? 1 : 0);
                if (words.size() >= 2) {
                    std::wstring m = Lower(word(1));
                    add(1, (m == L"down" || m == L"up") ? 3 : 4);
                }
                for (size_t k = 2; k < words.size(); k++) add(k, 4);
            }
        }
        if (i >= n) break;
        i++;
    }
    return out;
}

// ---------------------------------------------------------------- recorder

std::vector<InputEvent> RecorderClean(const std::vector<InputEvent>& rawIn) {
    // Alt+Tab into the game at the start of a recording isn't part of the macro.
    std::vector<InputEvent> raw;
    size_t start = 0;
    while (start < rawIn.size() && rawIn[start].key == kMouseMove) start++;
    if (start < rawIn.size() && rawIn[start].down &&
        (rawIn[start].key == VK_LMENU || rawIn[start].key == VK_RMENU || rawIn[start].key == VK_MENU)) {
        int alt = rawIn[start].key;
        size_t i = start + 1;
        while (i < rawIn.size() && !(rawIn[i].key == alt && !rawIn[i].down)) i++;
        bool onlyTab = true;
        for (size_t j = start + 1; j < i; j++)
            if (rawIn[j].key != VK_TAB && rawIn[j].key != kMouseMove) onlyTab = false;
        if (onlyTab && i < rawIn.size()) start = i + 1;
    }
    raw.assign(rawIn.begin() + std::min(start, rawIn.size()), rawIn.end());
    while (!raw.empty() && raw.front().key == kMouseMove) raw.erase(raw.begin());

    std::vector<int> held;
    std::vector<InputEvent> list;
    for (auto& e : raw) {
        if (e.key == kMouseMove) {
            list.push_back(e);
            continue;
        }
        auto it = std::find(held.begin(), held.end(), e.key);
        if (e.down) {
            if (it != held.end()) continue;  // auto-repeat
            held.push_back(e.key);
        } else {
            if (it == held.end()) continue;  // released something pressed before recording
            held.erase(it);
        }
        list.push_back(e);
    }
    // Moving the mouse back to the app after the last input isn't part of the macro.
    while (!list.empty() && list.back().key == kMouseMove) list.pop_back();
    // The click on "Stop" itself lands as a final left-button press.
    if (!list.empty() && list.back().key == VK_LBUTTON && list.back().down) {
        list.pop_back();
        held.erase(std::remove(held.begin(), held.end(), VK_LBUTTON), held.end());
        while (!list.empty() && list.back().key == kMouseMove) list.pop_back();
    }
    long long last = list.empty() ? 0 : list.back().ms;
    for (int k : held) list.push_back({k, false, last});
    return list;
}

std::wstring RecorderToSteps(const std::vector<InputEvent>& ev, const Binds& binds, int minWait) {
    std::vector<std::wstring> list;
    long long last = ev.empty() ? 0 : ev[0].ms;
    for (size_t i = 0; i < ev.size(); i++) {
        auto& e = ev[i];
        if (e.ms - last >= minWait && !list.empty()) list.push_back(L"wait " + std::to_wstring(e.ms - last));
        if (e.key == kMouseMove) {
            list.push_back(Fmt(L"move %d %d", e.dx, e.dy));
            last = e.ms;
            continue;
        }
        std::wstring key = KeyName(e.key);
        std::wstring text = key;
        for (auto& kv : binds)
            if (kv.second == key) {
                text = L"{" + kv.first + L"}";
                break;
            }
        if (e.down && i + 1 < ev.size() && ev[i + 1].key == e.key && !ev[i + 1].down) {
            list.push_back(text);
            last = ev[++i].ms;
        } else {
            list.push_back(text + (e.down ? L" down" : L" up"));
            last = e.ms;
        }
    }
    return Join(list, L", ");
}

// ---------------------------------------------------------------- share codes

static const wchar_t kPrefix[] = L"MT1:";

std::wstring ShareExport(const Macro& src) {
    Macro m = src;
    m.trigger.clear();
    m.enabled = false;
    std::string js = MacroToJson(m);
    auto packed = Deflate((const uint8_t*)js.data(), js.size());
    return kPrefix + Widen(Base64UrlEncode(packed));
}

Macro ShareImport(std::wstring code) {
    code = Trim(code);
    // Recorded mouse paths make long codes, so allow more than the .NET version's 8000.
    if (code.rfind(kPrefix, 0) != 0 || code.size() > 60000) throw FormatError{L"That isn't a Macro Tool share code."};
    std::vector<uint8_t> raw;
    if (!Base64Decode(Narrow(code.substr(4)), raw)) throw FormatError{L"The share code is damaged (copy all of it)."};
    std::vector<uint8_t> js;
    if (!Inflate(raw.data(), raw.size(), js, 65536)) {
        if (js.size() >= 65536) throw FormatError{L"The share code is too big."};
        throw FormatError{L"The share code is damaged (copy all of it)."};
    }
    json::Value v;
    if (!json::Parse(std::string_view((const char*)js.data(), js.size()), v)) throw FormatError{L"The share code is damaged."};
    if (v.t == json::Value::Null) throw FormatError{L"Empty share code."};
    if (v.t != json::Value::Obj) throw FormatError{L"The share code is damaged."};
    Macro m = MacroFrom(v);
    std::wstring name = Trim(m.name);
    m.name = name.empty() ? L"Imported macro" : name.substr(0, 60);
    m.category = L"Custom";
    m.trigger.clear();
    m.enabled = false;
    m.holdMs = std::clamp(m.holdMs, 0, 5000);
    m.stepDelay = std::clamp(m.stepDelay, 0, 5000);
    m.repeatDelay = std::clamp(m.repeatDelay, 1, 5000);
    return m;
}

// ---------------------------------------------------------------- setup packs

static const wchar_t kPackPrefix[] = L"MTP1:";

bool IsPackCode(const std::wstring& code) { return Trim(code).rfind(kPackPrefix, 0) == 0; }

std::wstring PackExport(const std::vector<Macro>& macros, const Binds& binds) {
    Value root = Value::Object();
    Value ms = Value::Array();
    for (auto& src : macros) {
        Macro m = src;
        m.trigger.clear();
        m.enabled = false;
        ms.Push(MacroValue(m));
    }
    root.Set("Macros", ms);
    Value b = Value::Object();
    for (auto& [k, v] : binds) b.Set(Narrow(k), Value::String(Narrow(v)));
    root.Set("Binds", b);
    std::string js = json::Write(root, false);
    auto packed = Deflate((const uint8_t*)js.data(), js.size());
    return kPackPrefix + Widen(Base64UrlEncode(packed));
}

MacroPack PackImport(std::wstring code) {
    code = Trim(code);
    if (code.rfind(kPackPrefix, 0) != 0 || code.size() > 400000) throw FormatError{L"That isn't a Macro Tool setup code."};
    std::vector<uint8_t> raw;
    if (!Base64Decode(Narrow(code.substr(5)), raw))  // skip "MTP1:"
        throw FormatError{L"The setup code is damaged (copy all of it)."};
    std::vector<uint8_t> js;
    if (!Inflate(raw.data(), raw.size(), js, 2 * 1024 * 1024)) {
        if (js.size() >= 2 * 1024 * 1024) throw FormatError{L"The setup code is too big."};
        throw FormatError{L"The setup code is damaged (copy all of it)."};
    }
    json::Value v;
    if (!json::Parse(std::string_view((const char*)js.data(), js.size()), v) || v.t != json::Value::Obj)
        throw FormatError{L"The setup code is damaged."};
    MacroPack pack;
    if (auto* ms = v.Get("Macros"); ms && ms->t == Value::Arr) {
        for (auto& e : ms->a) {
            if (e.t != Value::Obj) continue;
            Macro m = MacroFrom(e);
            std::wstring name = Trim(m.name);
            m.name = name.empty() ? L"Imported macro" : name.substr(0, 60);
            if (!IsCategory(m.category)) m.category = L"Custom";
            m.trigger.clear();
            m.enabled = false;
            m.holdMs = std::clamp(m.holdMs, 0, 5000);
            m.stepDelay = std::clamp(m.stepDelay, 0, 5000);
            m.repeatDelay = std::clamp(m.repeatDelay, 1, 5000);
            pack.macros.push_back(m);
        }
    }
    if (pack.macros.empty()) throw FormatError{L"The setup code has no macros."};
    if (auto* b = v.Get("Binds"); b && b->t == Value::Obj)
        for (auto& m : b->o)
            if (m.v.t == Value::Str) pack.binds.push_back({Widen(m.k), Widen(m.v.s)});
    return pack;
}
