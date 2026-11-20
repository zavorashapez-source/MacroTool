// Main page: what's plugged in, shown big on a stage, and what every key or button fires.
#include "app.h"
#include <algorithm>
#include <cmath>

namespace
{

const wchar_t *kGlyph[3] = {L"\xE765", L"\xE962", L"\xE7FC"}; // keyboard, mouse, game
const wchar_t *kKind[3] = {L"Keyboard", L"Mouse", L"Controller"};
const Color kBlack = Rgb(0x000000), kWhite = Rgb(0xFFFFFF);

// generic shift/ctrl/alt triggers light up the left key
int NormVk(int vk)
{
    if (vk == VK_SHIFT)
        return VK_LSHIFT;
    if (vk == VK_CONTROL)
        return VK_LCONTROL;
    if (vk == VK_MENU)
        return VK_LMENU;
    return vk;
}

bool IsMouseKey(int vk)
{
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2 ||
           vk == kWheelUp || vk == kWheelDown;
}

// each category gets its own colour on the pictures
Color CatColor(const std::wstring &cat)
{
    Color c = cat == L"Editing"    ? T.accent
              : cat == L"Building" ? T.amber
              : cat == L"Movement" ? T.green
              : cat == L"Utility"  ? T.pink
                                   : T.red;
    // pulled toward grey so the pictures read as hardware, not toys
    return Mix(c, T.dim, 0.32f);
}

// legend colour: lighter on dark, darker on light, so it stays readable on the caps
Color Ink(Color c)
{
    return T.bg.r > 0.5f ? Mix(c, kBlack, 0.32f) : Mix(c, kWhite, 0.25f);
}

std::wstring Mode(const Macro &m)
{
    return m.toggle ? L"toggle" : m.repeat ? L"repeats while held" : L"on press";
}

struct KbKey
{
    float x, y, w;
    int vk;
    std::wstring label;
};

// tenkeyless ansi, in key units
const std::vector<KbKey> &KbLayout()
{
    static const std::vector<KbKey> keys = [] {
        std::vector<KbKey> k;
        auto add = [&](float x, float y, float w, int vk, std::wstring l) { k.push_back({x, y, w, vk, std::move(l)}); };
        auto run = [&](float x, float y, const wchar_t *chars) {
            for (int i = 0; chars[i]; i++)
                add(x + i, y, 1, chars[i], std::wstring(1, chars[i]));
        };
        add(0, 0, 1, VK_ESCAPE, L"Esc");
        for (int i = 0; i < 12; i++)
            add(2 + i + (i / 4) * 0.5f, 0, 1, VK_F1 + i, L"F" + std::to_wstring(i + 1));
        add(15.25f, 0, 1, VK_SNAPSHOT, L"PrtSc");
        add(16.25f, 0, 1, VK_SCROLL, L"ScrLk");
        add(17.25f, 0, 1, VK_PAUSE, L"Pause");

        add(0, 1.25f, 1, VK_OEM_3, L"~");
        run(1, 1.25f, L"123456789");
        add(10, 1.25f, 1, '0', L"0");
        add(11, 1.25f, 1, VK_OEM_MINUS, L"-");
        add(12, 1.25f, 1, VK_OEM_PLUS, L"=");
        add(13, 1.25f, 2, VK_BACK, L"Back");
        add(15.25f, 1.25f, 1, VK_INSERT, L"Ins");
        add(16.25f, 1.25f, 1, VK_HOME, L"Home");
        add(17.25f, 1.25f, 1, VK_PRIOR, L"PgUp");

        add(0, 2.25f, 1.5f, VK_TAB, L"Tab");
        run(1.5f, 2.25f, L"QWERTYUIOP");
        add(11.5f, 2.25f, 1, VK_OEM_4, L"[");
        add(12.5f, 2.25f, 1, VK_OEM_6, L"]");
        add(13.5f, 2.25f, 1.5f, VK_OEM_5, L"\\");
        add(15.25f, 2.25f, 1, VK_DELETE, L"Del");
        add(16.25f, 2.25f, 1, VK_END, L"End");
        add(17.25f, 2.25f, 1, VK_NEXT, L"PgDn");

        add(0, 3.25f, 1.75f, VK_CAPITAL, L"Caps");
        run(1.75f, 3.25f, L"ASDFGHJKL");
        add(10.75f, 3.25f, 1, VK_OEM_1, L";");
        add(11.75f, 3.25f, 1, VK_OEM_7, L"'");
        add(12.75f, 3.25f, 2.25f, VK_RETURN, L"Enter");

        add(0, 4.25f, 2.25f, VK_LSHIFT, L"Shift");
        run(2.25f, 4.25f, L"ZXCVBNM");
        add(9.25f, 4.25f, 1, VK_OEM_COMMA, L",");
        add(10.25f, 4.25f, 1, VK_OEM_PERIOD, L".");
        add(11.25f, 4.25f, 1, VK_OEM_2, L"/");
        add(12.25f, 4.25f, 2.75f, VK_RSHIFT, L"Shift");
        add(16.25f, 4.25f, 1, VK_UP, L"\x2191");

        add(0, 5.25f, 1.25f, VK_LCONTROL, L"Ctrl");
        add(1.25f, 5.25f, 1.25f, VK_LWIN, L"Win");
        add(2.5f, 5.25f, 1.25f, VK_LMENU, L"Alt");
        add(3.75f, 5.25f, 6.25f, VK_SPACE, L"");
        add(10, 5.25f, 1.25f, VK_RMENU, L"Alt");
        add(11.25f, 5.25f, 1.25f, VK_RWIN, L"Win");
        add(12.5f, 5.25f, 1.25f, VK_APPS, L"Menu");
        add(13.75f, 5.25f, 1.25f, VK_RCONTROL, L"Ctrl");
        add(15.25f, 5.25f, 1, VK_LEFT, L"\x2190");
        add(16.25f, 5.25f, 1, VK_DOWN, L"\x2193");
        add(17.25f, 5.25f, 1, VK_RIGHT, L"\x2192");
        return k;
    }();
    return keys;
}
constexpr float kKbW = 18.25f, kKbH = 6.25f;

// closed catmull-rom through the control points, for the mouse and controller outlines
std::vector<D2D1_POINT_2F> Smooth(const std::vector<D2D1_POINT_2F> &p, int steps = 10)
{
    std::vector<D2D1_POINT_2F> out;
    size_t n = p.size();
    for (size_t i = 0; i < n; i++)
    {
        const auto &a = p[(i + n - 1) % n], &b = p[i], &c = p[(i + 1) % n], &d = p[(i + 2) % n];
        for (int s = 0; s < steps; s++)
        {
            float t = s / (float)steps, t2 = t * t, t3 = t2 * t;
            auto f = [&](float p0, float p1, float p2, float p3) {
                return 0.5f * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                               (3 * p1 - p0 - 3 * p2 + p3) * t3);
            };
            out.push_back({f(a.x, b.x, c.x, d.x), f(a.y, b.y, c.y, d.y)});
        }
    }
    return out;
}

// mirror the right half (x >= 200) of a 400 wide outline into a full loop
std::vector<D2D1_POINT_2F> Mirrored(std::vector<D2D1_POINT_2F> right)
{
    std::vector<D2D1_POINT_2F> all = right;
    for (size_t i = right.size(); i-- > 1;)
        if (right[i].x != 200)
            all.push_back({400 - right[i].x, right[i].y});
    return all;
}

void Outline(Gfx &g, const std::vector<D2D1_POINT_2F> &pts, Color c, float w)
{
    for (size_t i = 0; i < pts.size(); i++)
    {
        auto &a = pts[i], &b = pts[(i + 1) % pts.size()];
        g.Line(a.x, a.y, b.x, b.y, c, w);
    }
}

// a keycap: darker skirt, lighter top that sinks when pressed
void Cap(Gfx &g, const Rect &r, Color face, float press, float rad)
{
    g.Round(r, rad, Mix(face, kBlack, 0.42f));
    Rect top{r.x + 2.5f, r.y + 1.5f + 2.f * press, r.w - 5, r.h - 6.f};
    g.RoundGrad(top, rad - 1.5f, Mix(face, kWhite, 0.10f - 0.06f * press), Mix(face, kBlack, 0.05f), true);
    g.Fill({top.x + rad, top.y + 0.5f, top.w - 2 * rad, 1}, WithA(kWhite, 0.10f));
}

Rect CapTop(const Rect &r, float press)
{
    return {r.x + 2.5f, r.y + 1.5f + 2.f * press, r.w - 5, r.h - 6.f};
}

std::wstring PadLabel(int vk, PadStyle st)
{
    static const wchar_t *xbox[] = {L"A",    L"B",    L"X",        L"Y",          L"RB",         L"LB",
                                    L"LT",   L"RT",   L"D-pad up", L"D-pad down", L"D-pad left", L"D-pad right",
                                    L"Menu", L"View", L"LS",       L"RS"};
    static const wchar_t *ps[] = {L"Cross",   L"Circle", L"Square",   L"Triangle",   L"R1",         L"L1",
                                  L"L2",      L"R2",     L"D-pad up", L"D-pad down", L"D-pad left", L"D-pad right",
                                  L"Options", L"Create", L"L3",       L"R3"};
    // switch labels sit on the opposite spots: xinput A (bottom) is B
    static const wchar_t *nin[] = {L"B",  L"A",      L"Y",        L"X",          L"R",          L"L",
                                   L"ZL", L"ZR",     L"D-pad up", L"D-pad down", L"D-pad left", L"D-pad right",
                                   L"+",  L"\x2212", L"LS",       L"RS"};
    if (!IsPadKey(vk))
        return Pretty(KeyName(vk));
    int i = vk - kPadFirst;
    return st == PadStyle::PlayStation ? ps[i] : st == PadStyle::Nintendo ? nin[i] : xbox[i];
}

std::wstring Joined(const std::vector<const Macro *> &ms)
{
    std::wstring s;
    for (auto *m : ms)
        s += (s.empty() ? L"" : L", ") + m->name;
    return s;
}

} // namespace

// ---------------------------------------------------------------- devices + shared bits

void App::RefreshDevices()
{
    if (preview_ || !hwnd_)
        return;
    if (devicesBusy_)
    {
        devicesAgain_ = true; // plugging in fires a burst of device changes
        return;
    }
    devicesBusy_ = true;
    RunAsync([this]() -> std::function<void()> {
        std::vector<Device> list = ListDevices();
        return [this, list]() {
            devices_ = list;
            devicesBusy_ = false;
            if (devicesAgain_)
            {
                devicesAgain_ = false;
                RefreshDevices();
            }
        };
    });
}

float App::KeyFlash(const std::vector<const Macro *> &ms)
{
    float fire = 0;
    for (auto *m : ms)
        if (auto f = flash_.find(m->id); f != flash_.end())
            fire = std::max(fire, 1.f - Clamp01((float)(ui.time - f->second) / 0.7f));
    if (fire > 0)
        ui.Keep();
    return fire;
}

// 0..1 since the page opened or the device was switched
float App::MainAppear(float delay, float dur)
{
    if (ui.reduceMotion)
        return 1.f;
    float t = Clamp01((float)(ui.time - std::max(pageStart_, mainSwitchAt_) - delay) / dur);
    if (t < 1)
        ui.Keep();
    return EaseOut(t);
}

void App::Stage(const Rect &r)
{
    g.Shadow(r, 24, 30, 0.22f, 10);
    g.RoundGrad(r, 24, Mix(T.panel, T.panel2, 0.55f), Mix(T.bg, T.panel, 0.55f), true);
    ui.PushClip(r);
    // spotlight + a faint dot grid, so the device sits in a space and not in a box
    g.RadialGlow(r.Cx(), r.y + r.h * 0.52f, r.w * 0.46f, r.h * 0.55f, WithA(T.accent, 0.10f));
    for (float gy = r.y + 22; gy < r.B(); gy += 26)
        for (float gx = r.x + 22; gx < r.R(); gx += 26)
            g.Fill({gx, gy, 1.2f, 1.2f}, WithA(T.text, 0.045f));
    ui.PopClip();
    g.RoundStroke(r, 24, T.border);
    g.Fill({r.x + 30, r.y + 1, r.w - 60, 1}, WithA(T.text, 0.06f));
}

// ---------------------------------------------------------------- device switcher

void App::MainTabs(const Rect &r, const Device *const devs[3], const int counts[3], const bool online[3])
{
    float gap = 14, tw = (r.w - gap * 2) / 3;
    // one highlight that slides to the picked device
    float pos = ui.Spring(HashId(L"main-tab-pos"), (float)mainDev_, 300.f, 28.f);
    Rect slide{r.x + pos * (tw + gap), r.y, tw, r.h};
    g.Shadow(slide, 18, 22, 0.30f, 8);
    g.RoundGrad(slide, 18, Mix(T.panel2, T.text, 0.03f), T.panel2, true);
    g.RoundStroke(slide, 18, Mix(T.border2, T.accent, 0.45f));
    g.Fill({slide.x + 22, slide.y + 1, slide.w - 44, 1}, WithA(T.text, 0.10f));
    for (int i = 0; i < 3; i++)
    {
        Rect tr{r.x + i * (tw + gap), r.y, tw, r.h};
        float t = RowAppear(i);
        ID id = HashId(L"main-tab", (uint64_t)i);
        Interact in = ui.Press(id, tr);
        float sel = ui.Anim(HashId(id, 2), mainDev_ == i ? 1.f : 0.f, 14.f);
        if (in.clicked && mainDev_ != i)
        {
            mainDir_ = i > mainDev_ ? 1 : -1;
            mainDev_ = i;
            mainSwitchAt_ = ui.time;
        }
        float sp = ui.Spring(HashId(id, 0xB7), in.held ? 1.f : 0.f, 460.f, 26.f);
        ui.PushAlpha(t);
        ui.PushOffset(0, 12 * (1 - t));
        ui.PushScale(1 - 0.02f * sp, tr.Cx(), tr.Cy());
        if (sel < 0.99f)
            g.Round(tr, 18, WithA(T.panel2, 0.55f * in.hoverT * (1 - sel)));
        Rect ic{tr.x + 18, tr.Cy() - 23, 46, 46};
        g.Round(ic, 14, Mix(Mix(T.panel3, T.panel2, 0.4f), T.accent, 0.20f * sel));
        g.Text(kGlyph[i], ic, Mix(Mix(T.dim, T.text, in.hoverT), T.accentHi, sel), F::IconLg, kCenter);
        // connected dot, cut out of the icon tile
        g.Circle(ic.R() - 2, ic.y + 2, 6.5f, Mix(T.panel, T.panel2, sel));
        g.Circle(ic.R() - 2, ic.y + 2, 4, online[i] ? T.green : T.faint);

        float tx = ic.R() + 16, tw2 = tr.R() - tx - 70;
        const Device *d = devs[i];
        g.Text(kKind[i], {tx, tr.y + 17, tw2, 16}, Mix(T.faint, T.dim, sel), F::Caption);
        g.Text(d ? d->model : L"Not connected", {tx, tr.y + 33, tw2, 22}, d ? T.text : T.dim, F::Semi);
        std::wstring sub = d ? (d->brand.empty() ? d->link : d->brand + L"  \x00B7  " + d->link) : L"Plug it in";
        g.Text(sub, {tx, tr.y + 55, tw2, 18}, T.dim, F::Small);
        // macro count on the right
        Rect cr{tr.R() - 58, tr.Cy() - 20, 42, 40};
        g.Text(std::to_wstring(counts[i]), {cr.x, cr.y, cr.w, 26}, counts[i] ? T.text : T.faint, F::H2, kCenter);
        g.Text(counts[i] == 1 ? L"macro" : L"macros", {cr.x - 10, cr.y + 25, cr.w + 20, 14}, T.faint, F::Caption,
               kCenter);
        ui.PopXf();
        ui.PopXf();
        ui.PopAlpha();
    }
}

// ---------------------------------------------------------------- keyboard

void App::DrawKeyboard(const Rect &r, const BindMap &binds)
{
    float u = std::min(r.w / (kKbW + 1.2f), r.h / (kKbH + 1.2f));
    float kw = kKbW * u, kh = kKbH * u;
    float ox = r.Cx() - kw / 2, oy = r.Cy() - kh / 2;
    Rect body{ox - u * 0.6f, oy - u * 0.6f, kw + u * 1.2f, kh + u * 1.2f};

    // floor shadow, aluminium-ish case, recessed plate
    g.RadialGlow(body.Cx(), body.B() + 4, body.w * 0.55f, u * 0.8f, WithA(kBlack, 0.45f));
    g.Shadow(body, 22, 34, 0.36f, 14);
    g.RoundGrad(body, 22, Mix(T.panel3, kWhite, 0.06f), Mix(T.panel2, kBlack, 0.20f), true);
    g.RoundStroke(body, 22, WithA(kWhite, 0.07f), 1.f);
    g.Fill({body.x + 28, body.y + 1, body.w - 56, 1}, WithA(kWhite, 0.16f));
    Rect plate = body.Inset(u * 0.42f);
    bool light = T.bg.r > 0.5f;
    g.Round(plate, 13, Mix(T.panel2, kBlack, light ? 0.13f : 0.38f));

    int toggleVk = 0, menuVk = 0;
    KeyParse(cfg_.toggleKey, toggleVk);
    if (cfg_.menu)
        KeyParse(cfg_.menuKey, menuVk);

    // backlight under the keys with macros, before the caps so they sit on the glow
    for (auto &k : KbLayout())
        if (auto b = binds.find(k.vk); b != binds.end())
            g.RadialGlow(ox + (k.x + k.w / 2) * u, oy + (k.y + 0.8f) * u, k.w * u * 0.55f, u * 0.42f,
                         WithA(CatColor(b->second.front()->category), 0.10f));

    ID base = HashId(L"kb-key");
    for (auto &k : KbLayout())
    {
        Rect kr{ox + k.x * u + 1.5f, oy + k.y * u + 1.5f, k.w * u - 3, u - 3};
        // keys rise in left to right
        float t = MainAppear(0.10f + k.x * 0.016f + k.y * 0.01f, 0.34f);
        auto b = binds.find(k.vk);
        bool bound = b != binds.end();
        bool sys = k.vk == toggleVk || k.vk == menuVk;
        bool live = !preview_ && engine_.LiveDown(k.vk);
        Color c = bound ? CatColor(b->second.front()->category) : T.dim;
        float fire = bound ? KeyFlash(b->second) : 0;
        ID id = HashId(base, (uint64_t)k.vk);
        Interact in = ui.Press(id, kr);
        if (in.hover && bound)
            mainHotNext_ = k.vk;
        float hot = ui.Anim(HashId(id, 11), (bound && mainHot_ == k.vk) || in.hover ? 1.f : 0.f, 16.f);
        float press = ui.Spring(HashId(id, 7), in.held || live || fire > 0.6f ? 1.f : 0.f, 620.f, 30.f);

        // bound keys stay close to the others: a hint of colour, a coloured legend and a small led
        Color face = bound ? Mix(T.key, c, 0.12f + 0.10f * hot + 0.22f * fire)
                     : sys ? Mix(T.key, kWhite, 0.07f)
                           : Mix(T.key, kWhite, 0.05f * hot);
        if (live)
            face = Mix(face, bound ? c : T.accent, 0.28f);
        ui.PushAlpha(t);
        ui.PushOffset(0, 10 * (1 - t));
        if (bound && hot > 0.01f)
            g.RoundStroke(kr.Inset(-2.5f), 8.5f, WithA(c, 0.5f * hot), 1.4f);
        Cap(g, kr, face, press, 6.5f);
        Rect top = CapTop(kr, press);
        if (sys && !bound) // app keys: a thin ring instead of a colour
            g.RoundStroke(top, 5, WithA(T.text, 0.30f), 1.f);
        if (bound)
            g.Round({top.Cx() - top.w * 0.2f, top.B() - 4.5f, top.w * 0.4f, 2}, 1, WithA(c, 0.7f + 0.3f * fire));
        Color tc = bound  ? Mix(Ink(c), T.text, 0.4f * fire)
                   : live ? kWhite
                   : sys  ? T.text
                          : Mix(T.dim, T.text, 0.35f + 0.4f * hot);
        F f = k.label.size() > 1 || u < 40 ? F::Tiny : F::SmallSemi;
        g.Text(k.label, top, tc, f, kCenter);
        if (bound && b->second.size() > 1)
            g.Circle(top.R() - 5, top.y + 5, 2.f, c);
        if (bound)
        {
            ui.Tip(id, Joined(b->second), kr);
            if (in.clicked)
                OpenEditor(b->second.front()->id);
        }
        else if (sys)
            ui.Tip(id, k.vk == toggleVk ? L"Turns macros on and off" : L"Opens the in-game menu", kr);
        ui.PopXf();
        ui.PopAlpha();
    }
}

// ---------------------------------------------------------------- mouse

void App::DrawMouse(const Rect &r, const BindMap &binds, std::vector<Anchor> &anchors)
{
    // drawn in a 120 x 190 box
    float s = std::min(r.w / 140.f, r.h / 200.f);
    float ox = r.Cx() - 60 * s, oy = r.Cy() - 95 * s;
    auto P = [&](float x, float y) { return D2D1_POINT_2F{ox + x * s, oy + y * s}; };
    auto R = [&](float x, float y, float w, float h) { return Rect{ox + x * s, oy + y * s, w * s, h * s}; };
    std::vector<D2D1_POINT_2F> ctrl = {P(60, 4),    P(92, 10),  P(110, 34), P(116, 80), P(114, 130),
                                       P(104, 168), P(84, 186), P(60, 190), P(36, 186), P(16, 168),
                                       P(6, 130),   P(4, 80),   P(10, 34),  P(28, 10)};
    auto body = Smooth(ctrl);
    Rect box = R(0, 0, 120, 190);
    float appear = MainAppear(0.08f, 0.5f);
    ui.PushAlpha(appear);
    ui.PushScale(0.92f + 0.08f * appear, box.Cx(), box.Cy());

    ID base = HashId(L"mouse-part");
    struct Look
    {
        bool bound;
        Color c;
        float fire, hot, press;
    };
    // hit test, tooltip, anchor for the callout, and how it should look this frame
    auto look = [&](int vk, const Rect &hit, D2D1_POINT_2F anchor, bool left, const wchar_t *tip) {
        auto b = binds.find(vk);
        Look l{b != binds.end(), T.dim, 0, 0, 0};
        ID id = HashId(base, (uint64_t)vk);
        Interact in = ui.Press(id, hit);
        if (l.bound)
        {
            l.c = CatColor(b->second.front()->category);
            l.fire = KeyFlash(b->second);
            anchors.push_back({vk, anchor.x, anchor.y, left});
            if (in.hover)
                mainHotNext_ = vk;
            ui.Tip(id, std::wstring(tip) + L": " + Joined(b->second), hit);
            if (in.clicked)
                OpenEditor(b->second.front()->id);
        }
        else
            ui.Tip(id, tip, hit);
        l.hot = ui.Anim(HashId(id, 3), (l.bound && mainHot_ == vk) || in.hover ? 1.f : 0.f, 16.f);
        bool live = !preview_ && engine_.LiveDown(vk);
        l.press = ui.Spring(HashId(id, 5), live || in.held ? 1.f : 0.f, 620.f, 30.f);
        return l;
    };
    auto tint = [&](const Look &l, Color idle) {
        Color c = l.bound ? Mix(T.key, l.c, 0.16f + 0.12f * l.hot + 0.25f * l.fire) : Mix(idle, kWhite, 0.06f * l.hot);
        return Mix(c, l.bound ? l.c : T.accent, 0.28f * l.press);
    };

    // side buttons peek out on the left, so they go under the shell
    const int sideVk[2] = {VK_XBUTTON2, VK_XBUTTON1};
    const wchar_t *sideTip[2] = {L"Mouse 5", L"Mouse 4"};
    for (int i = 0; i < 2; i++)
    {
        Rect sr = R(-5, 84 + i * 30.f, 13, 26);
        Look l = look(sideVk[i], sr, {sr.x, sr.Cy()}, true, sideTip[i]);
        if (l.bound)
            g.RadialGlow(sr.Cx(), sr.Cy(), 12 * s, 16 * s, WithA(l.c, 0.10f));
        g.Round(sr, 5 * s, Mix(tint(l, T.key), kBlack, 0.25f));
        g.Round({sr.x, sr.y, sr.w - 2 * s, sr.h - 2 * s}, 5 * s, tint(l, T.key));
    }

    g.RadialGlow(box.Cx(), box.B() + 2, box.w * 0.55f, 16 * s, WithA(kBlack, 0.5f));
    g.Shadow(box.Inset(8 * s, 4 * s), 40 * s, 30, 0.34f, 12);
    g.PolygonGrad(body.data(), (int)body.size(), box, Mix(T.panel3, kWhite, 0.08f), Mix(T.panel2, kBlack, 0.18f));
    // left / right halves are the shell clipped to each side
    const int halfVk[2] = {VK_LBUTTON, VK_RBUTTON};
    const wchar_t *halfTip[2] = {L"Left click", L"Right click"};
    for (int i = 0; i < 2; i++)
    {
        Rect hr = i ? R(60.5f, 0, 60, 72) : R(0, 0, 59.5f, 72);
        Look l = look(halfVk[i], hr, P(i ? 92.f : 28.f, 40), i == 0, halfTip[i]);
        if (!l.bound && l.hot < 0.01f && l.press < 0.01f)
            continue;
        ui.PushClip(hr);
        g.PolygonGrad(body.data(), (int)body.size(), box, WithA(tint(l, T.panel3), l.bound ? 1.f : 0.45f),
                      WithA(tint(l, T.panel3), 0.f));
        ui.PopClip();
    }
    g.Line(P(10, 72).x, P(10, 72).y, P(110, 72).x, P(110, 72).y, WithA(kBlack, 0.35f), 1.4f);
    g.Line(P(60, 5).x, P(60, 5).y, P(60, 72).x, P(60, 72).y, WithA(kBlack, 0.35f), 1.4f);
    Outline(g, body, WithA(kWhite, 0.10f), 1.2f);

    // wheel + scroll arrows
    Rect wheel = R(53, 20, 14, 36);
    {
        Look l = look(VK_MBUTTON, wheel, {wheel.R(), wheel.Cy()}, false, L"Wheel click");
        g.Round(wheel, 7 * s, Mix(T.panel2, kBlack, 0.45f));
        Rect wf = wheel.Inset(2.5f * s);
        g.RoundGrad(wf, 5 * s, Mix(tint(l, T.key), kWhite, 0.10f), tint(l, T.key), true);
        for (int i = 0; i < 5; i++)
            g.Fill({wf.x + 2 * s, wf.y + (5 + i * 5.5f) * s, wf.w - 4 * s, 1}, WithA(kBlack, 0.25f));
    }
    for (int i = 0; i < 2; i++)
    {
        int vk = i ? kWheelDown : kWheelUp;
        Rect hit = R(72, i ? 42.f : 18.f, 14, 14);
        Look l = look(vk, hit, {hit.R(), hit.Cy()}, false, i ? L"Wheel down" : L"Wheel up");
        if (!l.bound && l.hot < 0.01f)
            continue;
        Color c = l.bound ? Mix(l.c, kWhite, l.fire) : T.dim;
        float cx = hit.Cx(), cy = hit.Cy(), d = 4 * s * (i ? -1 : 1);
        D2D1_POINT_2F tri[3] = {{cx, cy - d}, {cx - 4 * s, cy + d}, {cx + 4 * s, cy + d}};
        g.Polygon(tri, 3, c);
    }
    // little logo so it reads as a mouse
    auto lg = P(60, 150);
    g.RadialGlow(lg.x, lg.y, 16 * s, 16 * s, WithA(T.accent, 0.25f));
    g.Circle(lg.x, lg.y, 3.5f * s, WithA(T.accentHi, 0.85f));
    ui.PopXf();
    ui.PopAlpha();
}

// ---------------------------------------------------------------- controller

void App::DrawPad(const Rect &r, PadStyle style, const Pad::State &st, bool live, const BindMap &binds,
                  std::vector<Anchor> &anchors)
{
    // drawn in a 400 x 290 box
    float s = std::min(r.w / 410.f, r.h / 300.f);
    float ox = r.Cx() - 200 * s, oy = r.Cy() - 150 * s;
    auto P = [&](float x, float y) { return D2D1_POINT_2F{ox + x * s, oy + y * s}; };
    auto R = [&](float x, float y, float w, float h) { return Rect{ox + x * s, oy + y * s, w * s, h * s}; };
    bool ps = style == PadStyle::PlayStation;
    float mid = P(200, 0).x;

    std::vector<D2D1_POINT_2F> half =
        ps ? std::vector<D2D1_POINT_2F>{{200, 62},  {250, 60},  {292, 52},  {332, 50},  {362, 66},  {380, 98},
                                        {393, 156}, {400, 220}, {394, 262}, {372, 282}, {346, 274}, {324, 244},
                                        {304, 210}, {280, 194}, {240, 190}, {200, 190}}
           : std::vector<D2D1_POINT_2F>{{200, 60},  {250, 56},  {292, 46},  {330, 50},  {358, 70},  {376, 105},
                                        {390, 160}, {398, 215}, {392, 250}, {370, 268}, {342, 262}, {318, 238},
                                        {296, 205}, {270, 188}, {230, 182}, {200, 182}};
    std::vector<D2D1_POINT_2F> ctrl;
    for (auto &p : Mirrored(half))
        ctrl.push_back(P(p.x, p.y));
    auto body = Smooth(ctrl);
    Rect box = R(0, 40, 400, 245);

    ID base = HashId(L"pad-part");
    struct Look
    {
        bool bound;
        Color c;
        float fire, hot, down;
    };
    auto look = [&](int vk, const Rect &hit) {
        auto b = binds.find(vk);
        Look l{b != binds.end(), T.dim, 0, 0, 0};
        ID id = HashId(base, (uint64_t)vk);
        Interact in = ui.Press(id, hit);
        if (l.bound)
        {
            l.c = CatColor(b->second.front()->category);
            l.fire = KeyFlash(b->second);
            anchors.push_back({vk, hit.Cx(), hit.Cy(), hit.Cx() < mid});
            if (in.hover)
                mainHotNext_ = vk;
            ui.Tip(id, PadLabel(vk, style) + L": " + Joined(b->second), hit);
            if (in.clicked)
                OpenEditor(b->second.front()->id);
        }
        else
            ui.Tip(id, PadLabel(vk, style), hit);
        l.hot = ui.Anim(HashId(id, 3), (l.bound && mainHot_ == vk) || in.hover ? 1.f : 0.f, 16.f);
        bool held = live && (st.buttons >> (vk - kPadFirst) & 1);
        l.down = ui.Spring(HashId(id, 9), held ? 1.f : 0.f, 700.f, 32.f);
        return l;
    };
    auto fillFor = [&](const Look &l, Color idle) {
        Color c = l.bound ? Mix(T.key, l.c, 0.18f + 0.12f * l.hot + 0.25f * l.fire) : Mix(idle, kWhite, 0.06f * l.hot);
        return Mix(c, l.bound ? l.c : T.accent, 0.35f * Clamp01(l.down));
    };
    auto glow = [&](const Look &l, float cx, float cy, float rad) {
        if (l.bound)
            g.RadialGlow(cx, cy, rad * 1.4f, rad * 1.4f, WithA(l.c, 0.06f + 0.10f * l.hot + 0.2f * l.fire));
    };
    Color well = Mix(T.panel2, kBlack, 0.45f);

    // shoulders and triggers sit behind the shell
    for (int side = 0; side < 2; side++)
    {
        float x = side ? 258.f : 78.f;
        int tvk = kPadFirst + (side ? 7 : 6), bvk = kPadFirst + (side ? 4 : 5);
        Rect tr = R(x, 8, 64, 34);
        Look lt = look(tvk, tr);
        float pull = live ? (side ? st.rt : st.lt) : 0;
        g.Round(tr, 12 * s, Mix(fillFor(lt, T.key), kBlack, 0.15f));
        if (pull > 0.01f)
            g.Round({tr.x, tr.B() - tr.h * pull, tr.w, tr.h * pull}, 12 * s,
                    WithA(lt.bound ? kWhite : T.accent, 0.35f));
        g.Text(PadLabel(tvk, style), {tr.x, tr.y + 2 * s, tr.w, 16 * s}, lt.bound ? Ink(lt.c) : T.dim, F::Tiny,
               kCenter);
        Rect br = R(side ? 246.f : 68.f, 32, 86, 22);
        Look lb = look(bvk, br);
        g.RoundGrad(br, 10 * s, Mix(fillFor(lb, T.key), kWhite, 0.08f), fillFor(lb, T.key), true);
    }

    g.RadialGlow(box.Cx(), box.B() + 4 * s, box.w * 0.5f, 18 * s, WithA(kBlack, 0.5f));
    g.Shadow(box.Inset(20 * s, 10 * s), 80 * s, 30, 0.32f, 12);
    g.PolygonGrad(body.data(), (int)body.size(), box, Mix(T.panel3, kWhite, 0.09f), Mix(T.panel2, kBlack, 0.22f));
    Outline(g, body, WithA(kWhite, 0.09f), 1.2f);

    // stick: a well, and a cap that follows the live axis
    auto stick = [&](int vk, float cx, float cy, float ax, float ay) {
        float rad = 27 * s;
        auto c = P(cx, cy);
        Look l = look(vk, {c.x - rad, c.y - rad, rad * 2, rad * 2});
        float mx = ui.Spring(HashId(base, (uint64_t)vk * 31 + 1), live ? ax : 0.f, 500.f, 30.f);
        float my = ui.Spring(HashId(base, (uint64_t)vk * 31 + 2), live ? -ay : 0.f, 500.f, 30.f);
        glow(l, c.x, c.y, rad);
        g.Circle(c.x, c.y, rad, well);
        g.CircleStroke(c.x, c.y, rad, WithA(kWhite, 0.06f), 1.f);
        float px = c.x + mx * 9 * s, py = c.y + my * 9 * s;
        Color f = fillFor(l, T.key);
        g.Circle(px, py + 2 * s, rad * 0.74f, Mix(f, kBlack, 0.4f));
        g.Circle(px, py, rad * 0.72f, f);
        g.Circle(px, py - rad * 0.12f, rad * 0.56f, Mix(f, kWhite, 0.06f));
        g.CircleStroke(px, py, rad * 0.44f, WithA(kBlack, 0.18f), 1.f);
        if (l.bound)
            g.CircleStroke(px, py, rad * 0.72f, WithA(l.c, 0.65f), 1.3f);
    };
    // d-pad: four arms around a centre
    auto dpad = [&](float cx, float cy) {
        const float a = 15, w = 15;
        Rect arms[4] = {R(cx - w / 2, cy - a - w / 2, w, a), R(cx - w / 2, cy + w / 2, w, a),
                        R(cx - a - w / 2, cy - w / 2, a, w), R(cx + w / 2, cy - w / 2, a, w)};
        g.Round(R(cx - w / 2 - a - 3, cy - w / 2 - 3, w + 2 * a + 6, w + 6), 6 * s, well);
        g.Round(R(cx - w / 2 - 3, cy - w / 2 - a - 3, w + 6, w + 2 * a + 6), 6 * s, well);
        g.Round(R(cx - w / 2, cy - w / 2, w, w), 2 * s, T.key);
        for (int i = 0; i < 4; i++)
        {
            Look l = look(kPadFirst + 8 + i, arms[i].Inset(-2 * s));
            glow(l, arms[i].Cx(), arms[i].Cy(), 10 * s);
            g.RoundGrad(arms[i], 3 * s, Mix(fillFor(l, T.key), kWhite, 0.08f), fillFor(l, T.key), true);
        }
    };
    // face buttons, bottom / right / left / top = xinput A / B / X / Y
    auto face = [&](float cx, float cy) {
        const float off = 22, rad = 12.5f;
        D2D1_POINT_2F at[4] = {{cx, cy + off}, {cx + off, cy}, {cx - off, cy}, {cx, cy - off}};
        Color xc[4] = {Rgb(0x5BBF6A), Rgb(0xE0605E), Rgb(0x4E8FE0), Rgb(0xE5B64A)};
        Color pc[4] = {Rgb(0x7FA6E8), Rgb(0xE8707A), Rgb(0xD889C4), Rgb(0x5CC6A4)};
        for (int i = 0; i < 4; i++)
        {
            auto c = P(at[i].x, at[i].y);
            float rr = rad * s;
            Look l = look(kPadFirst + i, {c.x - rr, c.y - rr, rr * 2, rr * 2});
            glow(l, c.x, c.y, rr);
            float pop = 1 - 0.08f * Clamp01(l.down);
            Color f = fillFor(l, T.key);
            g.Circle(c.x, c.y, rr + 2 * s, well);
            g.Circle(c.x, c.y + 1.5f * s, rr * pop, Mix(f, kBlack, 0.4f));
            g.Circle(c.x, c.y, rr * pop, f);
            g.Circle(c.x, c.y - rr * 0.15f, rr * 0.75f * pop, Mix(f, kWhite, 0.05f));
            if (l.bound)
                g.CircleStroke(c.x, c.y, rr * pop, WithA(l.c, 0.65f), 1.3f);
            Color mark = l.bound ? Ink(l.c) : style == PadStyle::Nintendo ? T.dim : ps ? pc[i] : xc[i];
            float m = 5.5f * s;
            if (ps)
            {
                // the four shapes, drawn instead of relying on font glyphs
                if (i == 0)
                {
                    g.Line(c.x - m, c.y - m, c.x + m, c.y + m, mark, 1.8f);
                    g.Line(c.x - m, c.y + m, c.x + m, c.y - m, mark, 1.8f);
                }
                else if (i == 1)
                    g.CircleStroke(c.x, c.y, m, mark, 1.8f);
                else if (i == 2)
                    g.RoundStroke({c.x - m * 0.85f, c.y - m * 0.85f, m * 1.7f, m * 1.7f}, 1, mark, 1.8f);
                else
                {
                    g.Line(c.x, c.y - m, c.x + m, c.y + m * 0.7f, mark, 1.8f);
                    g.Line(c.x + m, c.y + m * 0.7f, c.x - m, c.y + m * 0.7f, mark, 1.8f);
                    g.Line(c.x - m, c.y + m * 0.7f, c.x, c.y - m, mark, 1.8f);
                }
            }
            else
                g.Text(PadLabel(kPadFirst + i, style), {c.x - rr, c.y - rr, rr * 2, rr * 2}, mark, F::SmallSemi,
                       kCenter);
        }
    };
    auto pill = [&](int vk, float cx, float cy) {
        Rect pr = R(cx - 9, cy - 5.5f, 18, 11);
        Look l = look(vk, pr.Inset(-4 * s));
        glow(l, pr.Cx(), pr.Cy(), 8 * s);
        g.Round(pr.Inset(-1.5f * s), 7 * s, well);
        g.Round(pr, 5.5f * s, fillFor(l, T.key));
    };

    if (ps)
    {
        Rect pad = R(146, 64, 108, 64);
        g.Round(pad.Inset(-2 * s), 14 * s, well);
        g.RoundGrad(pad, 12 * s, Mix(T.panel3, kWhite, 0.04f), Mix(T.panel2, kBlack, 0.1f), true);
        g.RadialGlow(pad.Cx(), pad.y, pad.w * 0.45f, 8 * s, WithA(T.accentHi, 0.5f)); // light bar
        dpad(110, 116);
        face(290, 116);
        stick(kPadFirst + 14, 152, 186, st.lx, st.ly);
        stick(kPadFirst + 15, 248, 186, st.rx, st.ry);
        pill(kPadFirst + 13, 130, 74);
        pill(kPadFirst + 12, 270, 74);
        g.Circle(P(200, 152).x, P(200, 152).y, 8 * s, well);
    }
    else
    {
        stick(kPadFirst + 14, 122, 110, st.lx, st.ly);
        dpad(158, 172);
        stick(kPadFirst + 15, 248, 172, st.rx, st.ry);
        face(288, 110);
        pill(kPadFirst + 13, 174, 108);
        pill(kPadFirst + 12, 226, 108);
        auto gd = P(200, 80);
        g.Circle(gd.x, gd.y, 13 * s, well);
        g.RadialGlow(gd.x, gd.y, 14 * s, 14 * s, WithA(T.accent, 0.35f));
        g.CircleStroke(gd.x, gd.y, 10 * s, WithA(T.accentHi, 0.7f), 1.4f);
    }
}

// ---------------------------------------------------------------- callouts

// labels beside the picture, a thin line from each bound button out to its label
void App::Callouts(const std::vector<Anchor> &anchors, const BindMap &binds, float leftX, float rightX, float top,
                   float bottom, PadStyle style)
{
    const float lh = 48, lw = 190, gap = 10;
    int n = 0;
    for (int side = 0; side < 2; side++)
    {
        std::vector<const Anchor *> list;
        for (auto &a : anchors)
            if (a.left == (side == 0) && binds.count(a.vk))
                list.push_back(&a);
        std::sort(list.begin(), list.end(), [](auto *a, auto *b) { return a->y < b->y; });
        // each label wants to sit level with its button, pushed apart so they never overlap
        std::vector<float> ys(list.size());
        for (size_t i = 0; i < list.size(); i++)
            ys[i] = std::max(list[i]->y - lh / 2, i ? ys[i - 1] + lh + gap : top);
        if (!ys.empty() && ys.back() + lh > bottom)
        {
            ys.back() = bottom - lh;
            for (size_t i = ys.size() - 1; i-- > 0;)
                ys[i] = std::min(ys[i], ys[i + 1] - lh - gap);
        }
        for (size_t i = 0; i < list.size(); i++, n++)
        {
            const Anchor &a = *list[i];
            const auto &ms = binds.at(a.vk);
            const Macro *m = ms.front();
            Color c = CatColor(m->category);
            Rect lr = side == 0 ? Rect{leftX - lw, ys[i], lw, lh} : Rect{rightX, ys[i], lw, lh};
            ID id = HashId(L"callout", (uint64_t)a.vk);
            Interact in = ui.Press(id, lr);
            if (in.hover)
                mainHotNext_ = a.vk;
            float hot = ui.Anim(HashId(id, 3), in.hover || mainHot_ == a.vk ? 1.f : 0.f, 16.f);
            float fire = KeyFlash(ms);
            // the line draws out from the button, then the label fades in at its end
            float p = MainAppear(0.30f + n * 0.06f, 0.55f);
            float edge = side == 0 ? lr.R() : lr.x;
            float ex = side == 0 ? edge + 26 : edge - 26, ly = lr.Cy();
            float l1 = std::hypot(ex - a.x, ly - a.y), l2 = std::fabs(edge - ex), d = (l1 + l2) * p;
            Color lc = Mix(WithA(T.text, 0.22f), c, 0.35f + 0.65f * std::max(hot, fire));
            float lwid = 1.2f + 0.6f * hot;
            float f1 = l1 > 0 ? std::min(1.f, d / l1) : 1.f;
            g.Line(a.x, a.y, a.x + (ex - a.x) * f1, a.y + (ly - a.y) * f1, lc, lwid);
            if (d > l1)
            {
                float f2 = std::min(1.f, (d - l1) / std::max(1.f, l2));
                g.Line(ex, ly, ex + (edge - ex) * f2, ly, lc, lwid);
            }
            g.Circle(a.x, a.y, 7 + 2 * hot, WithA(c, 0.20f + 0.2f * hot));
            g.Circle(a.x, a.y, 3.2f, c);

            float la = Clamp01(p * 1.8f - 0.8f);
            ui.PushAlpha(la);
            ui.PushOffset((side == 0 ? 8.f : -8.f) * (1 - la), 0);
            if (fire > 0)
                g.Round(lr.Inset(-3), 15, WithA(c, 0.25f * fire));
            g.Shadow(lr, 12, 16, 0.28f, 5);
            g.Round(lr, 12, Mix(T.panel2, T.panel3, 0.35f + 0.5f * hot));
            g.RoundStroke(lr, 12, Mix(T.border, c, 0.25f + 0.55f * hot));
            g.Round({lr.x + 12, lr.y + 13, 3, lr.h - 26}, 1.5f, c);
            g.Text(m->name, {lr.x + 25, lr.y + 7, lr.w - 34, 20}, T.text, F::SmallSemi);
            std::wstring sub = PadLabel(a.vk, style) + L"  \x00B7  " + m->category;
            if (ms.size() > 1)
                sub += Fmt(L"  +%d", (int)ms.size() - 1);
            g.Text(sub, {lr.x + 25, lr.y + 26, lr.w - 34, 16}, T.dim, F::Caption);
            ui.Tip(id, Joined(ms) + L"  (" + Mode(*m) + L")", lr);
            if (in.clicked)
                OpenEditor(m->id);
            ui.PopXf();
            ui.PopAlpha();
        }
    }
}

// ---------------------------------------------------------------- binding rows

float App::BindRows(float x, float y, float w, const BindList &items, PadStyle style)
{
    int cols = w > 960 ? 3 : w > 600 ? 2 : 1;
    float gap = 12, rw = (w - gap * (cols - 1)) / cols, rh = 62;
    for (size_t i = 0; i < items.size(); i++)
    {
        auto [vk, m] = items[i];
        Rect r{x + (i % cols) * (rw + gap), y + (i / cols) * (rh + gap), rw, rh};
        Color c = CatColor(m->category);
        float t = MainAppear(0.32f + i * 0.035f, 0.4f);
        ID id = HashId(L"bind-row-" + m->name, (uint64_t)vk);
        Interact in = ui.Press(id, r);
        if (in.hover)
            mainHotNext_ = vk;
        float hot = ui.Anim(HashId(id, 3), in.hover || mainHot_ == vk ? 1.f : 0.f, 16.f);
        float fire = KeyFlash({m});
        float sp = ui.Spring(HashId(id, 0xB7), in.held ? 1.f : 0.f, 460.f, 26.f);
        ui.PushAlpha(t);
        ui.PushOffset(0, 10 * (1 - t));
        ui.PushScale(1 - 0.015f * sp, r.Cx(), r.Cy());
        g.Round(r, 14, Mix(T.panel, T.panel2, 0.35f + 0.65f * hot));
        g.RoundStroke(r, 14, Mix(T.border, c, 0.45f * std::max(hot, fire)));
        std::wstring key = PadLabel(vk, style);
        float kw = std::max(46.f, g.TextW(key, F::SmallSemi) + 24);
        Rect kc{r.x + 12, r.y + 12, kw, 38};
        if (fire > 0 || hot > 0)
            g.RadialGlow(kc.Cx(), kc.Cy(), kw * 0.9f, 30, WithA(c, 0.08f * std::max(hot, fire)));
        Cap(g, kc, Mix(T.key, c, 0.14f + 0.2f * fire), 0, 8);
        g.Text(key, CapTop(kc, 0), Ink(c), F::SmallSemi, kCenter);
        float tx = kc.R() + 14, tw = r.R() - tx - 40;
        g.Text(m->name, {tx, r.y + 11, tw, 20}, T.text, F::Semi);
        g.Circle(tx + 3, r.y + 42, 3, c);
        g.Text(m->category + L"  \x00B7  " + Mode(*m), {tx + 12, r.y + 33, tw - 12, 18}, T.dim, F::Small);
        g.Text(L"\xE76C", {r.R() - 34, r.y, 20, rh}, WithA(T.dim, 0.3f + 0.7f * hot), F::IconSm, kCenter);
        if (in.clicked)
            OpenEditor(m->id);
        ui.PopXf();
        ui.PopXf();
        ui.PopAlpha();
    }
    int rows = ((int)items.size() + cols - 1) / cols;
    return y + rows * (rh + gap) - gap;
}

// ---------------------------------------------------------------- page

void App::PageMain(const Rect &area)
{
    mainHot_ = mainHotNext_;
    mainHotNext_ = 0;
    ID sid = HashId(L"scroll-Main");
    ui.BeginScroll(sid, {area.x, area.y, area.w + 18, area.h});
    float y = area.y;

    const Device *devs[3] = {};
    int extra[3] = {};
    for (auto &d : devices_)
    {
        int k = (int)d.kind;
        if (!devs[k])
            devs[k] = &d;
        else
            extra[k]++;
    }
    Pad::State st = padPreview_ ? padShot_ : Pad::Get();
    if (!preview_)
    {
        engine_.WatchLive();
        if (st.connected)
            Pad::Watch();
    }
    // a pad only xinput sees (no HID entry), e.g. a 360 pad on the xusb driver
    Device xinputOnly{DevKind::Controller, L"", L"Xbox controller", L"XInput", 0x045E, 0};
    if (!devs[2] && st.connected)
        devs[2] = &xinputOnly;
    bool padPresent = devs[2] != nullptr;
    PadStyle style = padPresent ? PadStyleFor(*devs[2]) : PadStyle::Xbox;

    // what fires on what, enabled macros only
    BindMap binds;
    BindList lists[3];
    for (auto &m : cfg_.macros)
    {
        int vk;
        if (!m.enabled || !KeyParse(m.trigger, vk))
            continue;
        vk = NormVk(vk);
        binds[vk].push_back(&m);
        lists[IsPadKey(vk) ? 2 : IsMouseKey(vk) ? 1 : 0].push_back({vk, &m});
    }
    for (auto &l : lists)
        std::stable_sort(l.begin(), l.end(), [](auto &a, auto &b) { return a.first < b.first; });
    int counts[3] = {(int)lists[0].size(), (int)lists[1].size(), (int)lists[2].size()};
    bool online[3] = {devs[0] != nullptr, devs[1] != nullptr, st.connected};

    MainTabs({area.x, y, area.w, 88}, devs, counts, online);
    y += 88 + 22;

    // ---- stage with the picked device
    int dev = mainDev_;
    const Device *d = devs[dev];
    float stageH = 500;
    Rect stage{area.x, y, area.w, stageH};
    {
        float t = RowAppear(3);
        ui.PushAlpha(t);
        ui.PushOffset(0, 16 * (1 - t));
        Stage(stage);

        // content slides in from the side it was picked on
        float ct = MainAppear(0, 0.42f);
        ui.PushAlpha(ct);
        ui.PushOffset(28.f * mainDir_ * (1 - ct), 0);

        // title block
        std::wstring kindLine = std::wstring(kKind[dev]);
        if (d)
            kindLine += L"  \x00B7  " + d->link;
        if (dev == 2 && st.connected)
            kindLine += st.wired          ? L"  \x00B7  wired"
                        : st.battery >= 3 ? L"  \x00B7  battery full"
                        : st.battery == 2 ? L"  \x00B7  battery good"
                        : st.battery == 1 ? L"  \x00B7  battery low"
                        : st.battery == 0 ? L"  \x00B7  battery empty"
                                          : L"";
        g.Text(kindLine, {stage.x + 34, stage.y + 28, stage.w - 300, 16}, T.faint, F::Caption);
        std::wstring title = d ? (d->brand.empty() ? d->model : d->brand + L" " + d->model) : L"Nothing plugged in";
        if (extra[dev] > 0)
            title += Fmt(L"  +%d", extra[dev]);
        g.Text(title, {stage.x + 32, stage.y + 44, stage.w - 300, 36}, d ? T.text : T.dim, F::Title);

        // colour key for the categories that show up on this device
        {
            std::vector<std::wstring> cats;
            for (auto &[vk, m] : lists[dev])
                if (std::find(cats.begin(), cats.end(), m->category) == cats.end())
                    cats.push_back(m->category);
            float lx = stage.R() - 32;
            auto chip = [&](const std::wstring &name, Color c) {
                float w = g.TextW(name, F::Caption) + 26;
                lx -= w;
                g.Circle(lx + 8, stage.y + 40, 3.5f, c);
                g.Text(name, {lx + 16, stage.y + 32, w - 16, 16}, T.dim, F::Caption);
                lx -= 10;
            };
            if (dev == 0)
                chip(L"App keys", T.dim);
            for (size_t i = cats.size(); i-- > 0;)
                chip(cats[i], CatColor(cats[i]));
        }

        Rect pic{stage.x + 40, stage.y + 100, stage.w - 80, stage.h - 140};
        bool callouts = dev != 0 && stage.w >= 860;
        float colW = 190 + 24;
        Rect devR = callouts ? Rect{pic.x + colW, pic.y, pic.w - 2 * colW, pic.h} : pic;
        std::vector<Anchor> anchors;
        if (dev == 0)
            DrawKeyboard(pic, binds);
        else if (dev == 1)
            DrawMouse(devR, binds, anchors);
        else
        {
            float on = ui.Anim(HashId(L"pad-present"), padPresent ? 1.f : 0.f, 6.f);
            ui.PushAlpha(0.14f + 0.86f * on);
            ui.PushScale(0.94f + 0.06f * on, devR.Cx(), devR.Cy());
            DrawPad(devR, style, st, st.connected, padPresent ? binds : BindMap{}, anchors);
            ui.PopXf();
            ui.PopAlpha();
            if (!padPresent)
            {
                float bw = std::min(420.f, stage.w - 80);
                Rect plate{stage.Cx() - bw / 2, devR.Cy() - 48, bw, 104};
                g.Shadow(plate, 16, 22, 0.32f, 6);
                g.Round(plate, 16, WithA(T.panel2, 0.95f));
                g.RoundStroke(plate, 16, T.border2);
                g.Text(L"Plug in a controller", {plate.x, plate.y + 20, plate.w, 26}, T.text, F::H3, kCenter);
                g.Text(L"Xbox pads work as they are. PlayStation and Switch pads need Steam Input or DS4Windows.",
                       {plate.x + 26, plate.y + 50, plate.w - 52, 40}, T.dim, F::Small, kCenter, false, true);
            }
        }
        if (callouts)
            Callouts(anchors, binds, pic.x + 190, pic.R() - 190, pic.y, pic.B(), style);
        ui.PopXf();
        ui.PopAlpha();
        ui.PopXf();
        ui.PopAlpha();
    }
    y = stage.B() + 30;

    // ---- list under the stage: always for the keyboard, for the others when callouts didn't fit
    bool listed = dev == 0 || stage.w < 860;
    float t = RowAppear(4);
    ui.PushAlpha(t);
    if (listed && !lists[dev].empty())
    {
        g.Text(Fmt(L"ON THIS %s", dev == 0   ? L"KEYBOARD"
                                  : dev == 1 ? L"MOUSE"
                                             : L"CONTROLLER"),
               {area.x + 4, y, 300, 16}, T.faint, F::Caption);
        y = BindRows(area.x, y + 26, area.w, lists[dev], style) + 26;
    }
    else if (lists[dev].empty())
    {
        const wchar_t *hint = dev == 0   ? L"Nothing on the keyboard yet. Give a macro a key and it lights up above."
                              : dev == 1 ? L"No macros on mouse buttons yet."
                                         : L"Set a macro's key and press a controller button to put it here.";
        g.Text(hint, {area.x + 4, y, area.w, 20}, T.dim, F::Small);
        y += 40;
    }
    if (dev == 2 && padPresent)
    {
        // the two things worth knowing about pad triggers, kept small
        std::wstring note = st.connected
                                ? L"The game still gets controller presses, they can't be blocked like keys."
                                : L"This pad isn't showing up through XInput. Turn on Steam Input or DS4Windows for "
                                  L"its buttons to trigger macros.";
        Rect nr{area.x, y, area.w, 44};
        g.Round(nr, 12, WithA(st.connected ? T.panel2 : T.amber, st.connected ? 0.6f : 0.12f));
        g.Text(L"\xE946", {nr.x + 14, nr.y, 20, nr.h}, st.connected ? T.dim : T.amber, F::IconSm, kCenter);
        g.Text(note, {nr.x + 42, nr.y, nr.w - 56, nr.h}, st.connected ? T.dim : T.amber, F::Small);
        y = nr.B() + 20;
    }
    ui.PopAlpha();
    ui.EndScroll(sid, y - area.y);
}
