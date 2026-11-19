#pragma once
#include "gfx.h"
#include "model.h"
#include <unordered_map>

using ID = uint64_t;
ID HashId(std::wstring_view s, ID seed = 0);
ID HashId(ID a, uint64_t b);

struct Theme {
    Color bg, side, panel, panel2, panel3, field, border, border2;
    Color text, dim, faint, accent, accentHi, accent2, green, red, amber, pink;
    Color key, keyEdge, thumb, overlay;
    Color primary, onPrimary;  // filled buttons and highlighted cards
    float shadow;
};
// current palette (blended between kDark/kLight on theme switch)
extern Theme T;
extern const Theme kDark, kLight;
Theme LerpTheme(const Theme& a, const Theme& b, float t);

float Clamp01(float t);
float EaseOut(float t);
float EaseOutBack(float t);
float EaseInOut(float t);

struct KeyEv {
    UINT vk;
    bool ctrl, shift;
};

struct InputState {
    float mx = -10000, my = -10000;
    bool down = false, pressed = false, released = false, dbl = false, rpressed = false;
    bool ctrl = false, shift = false;
    float wheel = 0;  // in notches
    std::vector<KeyEv> keys;
    std::wstring chars;
};

enum class Btn { Primary, Secondary, Ghost, Danger, Record };

struct TextBoxOpts {
    bool multiline = false;
    bool mono = false;
    const wchar_t* placeholder = L"";
    const Binds* colorize = nullptr;  // colour step syntax using these binds
    bool error = false;
    size_t maxLen = 20000;
    float padLeft = 12.f;
};

struct TextBoxResult {
    bool changed = false, submitted = false, focused = false;
};

struct Interact {
    bool hover = false, held = false, pressed = false, clicked = false;
    float hoverT = 0, pressT = 0;
};

// immediate mode ui. widget state (anims, caret, scroll) lives in maps keyed by ID
class UI {
public:
    explicit UI(Gfx& g) : g(g) {}
    Gfx& g;
    HWND hwnd = nullptr;
    InputState in;
    double time = 0;
    float dt = 1.f / 60;
    bool reduceMotion = false;
    ID hot = 0, active = 0, focus = 0;
    bool inputOn = true;
    bool needFrame = false;
    double wakeAt = 0;
    LPCWSTR cursor = IDC_ARROW;

    void Begin(double now);
    void End();

    // exp. approach, `from` seeds new entries
    float Anim(ID id, float target, float speed = 14.f, float from = -1e9f);
    // spring, slight overshoot
    float Spring(ID id, float target, float k = 380.f, float c = 30.f);
    void SetAnim(ID id, float v);
    void Keep() { needFrame = true; }
    void WakeIn(double s);

    void PushOffset(float dx, float dy);
    void PushScale(float s, float cx, float cy);
    void PushRotate(float degrees, float cx, float cy);
    void PopXf();
    void PushClip(const Rect& r);
    void PopClip();
    void PushAlpha(float a);
    void PopAlpha();
    D2D1_POINT_2F Local() const;
    Rect ToScreen(const Rect& r) const;

    bool Hover(const Rect& r) const;
    Interact Press(ID id, const Rect& r, bool enabled = true);
    void Tip(ID id, const std::wstring& text, const Rect& r);
    void DrawTooltip();

    bool Button(ID id, const Rect& r, std::wstring_view label, Btn style = Btn::Secondary, const wchar_t* icon = nullptr,
                bool enabled = true, bool onPress = false);
    bool IconButton(ID id, const Rect& r, const wchar_t* glyph, const wchar_t* tip = nullptr, bool danger = false);
    bool Switch(ID id, float x, float y, bool& v, bool enabled = true, bool busy = false);
    bool Slider(ID id, const Rect& r, int& v, int lo, int hi, bool enabled = true);
    bool Segmented(ID id, const Rect& r, const std::vector<std::wstring>& items, int& sel);
    TextBoxResult TextBox(ID id, const Rect& r, std::wstring& text, const TextBoxOpts& o);
    // number box with -/+, true if changed
    bool NumberField(ID id, const Rect& r, double& v, double lo, double hi, double step, int decimals);
    void Spinner(float cx, float cy, float r, Color c, float w = 2.f);
    void Skeleton(const Rect& r, float rad);
    void Tag(const Rect& r, std::wstring_view text, Color c);

    float BeginScroll(ID id, const Rect& view);
    // fade: soft edge at the bottom when there's more
    void EndScroll(ID id, float contentH, Color fade = {0, 0, 0, 0});
    void ScrollReset(ID id);

    std::vector<Span> StepSpans(const std::wstring& text, const Binds& binds) const;

private:
    struct SpringState {
        float x = 0, v = 0;
    };
    struct TbState {
        int caret = 0, anchor = 0;
        float scrollX = 0, scrollY = 0;
        double blink = 0, lastEdit = -10;
        float prefX = -1;
        bool dragging = false;
        std::vector<std::pair<std::wstring, int>> undo;
    };
    struct ScrollState {
        float pos = 0, target = 0, content = 0, view = 0;
        double lastMove = -10;
        bool drag = false;
        float dragY = 0, dragPos = 0;
    };
    struct ScrollFrame {
        ID id;
        Rect view;
    };
    void ApplyXf();

    std::unordered_map<ID, float> anims_;
    std::unordered_map<ID, SpringState> springs_;
    std::unordered_map<ID, TbState> tb_;
    std::unordered_map<ID, ScrollState> scroll_;
    std::unordered_map<ID, std::wstring> numText_;
    std::vector<D2D1_MATRIX_3X2_F> xfStack_;
    D2D1_MATRIX_3X2_F xf_ = D2D1::Matrix3x2F::Identity();
    D2D1_MATRIX_3X2_F inv_ = D2D1::Matrix3x2F::Identity();
    std::vector<Rect> clips_;  // screen space
    std::vector<float> alphas_;
    std::vector<ScrollFrame> scrollStack_;
    ID hotNext_ = 0;
    ID wheelTarget_ = 0, wheelTargetNext_ = 0;
    bool focusClaimed_ = false;
    ID tipId_ = 0;
    double tipStart_ = 0;
    bool tipSeen_ = false, tipShow_ = false;
    std::wstring tipText_;
    Rect tipAnchor_;
};

std::wstring ClipboardText(HWND h);
void SetClipboardText(HWND h, const std::wstring& s);
