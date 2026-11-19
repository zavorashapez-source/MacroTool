#pragma once
#include "util.h"
#include <functional>

struct TweakError {
    std::wstring msg;
};

// power plan guids, saved in macros.json
struct PowerState {
    std::wstring prev, max;
};

struct Tweak {
    const wchar_t* group;
    const wchar_t* name;
    const wchar_t* info;
    std::function<bool(const PowerState&)> isOn;
    std::function<void(PowerState&)> apply, revert;  // throw TweakError
    bool restart = false;
};

struct TweakAction {
    const wchar_t* group;
    const wchar_t* name;
    const wchar_t* info;
    const wchar_t* button;
    std::function<std::wstring()> run;
    const wchar_t* confirm = nullptr;
};

struct TweakGroup {
    const wchar_t* name;
    const wchar_t* glyph;
};
extern const TweakGroup kTweakGroups[5];

const std::vector<Tweak>& TweakToggles();
const std::vector<TweakAction>& TweakActions();
