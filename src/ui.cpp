#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cwctype>

// dark theme
const Theme kDark = {
    Rgb(0x121318),        Rgb(0x16171D), Rgb(0x1B1D24), Rgb(0x23262F), Rgb(0x2B2F3A), Rgb(0x17181E), Rgb(0x2B2F3A),
    Rgb(0x3A3F4C),        Rgb(0xF6F8FB), Rgb(0xAAB1C0), Rgb(0x6C7380), Rgb(0x8AB0FF), Rgb(0xC6D9FF), Rgb(0x6E8FE6),
    Rgb(0x72D3A2),        Rgb(0xEF8A90), Rgb(0xE4BD6E), Rgb(0xE79AC8), Rgb(0x242732), Rgb(0x0D0E12), Rgb(0x3A3F4C),
    Rgb(0x000000, 0.62f), Rgb(0xEAF1FF), Rgb(0x14161C), 0.78f,
};
const Theme kLight = {
    Rgb(0xF4F6FA),        Rgb(0xFBFCFE), Rgb(0xFFFFFF), Rgb(0xF3F6FB), Rgb(0xE9EEF6), Rgb(0xF7F9FC), Rgb(0xE1E6EF),
    Rgb(0xCFD6E1),        Rgb(0x171A21), Rgb(0x586070), Rgb(0x929AA8), Rgb(0x3B6FE0), Rgb(0x2A54B8), Rgb(0x3F7FD0),
    Rgb(0x2E9E6A),        Rgb(0xD15259), Rgb(0xB07E1E), Rgb(0xBE4E86), Rgb(0xFFFFFF), Rgb(0xD6DCE6), Rgb(0xC6CDD8),
    Rgb(0x000000, 0.16f), Rgb(0x1C1F26), Rgb(0xFFFFFF), 0.38f,
};
Theme T = kDark;

Theme LerpTheme(const Theme &a, const Theme &b, float t)
{
    Theme r;
    const Color *pa = &a.bg;
    const Color *pb = &b.bg;
    Color *pr = &r.bg;
    constexpr int n = (int)(offsetof(Theme, shadow) / sizeof(Color));
    for (int i = 0; i < n; i++)
        pr[i] = Mix(pa[i], pb[i], t);
    r.shadow = a.shadow + (b.shadow - a.shadow) * t;
    return r;
}

ID HashId(std::wstring_view s, ID seed)
{
    ID h = seed ? seed : 1469598103934665603ull;
    for (wchar_t c : s)
    {
        h ^= (ID)c;
        h *= 1099511628211ull;
    }
    return h;
}

ID HashId(ID a, uint64_t b)
{
    ID h = a ^ 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 8; i++)
    {
        h ^= (b >> (i * 8)) & 0xff;
        h *= 1099511628211ull;
    }
    return h ^ (h >> 29);
}

float Clamp01(float t)
{
    return t < 0 ? 0 : t > 1 ? 1 : t;
}
float EaseOut(float t)
{
    t = Clamp01(t);
    float u = 1 - t;
    return 1 - u * u * u;
}
float EaseOutBack(float t)
{
    t = Clamp01(t);
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * std::pow(t - 1, 3.f) + c1 * std::pow(t - 1, 2.f);
}
float EaseInOut(float t)
{
    t = Clamp01(t);
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2;
}

// ---------------------------------------------------------------- frame

void UI::Begin(double now)
{
    dt = (float)std::clamp(now - time, 0.0, 0.05);
    if (time == 0)
        dt = 1.f / 60;
    time = now;
    needFrame = false;
    wakeAt = 0;
    cursor = IDC_ARROW;
    focusClaimed_ = false;
    tipSeen_ = false;
    inputOn = true;
    xf_ = inv_ = D2D1::Matrix3x2F::Identity();
    xfStack_.clear();
    clips_.clear();
    alphas_.clear();
    scrollStack_.clear();
    g.opacity = 1.f;
    g.SetTransform(xf_);
}

void UI::End()
{
    if (in.pressed && !focusClaimed_)
        focus = 0;
    if (hot != hotNext_)
        needFrame = true;
    hot = hotNext_;
    hotNext_ = 0;
    if (in.released || !in.down)
        active = 0;
    wheelTarget_ = wheelTargetNext_;
    wheelTargetNext_ = 0;
    if (!tipSeen_)
    {
        tipId_ = 0;
        tipShow_ = false;
    }
    in.pressed = in.released = in.dbl = in.rpressed = false;
    in.wheel = 0;
    in.keys.clear();
    in.chars.clear();
}

float UI::Anim(ID id, float target, float speed, float from)
{
    auto [it, inserted] = anims_.try_emplace(id, from > -1e8f ? from : target);
    float &v = it->second;
    if (reduceMotion)
    {
        v = target;
        return v;
    }
    float k = 1.f - std::exp(-speed * dt);
    v += (target - v) * k;
    if (std::fabs(target - v) < 0.0015f)
        v = target;
    else
        needFrame = true;
    return v;
}

float UI::Spring(ID id, float target, float k, float c)
{
    auto [it, inserted] = springs_.try_emplace(id, SpringState{target, 0});
    SpringState &s = it->second;
    if (reduceMotion)
    {
        s = {target, 0};
        return target;
    }
    int n = std::max(1, (int)std::ceil(dt / 0.004f));
    float h = dt / n;
    for (int i = 0; i < n; i++)
    {
        float a = k * (target - s.x) - c * s.v;
        s.v += a * h;
        s.x += s.v * h;
    }
    if (std::fabs(target - s.x) < 0.01f && std::fabs(s.v) < 0.05f)
        s = {target, 0};
    else
        needFrame = true;
    return s.x;
}

void UI::SetAnim(ID id, float v)
{
    anims_[id] = v;
}

void UI::WakeIn(double s)
{
    double t = time + std::max(0.0, s);
    if (wakeAt == 0 || t < wakeAt)
        wakeAt = t;
}

// ---------------------------------------------------------------- transforms

void UI::ApplyXf()
{
    inv_ = xf_;
    D2D1::Matrix3x2F *m = D2D1::Matrix3x2F::ReinterpretBaseType(&inv_);
    if (m->IsInvertible())
        m->Invert();
    g.SetTransform(xf_);
}

void UI::PushOffset(float dx, float dy)
{
    xfStack_.push_back(xf_);
    xf_ = D2D1::Matrix3x2F::Translation(dx, dy) * *D2D1::Matrix3x2F::ReinterpretBaseType(&xf_);
    ApplyXf();
}

void UI::PushScale(float s, float cx, float cy)
{
    xfStack_.push_back(xf_);
    xf_ = D2D1::Matrix3x2F::Scale(s, s, {cx, cy}) * *D2D1::Matrix3x2F::ReinterpretBaseType(&xf_);
    ApplyXf();
}

void UI::PushRotate(float degrees, float cx, float cy)
{
    xfStack_.push_back(xf_);
    xf_ = D2D1::Matrix3x2F::Rotation(degrees, {cx, cy}) * *D2D1::Matrix3x2F::ReinterpretBaseType(&xf_);
    ApplyXf();
}

void UI::PopXf()
{
    if (xfStack_.empty())
        return;
    xf_ = xfStack_.back();
    xfStack_.pop_back();
    ApplyXf();
}

void UI::PushClip(const Rect &r)
{
    auto *m = D2D1::Matrix3x2F::ReinterpretBaseType(&xf_);
    D2D1_POINT_2F a = m->TransformPoint({r.x, r.y}), b = m->TransformPoint({r.R(), r.B()});
    Rect s{std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y)};
    if (!clips_.empty())
    {
        const Rect &c = clips_.back();
        float x0 = std::max(s.x, c.x), y0 = std::max(s.y, c.y);
        float x1 = std::min(s.R(), c.R()), y1 = std::min(s.B(), c.B());
        s = {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
    }
    clips_.push_back(s);
    g.PushClip(r);
}

void UI::PopClip()
{
    if (clips_.empty())
        return;
    clips_.pop_back();
    g.PopClip();
}

void UI::PushAlpha(float a)
{
    alphas_.push_back(g.opacity);
    g.opacity *= a;
}

void UI::PopAlpha()
{
    if (alphas_.empty())
        return;
    g.opacity = alphas_.back();
    alphas_.pop_back();
}

D2D1_POINT_2F UI::Local() const
{
    return D2D1::Matrix3x2F::ReinterpretBaseType(&inv_)->TransformPoint({in.mx, in.my});
}

Rect UI::ToScreen(const Rect &r) const
{
    auto *m = D2D1::Matrix3x2F::ReinterpretBaseType(&xf_);
    D2D1_POINT_2F a = m->TransformPoint({r.x, r.y}), b = m->TransformPoint({r.R(), r.B()});
    return {a.x, a.y, b.x - a.x, b.y - a.y};
}

bool UI::Hover(const Rect &r) const
{
    if (!inputOn)
        return false;
    if (!clips_.empty() && !clips_.back().Has(in.mx, in.my))
        return false;
    D2D1_POINT_2F p = Local();
    return r.Has(p.x, p.y);
}

Interact UI::Press(ID id, const Rect &r, bool enabled)
{
    Interact it;
    bool over = enabled && Hover(r);
    if (over)
        hotNext_ = id;
    it.hover = over && hot == id;
    if (it.hover && in.pressed)
    {
        active = id;
        it.pressed = true;
    }
    it.held = enabled && active == id && in.down;
    if (enabled && active == id && in.released)
        it.clicked = over;
    it.hoverT = Anim(HashId(id, 0xA1), (it.hover || it.held) ? 1.f : 0.f, 18.f);
    it.pressT = Anim(HashId(id, 0xA2), (it.held && over) ? 1.f : 0.f, 28.f);
    return it;
}

void UI::Tip(ID id, const std::wstring &text, const Rect &r)
{
    if (hot != id || !Hover(r) || in.down)
        return;
    tipSeen_ = true;
    if (tipId_ != id)
    {
        tipId_ = id;
        tipStart_ = time;
        tipShow_ = false;
    }
    if (time - tipStart_ >= 0.55)
    {
        tipShow_ = true;
        tipText_ = text;
        auto *m = D2D1::Matrix3x2F::ReinterpretBaseType(&xf_);
        D2D1_POINT_2F a = m->TransformPoint({r.x, r.y}), b = m->TransformPoint({r.R(), r.B()});
        tipAnchor_ = {a.x, a.y, b.x - a.x, b.y - a.y};
    }
    else
    {
        WakeIn(0.56 - (time - tipStart_));
    }
}

void UI::DrawTooltip()
{
    float t = Anim(0x7177, tipShow_ ? 1.f : 0.f, 20.f);
    if (t <= 0.01f || tipText_.empty())
        return;
    D2D1_SIZE_F win = g.rt->GetSize();
    float tw = std::min(320.f, g.TextW(tipText_, F::Small));
    float th = g.TextH(tipText_, F::Small, tw + 1);
    Rect box{tipAnchor_.Cx() - (tw + 20) / 2, tipAnchor_.B() + 8 + (1 - t) * 4, tw + 20, th + 12};
    box.x = std::clamp(box.x, 8.f, win.width - box.w - 8);
    if (box.B() > win.height - 8)
        box.y = tipAnchor_.y - box.h - 8;
    PushAlpha(t);
    g.Shadow(box, 8, 10, 0.5f, 2);
    g.Round(box, 8, T.panel3);
    g.RoundStroke(box, 8, T.border2);
    g.Text(tipText_, box.Inset(10, 6), T.text, F::Small, kLeft, false, true);
    PopAlpha();
}

// ---------------------------------------------------------------- widgets

bool UI::Button(ID id, const Rect &r, std::wstring_view label, Btn style, const wchar_t *icon, bool enabled,
                bool onPress)
{
    Interact it = Press(id, r, enabled);
    PushAlpha(enabled ? 1.f : 0.45f);
    // press anim: dip + small bounce back
    float sp = Spring(HashId(id, 0xB7), it.held ? 1.f : 0.f, 460.f, 26.f);
    PushScale(1.f - 0.06f * sp, r.Cx(), r.Cy());
    Color fg = T.text;
    switch (style)
    {
    case Btn::Primary:
        g.Round(r, 10, Mix(T.primary, T.accent, 0.25f * it.hoverT));
        fg = T.onPrimary;
        break;
    case Btn::Secondary:
        g.Round(r, 10, Mix(T.panel2, T.panel3, it.hoverT));
        g.RoundStroke(r, 10, Mix(T.border2, T.faint, it.hoverT * 0.5f));
        break;
    case Btn::Ghost:
        g.Round(r, 10, WithA(T.panel3, it.hoverT));
        fg = Mix(T.dim, T.text, it.hoverT);
        break;
    case Btn::Danger:
        g.Round(r, 10, WithA(T.red, 0.10f + 0.10f * it.hoverT));
        g.RoundStroke(r, 10, WithA(T.red, 0.25f + 0.2f * it.hoverT));
        fg = Mix(T.red, T.text, it.hoverT * 0.25f);
        break;
    case Btn::Record:
        g.Round(r, 10, WithA(T.red, 0.16f + 0.10f * it.hoverT));
        g.RoundStroke(r, 10, WithA(T.red, 0.45f));
        fg = T.red;
        break;
    }
    float iw = icon ? 15.f : 0.f, gap = (icon && !label.empty()) ? 8.f : 0.f;
    float tw = g.TextW(label, F::Semi);
    float x0 = r.Cx() - (iw + gap + tw) / 2;
    if (icon)
        g.Text(icon, {x0, r.y, iw + 4, r.h}, fg, F::IconSm);
    if (!label.empty())
        g.Text(label, {x0 + iw + gap, r.y, tw + 6, r.h}, fg, F::Semi);
    PopXf();
    PopAlpha();
    return onPress ? it.pressed : it.clicked;
}

bool UI::IconButton(ID id, const Rect &r, const wchar_t *glyph, const wchar_t *tip, bool danger)
{
    Interact it = Press(id, r);
    g.Round(r, 8, danger ? WithA(T.red, 0.9f * it.hoverT) : WithA(T.panel3, it.hoverT));
    Color fg = danger ? Mix(T.dim, Rgb(0xFFFFFF), it.hoverT) : Mix(T.dim, T.text, it.hoverT);
    float sp = Spring(HashId(id, 0xB7), it.held ? 1.f : 0.f, 460.f, 24.f);
    PushScale(1.f - 0.11f * sp, r.Cx(), r.Cy());
    g.Text(glyph, r, fg, F::IconSm, kCenter);
    PopXf();
    if (tip)
        Tip(id, tip, r);
    return it.clicked;
}

bool UI::Switch(ID id, float x, float y, bool &v, bool enabled, bool busy)
{
    Rect r{x, y, 40, 22};
    Interact it = Press(id, r.Inset(-4), enabled && !busy);
    bool changed = false;
    if (it.clicked)
    {
        v = !v;
        changed = true;
    }
    float t = Spring(HashId(id, 0xB1), v ? 1.f : 0.f, 520.f, 34.f);
    float tc = Clamp01(t);
    PushAlpha(enabled ? 1.f : 0.45f);
    Color off = Mix(T.panel3, T.border2, it.hoverT);
    Color on = Mix(T.accent, T.accentHi, it.hoverT * 0.35f);
    g.Round(r, 11, Mix(off, on, tc));
    g.RoundStroke(r, 11, WithA(T.border2, 1 - tc));
    float d = 16.f, w = d + 5.f * it.pressT;
    float kx = r.x + 3 + (r.w - 6 - w) * t;
    Rect knob{kx, r.y + 3, w, d};
    g.Round(knob, 8, Mix(T.faint, T.onPrimary, tc));
    if (busy)
        Spinner(knob.Cx(), knob.Cy(), 4.5f, T.accent, 1.6f);
    PopAlpha();
    return changed;
}

bool UI::Slider(ID id, const Rect &r, int &v, int lo, int hi, bool enabled)
{
    Interact it = Press(id, r.Inset(-2, -6), enabled);
    bool changed = false;
    if (it.held)
    {
        float lx = Local().x;
        float f = Clamp01((lx - r.x) / r.w);
        int nv = lo + (int)std::lround(f * (hi - lo));
        if (nv != v)
        {
            v = nv;
            changed = true;
        }
    }
    float f = hi > lo ? (float)(std::clamp(v, lo, hi) - lo) / (hi - lo) : 0.f;
    float fx = Spring(HashId(id, 0xC1), f, 700.f, 50.f);
    PushAlpha(enabled ? 1.f : 0.4f);
    float cy = r.Cy();
    Rect track{r.x, cy - 2, r.w, 4};
    g.Round(track, 2, T.panel3);
    g.Round({r.x, cy - 2, std::max(4.f, r.w * fx), 4}, 2, T.accent);
    float tx = r.x + r.w * fx;
    float rad = 7.f + 1.5f * std::max(it.hoverT, it.pressT);
    if (it.held)
        g.Circle(tx, cy, rad + 6, WithA(T.accent, 0.18f));
    g.Circle(tx, cy, rad, T.primary);
    g.CircleStroke(tx, cy, rad, Mix(T.border2, T.accent, it.hoverT), 1.f);
    PopAlpha();
    return changed;
}

bool UI::Segmented(ID id, const Rect &r, const std::vector<std::wstring> &items, int &sel)
{
    g.Round(r, 10, T.field);
    g.RoundStroke(r, 10, T.border);
    int n = (int)items.size();
    float sw = (r.w - 8) / n;
    float tx = Spring(HashId(id, 0xD1), r.x + 4 + sw * sel, 420.f, 32.f);
    Rect thumb{tx, r.y + 4, sw, r.h - 8};
    g.Round(thumb, 8, T.thumb);
    g.RoundStroke(thumb, 8, T.border2);
    bool changed = false;
    for (int i = 0; i < n; i++)
    {
        Rect s{r.x + 4 + sw * i, r.y + 4, sw, r.h - 8};
        Interact it = Press(HashId(id, i + 1), s);
        if (it.clicked && sel != i)
        {
            sel = i;
            changed = true;
        }
        Color c = i == sel ? T.text : Mix(T.dim, T.text, it.hoverT * 0.7f);
        g.Text(items[i], s, c, F::Semi, kCenter);
    }
    return changed;
}

void UI::Spinner(float cx, float cy, float r, Color c, float w)
{
    float a = (float)std::fmod(time * 6.0, 6.2831853);
    float sweep = 3.6f + 1.2f * (float)std::sin(time * 2.4);
    g.Arc(cx, cy, r, a, sweep, c, w);
    Keep();
}

void UI::Skeleton(const Rect &r, float rad)
{
    g.Round(r, rad, T.panel2);
    float p = (float)std::fmod(time * 0.9, 1.6) - 0.3f;
    PushClip(r);
    g.RadialGlow(r.x + r.w * p, r.Cy(), r.w * 0.35f, r.h * 2, WithA(T.text, 0.045f));
    PopClip();
    Keep();
}

void UI::Tag(const Rect &r, std::wstring_view text, Color c)
{
    g.Round(r, 6, WithA(c, 0.13f));
    g.Text(text, r, c, F::Caption, kCenter);
}

std::vector<Span> UI::StepSpans(const std::wstring &text, const Binds &binds) const
{
    std::vector<Span> spans;
    for (auto &t : TokenizeSteps(text, binds))
    {
        Color c = t.kind == 0   ? T.text
                  : t.kind == 1 ? T.accentHi
                  : t.kind == 2 ? T.amber
                  : t.kind == 3 ? T.accent2
                  : t.kind == 5 ? T.pink
                                : T.red;
        spans.push_back({(UINT32)t.start, (UINT32)t.len, c});
    }
    return spans;
}

// ---------------------------------------------------------------- text box

static bool WordChar(wchar_t c)
{
    return iswalnum(c) || c == L'_' || c == L'{' || c == L'}';
}

static int WordLeft(const std::wstring &s, int i)
{
    while (i > 0 && !WordChar(s[i - 1]))
        i--;
    while (i > 0 && WordChar(s[i - 1]))
        i--;
    return i;
}

static int WordRight(const std::wstring &s, int i)
{
    int n = (int)s.size();
    while (i < n && !WordChar(s[i]))
        i++;
    while (i < n && WordChar(s[i]))
        i++;
    return i;
}

static int HitPos(IDWriteTextLayout *l, float x, float y)
{
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    l->HitTestPoint(x, y, &trailing, &inside, &m);
    return (int)(m.textPosition + (trailing ? m.length : 0));
}

TextBoxResult UI::TextBox(ID id, const Rect &r, std::wstring &text, const TextBoxOpts &o)
{
    TextBoxResult res;
    TbState &st = tb_[id];
    Interact it = Press(id, r);
    if (Hover(r))
        cursor = IDC_IBEAM;
    F font = o.mono ? F::Mono : F::Body;
    float padX = 12.f, padY = o.multiline ? 10.f : 0.f;
    Rect inner{r.x + o.padLeft, r.y + padY, r.w - padX - o.padLeft, r.h - 2 * padY};
    auto layout = [&]() {
        return g.Layout(text, font, o.multiline ? inner.w : 100000.f, o.multiline ? 100000.f : inner.h, kLeft,
                        !o.multiline, o.multiline, o.colorize != nullptr);
    };
    IDWriteTextLayout *L = layout();
    if (!L)
        return res;
    int len = (int)text.size();
    st.caret = std::clamp(st.caret, 0, len);
    st.anchor = std::clamp(st.anchor, 0, len);

    auto localHit = [&]() {
        D2D1_POINT_2F p = Local();
        return HitPos(L, p.x - inner.x + st.scrollX, p.y - inner.y + st.scrollY);
    };
    if (it.pressed)
    {
        if (focus != id)
            st.undo.clear();
        focus = id;
        st.dragging = true;
        int pos = localHit();
        if (in.dbl)
        {
            st.anchor = pos;
            while (st.anchor > 0 && WordChar(text[st.anchor - 1]))
                st.anchor--;
            st.caret = pos;
            while (st.caret < len && WordChar(text[st.caret]))
                st.caret++;
            st.dragging = false;
        }
        else
        {
            st.caret = pos;
            if (!in.shift)
                st.anchor = pos;
        }
        st.blink = time;
        st.prefX = -1;
    }
    if (st.dragging && in.down && focus == id && active == id)
        st.caret = localHit();
    if (!in.down)
        st.dragging = false;
    bool focused = focus == id;
    if (focused)
        focusClaimed_ = true;

    if (focused)
    {
        auto selRange = [&](int &a, int &b) {
            a = std::min(st.caret, st.anchor);
            b = std::max(st.caret, st.anchor);
            return a != b;
        };
        auto snapshot = [&]() {
            if (time - st.lastEdit > 0.7 || st.undo.empty())
            {
                st.undo.push_back({text, st.caret});
                if (st.undo.size() > 100)
                    st.undo.erase(st.undo.begin());
            }
            st.lastEdit = time;
        };
        auto erase = [&](int a, int b) {
            snapshot();
            text.erase(a, b - a);
            st.caret = st.anchor = a;
            res.changed = true;
        };
        auto insert = [&](std::wstring s) {
            if (!o.multiline)
            {
                for (auto &c : s)
                    if (c == L'\n' || c == L'\t')
                        c = L' ';
            }
            s.erase(std::remove(s.begin(), s.end(), L'\r'), s.end());
            int a, b;
            if (selRange(a, b))
                erase(a, b);
            size_t room = o.maxLen > text.size() ? o.maxLen - text.size() : 0;
            if (s.size() > room)
                s.resize(room);
            if (s.empty())
                return;
            snapshot();
            text.insert(st.caret, s);
            st.caret += (int)s.size();
            st.anchor = st.caret;
            res.changed = true;
        };
        auto lineStart = [&](int i) {
            while (i > 0 && text[i - 1] != L'\n')
                i--;
            return i;
        };
        auto lineEnd = [&](int i) {
            while (i < (int)text.size() && text[i] != L'\n')
                i++;
            return i;
        };
        for (auto &k : in.keys)
        {
            int a, b;
            bool sel = selRange(a, b);
            bool moved = true;
            switch (k.vk)
            {
            case VK_LEFT:
                if (sel && !k.shift)
                    st.caret = a;
                else
                    st.caret = k.ctrl ? WordLeft(text, st.caret) : std::max(0, st.caret - 1);
                break;
            case VK_RIGHT:
                if (sel && !k.shift)
                    st.caret = b;
                else
                    st.caret = k.ctrl ? WordRight(text, st.caret) : std::min((int)text.size(), st.caret + 1);
                break;
            case VK_HOME:
                st.caret = (k.ctrl || !o.multiline) ? 0 : lineStart(st.caret);
                break;
            case VK_END:
                st.caret = (k.ctrl || !o.multiline) ? (int)text.size() : lineEnd(st.caret);
                break;
            case VK_UP:
            case VK_DOWN: {
                if (!o.multiline)
                {
                    st.caret = k.vk == VK_UP ? 0 : (int)text.size();
                    break;
                }
                IDWriteTextLayout *cur = layout();
                float px, py;
                DWRITE_HIT_TEST_METRICS m{};
                cur->HitTestTextPosition(st.caret, FALSE, &px, &py, &m);
                if (st.prefX < 0)
                    st.prefX = px;
                float ny = k.vk == VK_UP ? m.top - m.height * 0.5f : m.top + m.height * 1.5f;
                if (ny < 0)
                    st.caret = 0;
                else
                    st.caret = HitPos(cur, st.prefX, ny);
                if (!k.shift)
                    st.anchor = st.caret;
                continue;
            }
            case VK_BACK:
                if (sel)
                    erase(a, b);
                else if (st.caret > 0)
                    erase(k.ctrl ? WordLeft(text, st.caret) : st.caret - 1, st.caret);
                moved = false;
                break;
            case VK_DELETE:
                if (sel)
                    erase(a, b);
                else if (st.caret < (int)text.size())
                    erase(st.caret, k.ctrl ? WordRight(text, st.caret) : st.caret + 1);
                moved = false;
                break;
            case VK_RETURN:
                if (o.multiline && !k.ctrl)
                    insert(L"\n");
                else
                    res.submitted = true;
                moved = false;
                break;
            case 'A':
                if (k.ctrl)
                {
                    st.anchor = 0;
                    st.caret = (int)text.size();
                    st.blink = time;
                    continue;
                }
                moved = false;
                break;
            case 'C':
            case 'X':
                if (k.ctrl && sel)
                {
                    SetClipboardText(hwnd, text.substr(a, b - a));
                    if (k.vk == 'X')
                        erase(a, b);
                }
                moved = false;
                break;
            case 'V':
                if (k.ctrl)
                    insert(ClipboardText(hwnd));
                moved = false;
                break;
            case 'Z':
                if (k.ctrl && !st.undo.empty())
                {
                    text = st.undo.back().first;
                    st.caret = st.anchor = std::min((int)text.size(), st.undo.back().second);
                    st.undo.pop_back();
                    st.lastEdit = -10;
                    res.changed = true;
                }
                moved = false;
                break;
            default:
                moved = false;
                break;
            }
            if (moved)
            {
                if (!k.shift)
                    st.anchor = st.caret;
                st.prefX = -1;
            }
            st.blink = time;
        }
        for (wchar_t c : in.chars)
            if (c >= 32 && c != 127)
            {
                insert(std::wstring(1, c));
                st.blink = time;
                st.prefX = -1;
            }
        if (res.changed)
            L = layout();
    }

    // ---- draw
    float focusT = Anim(HashId(id, 0xE1), focused ? 1.f : 0.f, 16.f);
    Color border = o.error ? WithA(T.amber, 0.7f) : Mix(T.border, T.border2, it.hoverT);
    g.Round(r, 10, T.field);
    g.RoundStroke(r, 10, Mix(border, T.accent, focusT));
    if (focusT > 0.01f)
        g.RoundStroke(r.Inset(-3), 13, WithA(T.accent, 0.22f * focusT), 2.f);

    float px = 0, py = 0;
    DWRITE_HIT_TEST_METRICS cm{};
    L->HitTestTextPosition(st.caret, FALSE, &px, &py, &cm);
    if (focused)
    {
        if (o.multiline)
        {
            DWRITE_TEXT_METRICS tm{};
            L->GetMetrics(&tm);
            if (cm.top - st.scrollY < 0)
                st.scrollY = cm.top;
            if (cm.top + cm.height - st.scrollY > inner.h)
                st.scrollY = cm.top + cm.height - inner.h;
            st.scrollY = std::clamp(st.scrollY, 0.f, std::max(0.f, tm.height - inner.h));
        }
        else
        {
            if (px - st.scrollX > inner.w - 2)
                st.scrollX = px - inner.w + 2;
            if (px - st.scrollX < 0)
                st.scrollX = px;
        }
    }
    else
    {
        st.scrollX = 0;
    }

    PushClip(o.multiline ? Rect{r.x + 2, r.y + 2, r.w - 4, r.h - 4} : inner.Inset(-2, 0));
    float ox = inner.x - st.scrollX, oy = inner.y - st.scrollY;
    if (focused && st.caret != st.anchor)
    {
        int a = std::min(st.caret, st.anchor), b = std::max(st.caret, st.anchor);
        UINT32 count = 0;
        L->HitTestTextRange(a, b - a, 0, 0, nullptr, 0, &count);
        std::vector<DWRITE_HIT_TEST_METRICS> ms(count);
        if (count)
            L->HitTestTextRange(a, b - a, 0, 0, ms.data(), count, &count);
        for (auto &m : ms)
            g.Round({ox + m.left, oy + m.top, std::max(3.f, m.width), m.height}, 3, WithA(T.accent, 0.38f));
    }
    if (text.empty())
    {
        g.Text(o.placeholder, o.multiline ? Rect{inner.x, inner.y, inner.w, 20} : inner, T.faint, font, kLeft,
               !o.multiline);
    }
    else
    {
        if (o.colorize)
        {
            auto spans = StepSpans(text, *o.colorize);
            g.ApplySpans(L, spans.data(), spans.size());
        }
        g.DrawLayout(L, ox, oy, o.colorize ? T.dim : T.text);
    }
    if (focused)
    {
        double phase = std::fmod(time - st.blink, 1.06);
        if (phase < 0.6)
            g.Fill({ox + px - 0.5f, oy + cm.top + 1, 1.6f, cm.height - 2}, T.accentHi);
        WakeIn(phase < 0.6 ? 0.6 - phase : 1.06 - phase);
    }
    PopClip();
    res.focused = focused;
    return res;
}

bool UI::NumberField(ID id, const Rect &r, double &v, double lo, double hi, double step, int decimals)
{
    auto fmt = [&](double x) { return Fmt(L"%.*f", decimals, x); };
    std::wstring &text = numText_[id];
    bool changed = false;
    if (focus != id)
        text = fmt(v);
    TextBoxOpts o;
    o.maxLen = 12;
    auto res = TextBox(id, {r.x, r.y, r.w, r.h}, text, o);
    if (res.changed)
    {
        wchar_t *end = nullptr;
        std::wstring t = text;
        for (auto &c : t)
            if (c == L',')
                c = L'.';
        double x = wcstod(t.c_str(), &end);
        if (end && *end == 0 && !t.empty() && x >= lo && x <= hi)
        {
            v = x;
            changed = true;
        }
    }
    float bw = 26, bh = r.h - 10;
    Rect minus{r.R() - 6 - bw * 2 - 2, r.y + 5, bw, bh}, plus{r.R() - 6 - bw, r.y + 5, bw, bh};
    auto stepBtn = [&](const Rect &b, const wchar_t *glyph, double dir) {
        Interact it = Press(HashId(id, dir > 0 ? 0x51 : 0x52), b);
        g.Round(b, 7, Mix(T.panel3, T.border2, it.hoverT));
        PushScale(1.f - 0.1f * it.pressT, b.Cx(), b.Cy());
        g.Text(glyph, b, Mix(T.dim, T.text, it.hoverT), F::IconSm, kCenter);
        PopXf();
        if (it.clicked)
        {
            double x = std::clamp(std::round((v + dir * step) / step) * step, lo, hi);
            if (x != v)
            {
                v = x;
                changed = true;
                text = fmt(v);
            }
        }
    };
    stepBtn(minus, L"", -1);
    stepBtn(plus, L"", 1);
    return changed;
}

// ---------------------------------------------------------------- scrolling

float UI::BeginScroll(ID id, const Rect &view)
{
    ScrollState &s = scroll_[id];
    s.view = view.h;
    float maxPos = std::max(0.f, s.content - s.view);
    if (Hover(view))
        wheelTargetNext_ = id;
    if (id == wheelTarget_ && in.wheel != 0 && inputOn)
    {
        s.target -= in.wheel * 96.f;
        s.lastMove = time;
    }
    s.target = std::clamp(s.target, 0.f, maxPos);
    if (reduceMotion || s.drag)
    {
        s.pos = s.target;
    }
    else
    {
        s.pos += (s.target - s.pos) * (1.f - std::exp(-dt * 18.f));
        if (std::fabs(s.target - s.pos) < 0.3f)
            s.pos = s.target;
        else
            needFrame = true;
    }
    PushClip(view);
    PushOffset(0, -std::round(s.pos));
    scrollStack_.push_back({id, view});
    return s.pos;
}

void UI::EndScroll(ID id, float contentH, Color fade)
{
    PopXf();
    PopClip();
    Rect view = scrollStack_.empty() ? Rect{} : scrollStack_.back().view;
    if (!scrollStack_.empty())
        scrollStack_.pop_back();
    ScrollState &s = scroll_[id];
    s.content = contentH;
    float maxPos = std::max(0.f, s.content - s.view);
    if (maxPos < 1.f)
        return;
    if (fade.a > 0)
    {
        float more = Anim(HashId(id, 0xF4), s.pos < maxPos - 2 ? 1.f : 0.f, 10.f);
        Color clear = fade;
        clear.a = 0;
        if (more > 0.01f)
            g.RoundGrad({view.x, view.B() - 36, view.w, 36}, 0, WithA(clear, 0), WithA(fade, more), true);
    }
    float trackH = view.h - 8;
    float thumbH = std::max(36.f, trackH * s.view / s.content);
    float thumbY = view.y + 4 + (trackH - thumbH) * (s.pos / maxPos);
    Rect hit{view.R() - 14, view.y, 14, view.h};
    Interact it = Press(HashId(id, 0xF1), hit);
    if (it.pressed)
    {
        s.drag = true;
        s.dragY = in.my;
        s.dragPos = s.target;
    }
    if (s.drag)
    {
        if (!in.down)
            s.drag = false;
        else
            s.target = std::clamp(s.dragPos + (in.my - s.dragY) * maxPos / std::max(1.f, trackH - thumbH), 0.f, maxPos);
    }
    bool show = Hover(view) || s.drag || time - s.lastMove < 0.8;
    if (time - s.lastMove < 0.8)
        WakeIn(0.82 - (time - s.lastMove));
    float vis = Anim(HashId(id, 0xF2), show ? 1.f : 0.f, 10.f);
    float wide = Anim(HashId(id, 0xF3), (it.hoverT > 0.5f || s.drag) ? 1.f : 0.f, 16.f);
    float w = 4 + 2 * wide;
    g.Round({view.R() - 4 - w, thumbY, w, thumbH}, w / 2, WithA(T.text, (0.16f + 0.14f * wide) * vis));
}

void UI::ScrollReset(ID id)
{
    ScrollState &s = scroll_[id];
    s.pos = s.target = 0;
}

// ---------------------------------------------------------------- clipboard

std::wstring ClipboardText(HWND h)
{
    std::wstring s;
    if (!OpenClipboard(h))
        return s;
    if (HANDLE d = GetClipboardData(CF_UNICODETEXT))
    {
        if (auto *p = (const wchar_t *)GlobalLock(d))
        {
            s = p;
            GlobalUnlock(d);
        }
    }
    CloseClipboard();
    return s;
}

void SetClipboardText(HWND h, const std::wstring &s)
{
    if (!OpenClipboard(h))
        return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, bytes))
    {
        memcpy(GlobalLock(m), s.c_str(), bytes);
        GlobalUnlock(m);
        if (!SetClipboardData(CF_UNICODETEXT, m))
            GlobalFree(m);
    }
    CloseClipboard();
}
