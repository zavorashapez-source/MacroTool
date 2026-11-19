#include "app.h"
#include "monitor.h"
#include "osd.h"
#include <commdlg.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <thread>

double NowSeconds() { return QpcToMs(QpcNow()) / 1000.0; }

void DrawLogo(Gfx& g, const Rect& r) {
    g.Circle(r.Cx(), r.Cy(), r.w / 2, Rgb(0xE6F1FB));
    auto P = [&](float x, float y) { return D2D1_POINT_2F{r.x + x * r.w, r.y + y * r.h}; };
    D2D1_POINT_2F pts[7] = {P(.58f, .18f), P(.32f, .54f), P(.48f, .54f), P(.42f, .82f),
                            P(.69f, .45f), P(.53f, .45f), P(.60f, .18f)};
    g.Polygon(pts, 7, Rgb(0x1E2A36));
}

// ---------------------------------------------------------------- lifecycle

void App::Init(HWND hwnd, bool preview) {
    hwnd_ = hwnd;
    preview_ = preview;
    ui.hwnd = hwnd;
    if (preview) {
        SetConfigReadOnly(true);
        cfg_ = Config::Defaults();
    } else {
        cfg_ = Config::Load(&StartupWarning);
        stats_.Load();
    }
    tweakUi_.assign(TweakToggles().size(), {});
    actionBusy_.assign(TweakActions().size(), false);
    RecomputeIssues();
    engine_.SetEnabled(cfg_.enabled);
    engine_.Reindex(cfg_);
    T = cfg_.theme == L"Light" ? kLight : kDark;
    if (!preview) {
        if (cfg_.highPriority) SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        Tester::SetNotify(hwnd, WM_APP_TESTER);
        Monitor::SetNotify(hwnd, WM_APP_MONITOR);
        Monitor::Configure(cfg_.timing, cfg_.pingHost);
        engine_.SetLocked(false);  // no license gate
        engine_.Start(hwnd, WM_APP_ENGINE);
        Osd::SetEnabled(cfg_.osd);
        licensed_ = true;
        lic_.expiry = 0;
        SetTheme(cfg_.theme == L"Light");
        if (!StartupWarning.empty()) Message(L"Settings were reset", StartupWarning, true);
        splashAt_ = NowSeconds();
    }
    startedAt_ = pageStart_ = NowSeconds();
}

void App::Shutdown() {
    if (ed_.recording) engine_.EndRecord();
    Tester::SetListening(false);
    if (!preview_) {
        if (stats_.dirty) stats_.Save();
        engine_.Shutdown();
        Monitor::Shutdown();
    }
}

void App::SaveCfg() {
    if (!cfg_.Save()) ShowToast(L"Couldn't save macros.json next to the app.", 2);
}

void App::Changed() {
    cfg_.AssignIds();
    SaveCfg();
    engine_.Reindex(cfg_);
    if (!preview_) Monitor::Configure(cfg_.timing, cfg_.pingHost);
    RecomputeIssues();
}

void App::RecomputeIssues() {
    issues_.clear();
    int toggleVk = VK_F8;
    KeyParse(cfg_.toggleKey, toggleVk);
    for (auto& m : cfg_.macros) {
        try {
            ParseSteps(m.steps, cfg_.binds);
            ParseSteps(m.releaseSteps, cfg_.binds);
        } catch (const FormatError& e) {
            issues_[m.id] = {e.msg, true};
            continue;
        }
        int k = 0;
        bool keyOk = KeyParse(m.trigger, k);
        if (Trim(m.steps).empty() && Trim(m.releaseSteps).empty())
            issues_[m.id] = {L"No steps yet. Open it to add some.", false};
        else if (!m.trigger.empty() && !keyOk)
            issues_[m.id] = {L"Unknown trigger key '" + m.trigger + L"'", true};
        else if (m.enabled && m.trigger.empty())
            issues_[m.id] = {L"Turned on, but no key is set yet", false};
        else if (m.enabled && keyOk && k == toggleVk)
            issues_[m.id] = {L"Uses the on/off key, so it never fires", false};
    }
}

Macro* App::FindMacro(int id) {
    for (auto& m : cfg_.macros)
        if (m.id == id) return &m;
    return nullptr;
}

bool App::IsMacroPage() const { return IsCategory(page_); }

const wchar_t* App::PageGlyph(const std::wstring& p) const {
    for (auto& c : kCategories)
        if (p == c.name) return c.glyph;
    if (p == L"Dashboard") return L"";
    if (p == L"Tweaks") return L"";
    if (p == L"Tester") return L"";
    if (p == L"Profiles") return L"";
    return L"";
}

std::wstring App::TimingLabel() const {
    bool human = cfg_.timing == L"FrameHuman";
    return Fmt(L"Frame %s · %d ms per step", human ? L"better" : L"legacy", Monitor::frameMs.load());
}

// ---------------------------------------------------------------- capture, master, theme

void App::StartCapture(Capture::Kind k, ID chip, int macroId, const std::wstring& bind) {
    cap_.kind = k;
    cap_.chip = chip;
    cap_.macroId = macroId;
    cap_.bind = bind;
    cap_.token = ++capToken_;
    engine_.BeginCapture(cap_.token);
}

void App::CancelCapture() {
    if (cap_.kind == Capture::None) return;
    engine_.CancelCapture();
    cap_.kind = Capture::None;
}

void App::ApplyCapture(int vk) {
    std::wstring name = KeyName(vk);
    Capture c = cap_;
    cap_.kind = Capture::None;
    ui.SetAnim(HashId(c.chip, 0x99), 1.f);
    int toggleVk = VK_F8;
    KeyParse(cfg_.toggleKey, toggleVk);
    switch (c.kind) {
        case Capture::MacroKey:
            if (Macro* m = FindMacro(c.macroId)) {
                if (m->trigger.empty()) m->enabled = true;  // a fresh macro is ready once it has a key
                m->trigger = name;
                Changed();
                if (vk == toggleVk) ShowToast(L"That's the on/off key, so this macro won't fire.", 2);
            }
            break;
        case Capture::EditorKey: ed_.trigger = name; break;
        case Capture::BindKey:
            for (auto& b : cfg_.binds)
                if (b.first == c.bind) b.second = name;
            Changed();
            break;
        case Capture::ToggleKey:
            cfg_.toggleKey = name;
            Changed();
            ShowToast(Pretty(name) + L" now turns macros on and off.", 1);
            break;
        case Capture::ResetKey:
            cfg_.editResetKey = name;
            Changed();
            break;
        default: break;
    }
}

void App::SetMaster(bool on) {
    engine_.SetEnabled(on);
    cfg_.enabled = on;
    SaveCfg();
    if (!on) running_.clear();
}

void App::SetTheme(bool light) {
    std::wstring t = light ? L"Light" : L"Dark";
    if (cfg_.theme != t) {
        cfg_.theme = t;
        SaveCfg();
    }
    if (!hwnd_) return;
    BOOL dark = !light;
    DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    COLORREF border = light ? RGB(0xD5, 0xDA, 0xE5) : RGB(0x1C, 0x24, 0x36);
    DwmSetWindowAttribute(hwnd_, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof border);
}

// ---------------------------------------------------------------- events

void App::OnEngine() {
    double now = NowSeconds();
    for (auto& m : engine_.Drain()) {
        switch (m.type) {
            case EngineMsg::Fired:
                running_.insert(m.macroId);
                flash_[m.macroId] = now;
                if (Macro* x = FindMacro(m.macroId)) {
                    lastFired_ = x->name;
                    stats_.Add(x->name);
                    recent_.push_front({x->name, KeyLabel(x->trigger), now});
                    if (recent_.size() > 40) recent_.pop_back();
                }
                lastFiredAt_ = now;
                break;
            case EngineMsg::Stopped: running_.erase(m.macroId); break;
            case EngineMsg::Toggled:
                cfg_.enabled = engine_.Enabled();
                SaveCfg();
                if (!cfg_.enabled) running_.clear();
                Osd::Flash(cfg_.enabled);  // brief in-game on/off popup
                break;
            case EngineMsg::Captured:
                if (cap_.kind != Capture::None && m.token == cap_.token) ApplyCapture(m.key);
                break;
            case EngineMsg::CaptureCanceled:
                if (m.token == cap_.token) cap_.kind = Capture::None;
                break;
            case EngineMsg::RecordStopped:
                if (ed_.recording) FinishRecord();
                break;
        }
    }
    if (!preview_ && stats_.dirty && now - statsSavedAt_ > 20) {
        stats_.Save();
        statsSavedAt_ = now;
    }
}

void App::OnTester() {
    for (auto& e : Tester::Drain()) {
        if (e.fire || runs_.empty()) {
            TesterRun r;
            r.name = e.fire ? e.macro : L"Input";
            r.qpc0 = e.qpc;
            r.at = NowSeconds();
            r.id = HashId(L"run", (uint64_t)++runSeq_);
            runs_.push_front(std::move(r));
            if (runs_.size() > 30) runs_.pop_back();
            if (e.fire) continue;
        }
        if (runs_.front().ev.size() < 500) runs_.front().ev.push_back(e);
    }
}

void App::OnTasks() {
    std::vector<std::function<void()>> t;
    {
        std::lock_guard lk(taskMu_);
        t.swap(tasks_);
    }
    for (auto& f : t)
        if (f) f();
}

void App::OnActivate(bool active) {
    active_ = active;
    if (!active) CancelCapture();
}

void App::RunAsync(std::function<std::function<void()>()> work) {
    if (preview_) {
        auto done = work();
        if (done) done();
        return;
    }
    HWND h = hwnd_;
    std::thread([this, work = std::move(work), h] {
        std::function<void()> done;
        try {
            done = work();
        } catch (...) {
        }
        {
            std::lock_guard lk(taskMu_);
            tasks_.push_back(std::move(done));
        }
        PostMessageW(h, WM_APP_TASK, 0, 0);
    }).detach();
}

int App::HitTest(float x, float y) {
    if (y < kChrome && x >= W_ - 90) return HTCLIENT;  // window dots
    if (y < kChrome) return HTCAPTION;
    for (auto& r : holes_)
        if (r.Has(x, y)) return HTCLIENT;
    if (!modals_.empty() || menu_.open) return HTCLIENT;
    if (y < kChrome + kTopH && x >= sideW_) return HTCAPTION;
    if (x < sideW_ && y < kChrome + 66) return HTCAPTION;
    return HTCLIENT;
}

void App::ExportBackup() {
    wchar_t file[MAX_PATH] = L"MacroTool-backup.json";
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Macro Tool backup (*.json)\0*.json\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!hwnd_ || !GetSaveFileNameW(&ofn)) return;
    if (cfg_.SaveTo(file)) ShowToast(L"Backup saved. Load it later from Profiles.", 1);
    else Message(L"Couldn't export", L"Windows wouldn't let the app write that file.", true);
}

// ---------------------------------------------------------------- dialogs, menus, toasts

void App::ShowToast(const std::wstring& text, int kind) {
    toasts_.push_back({text, kind, ui.time > 0 ? ui.time : NowSeconds(), HashId(L"toast", (uint64_t)++toastSeq_)});
    if (toasts_.size() > 3) toasts_.erase(toasts_.begin());
}

void App::OpenModal(ModalKind k) {
    CancelCapture();
    menu_.open = false;
    ui.focus = 0;
    for (auto& m : modals_)
        if (m.kind == k) {
            m.closing = false;
            return;
        }
    ModalEntry e{k, false, HashId(L"modal", (uint64_t)k * 7919 + (uint64_t)++toastSeq_)};
    ui.SetAnim(e.anim, 0.f);
    modals_.push_back(e);
}

void App::CloseModal(ModalKind k) {
    for (auto& m : modals_)
        if (m.kind == k) m.closing = true;
    ui.focus = 0;
    if (k == ModalKind::Editor && cap_.kind == Capture::EditorKey) CancelCapture();
}

bool App::ModalOpen(ModalKind k) const {
    for (auto& m : modals_)
        if (m.kind == k && !m.closing) return true;
    return false;
}

void App::Prompt(const std::wstring& title, const std::wstring& label, const std::wstring& initial,
                 std::function<bool(const std::wstring&)> onOk, const std::wstring& ok, bool mono,
                 const std::wstring& placeholder) {
    pr_ = {title, label, initial, placeholder, ok, mono, std::move(onOk), L""};
    OpenModal(ModalKind::Prompt);
    ui.focus = HashId(L"prompt-input");
}

void App::Confirm(const std::wstring& title, const std::wstring& text, const std::wstring& ok, bool danger,
                  std::function<void()> onOk) {
    cf_ = {title, text, ok, danger, std::move(onOk)};
    OpenModal(ModalKind::Confirm);
}

void App::Message(const std::wstring& title, const std::wstring& text, bool error) {
    msg_ = {title, text, error};
    OpenModal(ModalKind::Message);
}

void App::OpenMenu(const Rect& anchor, std::vector<MenuItem> items, float width) {
    CancelCapture();
    menu_.open = true;
    menu_.anchor = anchor;
    menu_.items = std::move(items);
    menu_.width = width;
    menu_.anim = HashId(L"menu", (uint64_t)++toastSeq_);
    ui.SetAnim(menu_.anim, 0.f);
}

void App::HandleKeys() {
    auto topModal = [&]() -> ModalEntry* {
        ModalEntry* top = nullptr;
        for (auto& m : modals_)
            if (!m.closing) top = &m;
        return top;
    };
    ID searchId = HashId(L"search");
    for (auto& k : ui.in.keys) {
        ModalEntry* top = topModal();
        if (k.vk == VK_ESCAPE) {
            if (menu_.open) {
                menu_.open = false;
            } else if (ui.focus == searchId) {
                search_.clear();
                ui.focus = 0;
            } else if (top) {
                if (top->kind == ModalKind::Editor) {
                    if (ed_.recording) ToggleRecord();
                    else CloseEditor();
                } else {
                    CloseModal(top->kind);
                }
            }
        } else if (k.vk == VK_OEM_2 && !k.shift && !k.ctrl && ui.focus == 0 && !top && !menu_.open) {
            // "/" jumps to search, like the dashboard it's modelled on.
            ui.focus = searchId;
            searchSel_ = 0;
            auto& ch = ui.in.chars;
            ch.erase(std::remove(ch.begin(), ch.end(), L'/'), ch.end());
        } else if (k.vk == VK_RETURN && top && !menu_.open) {
            if (top->kind == ModalKind::Confirm) {
                auto cb = cf_.onOk;
                CloseModal(ModalKind::Confirm);
                if (cb) cb();
            } else if (top->kind == ModalKind::Message) {
                CloseModal(ModalKind::Message);
            }
        } else if (k.ctrl && k.vk == 'S' && top && top->kind == ModalKind::Editor) {
            SaveEditor();
        } else if (k.ctrl && k.vk == 'N' && !top && !menu_.open) {
            OpenEditor(0);  // new macro
        } else if (k.ctrl && k.vk == 'F' && !top && !menu_.open) {
            ui.focus = searchId;  // jump to search
            searchSel_ = 0;
        }
    }
}

void App::Caption(const std::wstring& text, float x, float y, Color c) {
    g.Text(text, {x, y, 400, 14}, c, F::Caption, kLeft, false);
}

float App::RowAppear(int i) {
    if (ui.reduceMotion) return 1.f;
    // Snappy staggered cascade: each row ~0.34s, whole list well under 1s.
    float t = (float)((ui.time - pageStart_ - i * 0.05) / 0.34);
    if (t < 1) ui.Keep();
    return EaseOutBack(t) * 0.06f + EaseOut(t) * 0.94f;  // gentle settle, no harsh stop
}

void App::Card(const Rect& r) {
    g.Shadow(r, 14, 22, 0.22f, 7);  // soft elevation
    g.Round(r, 14, T.panel);
    g.RoundStroke(r, 14, T.border);
    g.Fill({r.x + 14, r.y + 1, r.w - 28, 1}, WithA(T.text, 0.045f));  // top sheen
}

void App::CardTitle(const Rect& r, const std::wstring& title, const std::wstring& legend, Color legendC) {
    g.Text(title, {r.x + 26, r.y + 20, r.w - (legend.empty() ? 52 : 260), 28}, T.text, F::H2);
    if (!legend.empty()) {
        float lw = g.TextW(legend, F::Caption);
        g.Circle(r.R() - 26 - lw - 12, r.y + 34, 4.5f, legendC);
        g.Text(legend, {r.R() - 26 - lw - 2, r.y + 26, lw + 4, 16}, T.dim, F::Caption, kLeft, true);
    }
    g.Fill({r.x, r.y + 66, r.w, 1}, T.border);
}

float App::EmptyState(const Rect& area, const wchar_t* glyph, const std::wstring& title, const std::wstring& text) {
    float cy = area.y + std::min(area.h * 0.30f, 150.f);
    float t = RowAppear(0);
    ui.PushAlpha(t);
    ui.PushOffset(0, 12 * (1 - t));
    float cx = area.Cx();
    g.Circle(cx, cy, 42, T.panel2);
    g.CircleStroke(cx, cy, 42, T.border2);
    g.Text(glyph, {cx - 42, cy - 42, 84, 84}, T.accentHi, F::IconXl, kCenter);
    g.Text(title, {area.x, cy + 58, area.w, 28}, T.text, F::H2, kCenter);
    float tw = std::min(440.f, area.w - 40);
    g.Text(text, {cx - tw / 2, cy + 92, tw, 60}, T.dim, F::Body, kCenter, false, true);
    ui.PopXf();
    ui.PopAlpha();
    return cy + 92 + g.TextH(text, F::Body, tw) + 22;
}

// ---------------------------------------------------------------- frame

void App::Frame(float W, float H) {
    W_ = W;
    H_ = H;
    holes_.clear();
    // Our own Animations toggle overrides the Windows "reduce motion" setting.
    if (!preview_) ui.reduceMotion = !cfg_.animations;
    float light = ui.Anim(0x7EE3E, cfg_.theme == L"Light" ? 1.f : 0.f, 7.f);
    T = LerpTheme(kDark, kLight, light);
    g.shadowScale = T.shadow;
    if (cap_.kind != Capture::None && ui.in.pressed && ui.hot != cap_.chip) CancelCapture();
    HandleKeys();

    int top = -1;
    for (int i = 0; i < (int)modals_.size(); i++)
        if (!modals_[i].closing) top = i;
    for (auto& m : modals_) m.t = ui.Anim(m.anim, m.closing ? 0.f : 1.f, m.closing ? 20.f : 13.f);
    float base = modals_.empty() ? 0.f : modals_[0].t;

    // first-run walkthrough, once a key is in
    bool tut = licensed_ && !cfg_.tutorialDone && modals_.empty();
    if (tut && tutorialStep_ < 0) tutorialStep_ = 0;
    if (!tut) tutorialStep_ = -1;
    bool tutActive = tutorialStep_ >= 0;

    g.Clear(T.bg);
    bool layered = base > 0.003f && g.BeginLayer();
    if (layered) g.Clear(T.bg);
    ui.inputOn = top < 0 && !menu_.open && !tutActive;
    // thin title strip, like a plain desktop window
    g.Fill({0, 0, W, kChrome}, T.side);
    g.Fill({0, kChrome - 1, W, 1}, T.border);
    g.Text(L"Macro Tool", {18, 0, 300, kChrome}, T.dim, F::Small);
    ui.PushOffset(0, kChrome);
    H_ = H - kChrome;
    DrawBase();
    H_ = H;
    ui.PopXf();
    if (layered) {
        if (ID2D1Bitmap* b = g.EndLayer()) {
            g.DrawBlurred(b, 10.f * EaseOut(base));
            b->Release();
        }
    }
    for (int i = 0; i < (int)modals_.size(); i++) {
        ModalEntry& m = modals_[i];
        g.Fill({0, 0, W, H}, WithA(T.overlay, m.t));
        bool input = i == top && !menu_.open && !m.closing;
        ui.inputOn = input;
        DrawModal(m, input);
    }
    modals_.erase(std::remove_if(modals_.begin(), modals_.end(), [](auto& m) { return m.closing && m.t <= 0.003f; }),
                  modals_.end());
    if (tutActive) {
        ui.inputOn = true;
        DrawTutorial(W, H);
    }
    ui.inputOn = !menu_.open;
    DrawWindowButtons();
    ui.inputOn = true;
    DrawMenu();
    DrawToasts();
    DrawSplash(W, H);
    ui.DrawTooltip();
    engine_.SetTyping(ui.focus != 0, hwnd_);
}

// A short "Welcome to Macro Tool" splash on launch: soft fade + spring-in logo.
void App::DrawSplash(float W, float H) {
    if (splashAt_ < 0) return;
    float s = (float)(ui.time - splashAt_);
    const float total = 2.4f, outAt = 1.85f;
    if (s >= total) {
        splashAt_ = -1;
        return;
    }
    ui.Keep();
    float in = EaseOut(Clamp01(s / 0.5f));
    float out = s > outAt ? 1.f - Clamp01((s - outAt) / (total - outAt)) : 1.f;
    float a = in * out;
    g.Fill({0, 0, W, H}, WithA(T.bg, 0.97f * a));
    float pop = EaseOutBack(Clamp01(s / 0.6f));
    float cx = W / 2, cy = H / 2 - 24;
    ui.PushAlpha(a);
    // soft glow behind the mark
    g.RadialGlow(cx, cy, 160 + 20 * std::sin(ui.time * 1.6), 160, WithA(T.accent, 0.10f));
    ui.PushScale(0.7f + 0.3f * pop, cx, cy);
    DrawLogo(g, {cx - 34, cy - 34, 68, 68});
    ui.PopXf();
    float ty = cy + 56 + 8 * (1 - EaseOut(Clamp01((s - 0.15f) / 0.5f)));
    g.Text(L"Welcome to Macro Tool", {cx - 300, ty, 600, 36}, T.text, F::Hero, kCenter);
    g.Text(L"Native macro engine", {cx - 300, ty + 40, 600, 22}, T.dim, F::Body, kCenter);
    ui.PopAlpha();
}

void App::DrawBase() {
    float open = ui.Anim(0x51DE0, cfg_.sidebarCollapsed ? 0.f : 1.f, 11.f);
    sideW_ = kSideClosed + (kSideOpen - kSideClosed) * open;
    float appear = ui.Anim(0xA99EA2, 1.f, 4.5f, 0.f);
    DrawSidebar(sideW_);

    float a = EaseOut(appear * 1.15f - 0.15f);
    ui.PushAlpha(a);
    ui.PushOffset(0, 10 * (1 - a));
    Rect header{sideW_ + 36, kTopH + 28, W_ - sideW_ - 72, 56};
    DrawHeader(header);
    Rect area{header.x, header.B() + 26, header.w, H_ - header.B() - 26};
    if (page_ == L"Dashboard") PageDashboard(area);
    else if (IsMacroPage()) PageCategory(area);
    else if (page_ == L"Tweaks") PageTweaks(area);
    else if (page_ == L"Tester") PageTester(area);
    else if (page_ == L"Profiles") PageProfiles(area);
    else PageSettings(area);
    ui.PopXf();
    ui.PopAlpha();

    // top bar sits above the scrolled page
    DrawTopBar({sideW_, 0, W_ - sideW_, kTopH});
    DrawSearchResults();
}

// ---------------------------------------------------------------- sidebar

void App::DrawSidebar(float sideW) {
    float open = Clamp01((sideW - kSideClosed) / (kSideOpen - kSideClosed));
    g.Fill({0, 0, sideW, H_}, T.side);
    g.Fill({sideW - 1, 0, 1, H_}, T.border);

    DrawLogo(g, {20, 16, 38, 38});
    ui.PushAlpha(open);
    g.Text(L"Blatant", {70, 16, 170, 22}, T.text, F::H3);
    g.Text(L"Native macro engine", {70, 36, 170, 18}, T.dim, F::Small);
    ui.PopAlpha();

    float footerH = open > 0.5f ? 150.f : 128.f;
    Rect navView{0, 72, sideW, H_ - 72 - footerH};
    ID sid = HashId(L"side-scroll");
    ui.BeginScroll(sid, navView);
    float y = navView.y + 8;

    auto section = [&](const wchar_t* title) {
        y += 6;
        if (open > 0.02f) {
            ui.PushAlpha(open);
            g.Text(title, {24, y, sideW - 40, 18}, T.faint, F::SmallSemi);
            ui.PopAlpha();
        }
        if (open < 0.98f) g.Fill({26, y + 9, 24, 1}, WithA(T.border2, 1 - open));
        y += 26;
    };
    auto badgeFor = [&](const std::wstring& cat) {
        int n = 0;
        for (auto& m : cfg_.macros)
            if (m.enabled && m.category == cat) n++;
        return n;
    };
    auto badge = [&](const Rect& r, const std::wstring& key, int n, float sel) {
        ID pop = HashId(L"badge-" + key);
        auto it = badges_.find(key);
        if (it != badges_.end() && it->second != n) ui.SetAnim(pop, 1.f);
        badges_[key] = n;
        float p = ui.Anim(pop, 0.f, 7.f);
        if (n <= 0 || open < 0.05f) return;
        std::wstring s = std::to_wstring(n);
        Rect b{r.R() - 46, r.y, 34, r.h};
        ui.PushAlpha(open);
        g.Text(s, b, Mix(Mix(T.faint, T.dim, sel), T.accent, p), F::Small, kRight);
        ui.PopAlpha();
    };
    // One sidebar entry; `active` tints it like the screenshot's selected item.
    auto item = [&](const std::wstring& label, const wchar_t* glyph, bool active, ID id, float h = 42.f) {
        Rect r{12, y, sideW - 24, h};
        Interact it = ui.Press(id, r);
        float at = ui.Anim(HashId(id, 3), active ? 1.f : 0.f, 14.f);
        float sp = ui.Spring(HashId(id, 0xB7), it.held ? 1.f : 0.f, 460.f, 26.f);
        ui.PushScale(1.f - 0.04f * sp, r.Cx(), r.Cy());
        g.Round(r, 11, WithA(T.panel2, 0.8f * it.hoverT * (1 - at)));
        g.Round(r, 10, WithA(T.text, 0.07f * at));
        float ix = r.x + 14 + (r.w - 22 - 28) * 0.5f * (1 - open);
        Color ic = Mix(Mix(T.dim, T.text, it.hoverT), T.text, at);
        g.Text(glyph, {ix, r.y, 22, r.h}, ic, F::Icon, kCenter);
        if (open > 0.02f) {
            ui.PushAlpha(open);
            g.Text(label, {r.x + 46 + 3 * it.hoverT * (1 - at), r.y, r.w - 100, r.h},
                   Mix(Mix(T.dim, T.text, 0.55f + 0.45f * it.hoverT), T.text, at), F::Body);
            ui.PopAlpha();
        }
        if (open < 0.5f) ui.Tip(id, label, r);
        ui.PopXf();
        y += h + 2;
        return std::pair<Interact, Rect>{it, r};
    };

    section(L"Main Menu");
    {
        auto [it, r] = item(L"Dashboard", L"", page_ == L"Dashboard", HashId(L"nav-Dashboard"));
        if (it.clicked) Navigate(L"Dashboard");
    }
    bool inGroup = false;
    for (int i = 0; i < 5; i++)
        if (page_ == kCategories[i].name) inGroup = true;
    {
        auto [it, r] = item(L"Macros", L"", inGroup, HashId(L"nav-Macros"));
        // Springy roll so opening the group feels alive, not a hard snap.
        float gt = ui.Spring(HashId(L"grp-open"), macrosOpen_ ? 1.f : 0.f, 300.f, 30.f);
        float gtc = Clamp01(gt);
        if (open > 0.02f) {
            ui.PushAlpha(open);
            Rect ch{r.R() - 30, r.y, 20, r.h};
            ui.PushRotate(-90.f * (1 - gtc), ch.Cx(), ch.Cy());
            g.Text(L"", ch, inGroup ? T.accentHi : T.dim, F::IconSm, kCenter);
            ui.PopXf();
            ui.PopAlpha();
        }
        if (it.clicked) {
            if (open < 0.5f) Navigate(L"Editing");
            else macrosOpen_ = !macrosOpen_;
        }
        float childT = gtc * open;
        if (childT > 0.004f) {
            float full = 5 * 38.f;
            float roll = EaseOut(childT);  // height eases open with a soft landing
            float h = full * roll;
            ui.PushClip({0, y, sideW, h + 2});
            float cy = y;
            for (int i = 0; i < 5; i++) {
                std::wstring name = kCategories[i].name;
                bool sel = page_ == name;
                // Each row cascades in just after the roll reaches it.
                float rev = EaseOut(Clamp01((childT - i * 0.10f) / 0.5f));
                Rect cr{24, cy, sideW - 36, 36};
                ID cid = HashId(L"nav-" + name);
                Interact ci = ui.Press(cid, cr);
                float csp = ui.Spring(HashId(cid, 0xB7), ci.held ? 1.f : 0.f, 460.f, 26.f);
                ui.PushAlpha(rev);
                ui.PushOffset(0, (1 - rev) * 8.f);
                ui.PushScale(1.f - 0.05f * csp, cr.Cx(), cr.Cy());
                g.Round(cr, 9, WithA(T.panel2, 0.7f * ci.hoverT));
                float st = ui.Anim(HashId(cid, 3), sel ? 1.f : 0.f, 14.f);
                g.Fill({cr.x + 8, cr.y + 6, 1, cr.h - 12}, WithA(T.border2, 1 - st));
                g.Circle(cr.x + 8.5f, cr.Cy(), 3.f * st, T.accent);
                g.Text(name, {cr.x + 22 + 3 * ci.hoverT, cr.y, cr.w - 70, cr.h},
                       Mix(Mix(T.dim, T.text, ci.hoverT), T.text, st), F::Body);
                badge(cr, name, badgeFor(name), st);
                if (ci.clicked) Navigate(name);
                ui.PopXf();  // scale
                ui.PopXf();  // offset
                ui.PopAlpha();
                cy += 38;
            }
            ui.PopClip();
            y += h;
        }
    }
    section(L"Tools");
    for (const wchar_t* p : {L"Tweaks", L"Tester", L"Profiles", L"Settings"}) {
        auto [it, r] = item(p, PageGlyph(p), page_ == p, HashId(std::wstring(L"nav-") + p));
        if (it.clicked) Navigate(p);
    }
    y += 10;
    ui.EndScroll(sid, y - navView.y);
    DrawSideFooter(sideW, open);
}

void App::DrawSideFooter(float sideW, float open) {
    float footerH = open > 0.5f ? 150.f : 128.f;
    float y = H_ - footerH;
    g.Fill({0, y, sideW, 1}, T.border);
    y += 14;
    bool light = cfg_.theme == L"Light";
    if (open > 0.5f) {
        Rect seg{12, y, sideW - 24, 40};
        g.Round(seg, 11, T.field);
        g.RoundStroke(seg, 11, T.border);
        float hw = (seg.w - 8) / 2;
        float tx = ui.Spring(0x7A9E5, seg.x + 4 + (light ? 0 : hw), 420.f, 32.f);
        Rect thumb{tx, seg.y + 4, hw, seg.h - 8};
        g.Round(thumb, 8, T.thumb);
        g.RoundStroke(thumb, 8, T.border2);
        const wchar_t* glyphs[2] = {L"", L""};
        const wchar_t* names[2] = {L"Light", L"Dark"};
        for (int i = 0; i < 2; i++) {
            Rect s{seg.x + 4 + hw * i, seg.y + 4, hw, seg.h - 8};
            Interact it = ui.Press(HashId(L"theme-seg", (uint64_t)i), s);
            bool sel = (i == 0) == light;
            Color c = sel ? T.text : Mix(T.dim, T.text, it.hoverT);
            float w = g.TextW(names[i], F::Semi) + 22;
            g.Text(glyphs[i], {s.Cx() - w / 2, s.y, 16, s.h}, c, F::IconSm);
            g.Text(names[i], {s.Cx() - w / 2 + 22, s.y, w, s.h}, c, F::Semi);
            if (it.clicked && !sel) SetTheme(i == 0);
        }
    } else {
        Rect b{(sideW - 40) / 2, y, 40, 40};
        if (ui.IconButton(HashId(L"theme-mini"), b, light ? L"" : L"", light ? L"Dark theme" : L"Light theme"))
            SetTheme(!light);
    }
    y += 52;

    bool on = cfg_.enabled;
    float onT = ui.Anim(0x57A7, on ? 1.f : 0.f, 8.f);
    Rect card{12, y, sideW - 24, 66};
    if (open > 0.5f) {
        g.Round(card, 12, T.panel2);
        g.RoundStroke(card, 12, T.border);
    }
    float ax = open > 0.5f ? card.x + 12 : (sideW - 42) / 2;
    Rect av{ax, card.y + 12, 42, 42};
    Interact ai = ui.Press(HashId(L"side-avatar"), av);
    g.Round(av, 21, Mix(T.panel3, T.border2, ai.hoverT));
    g.Text(L"", av, T.text, F::Icon, kCenter);
    if (on && active_ && !ui.reduceMotion) {
        float ph = (float)std::fmod(ui.time, 1.8) / 1.8f;
        g.Circle(av.R() - 2, av.B() - 2, 5.f + 7.f * ph, WithA(T.green, 0.45f * (1 - ph)));
        ui.Keep();
    }
    g.Circle(av.R() - 2, av.B() - 2, 6, T.panel2);
    g.Circle(av.R() - 2, av.B() - 2, 4.5f, Mix(T.faint, T.green, onT));
    ui.Tip(HashId(L"side-avatar"), on ? L"Macros are on. Click to pause them." : L"Macros are paused. Click to turn them on.", av);
    if (ai.clicked) SetMaster(!on);
    if (open > 0.5f) {
        ui.PushAlpha(Clamp01(open * 2 - 1));
        g.Text(on ? L"Macros on" : L"Macros paused", {av.R() + 12, card.y + 14, card.w - 120, 20}, T.text, F::Semi);
        g.Text(Pretty(cfg_.toggleKey) + L" toggles in game", {av.R() + 12, card.y + 34, card.w - 120, 18}, T.dim, F::Small);
        bool v = on;
        if (ui.Switch(0x5717C4, card.R() - 12 - 40, card.Cy() - 11, v)) SetMaster(v);
        ui.PopAlpha();
    }
}

// ---------------------------------------------------------------- top bar & search

void App::DrawTopBar(const Rect& bar) {
    g.Fill(bar, WithA(T.bg, 0.92f));
    g.Fill({bar.x, bar.B() - 1, bar.w, 1}, T.border);

    Rect col{bar.x + 24, 16, 38, 38};
    holes_.push_back(ui.ToScreen(col));
    Interact ct = ui.Press(HashId(L"collapse"), col);
    g.Round(col, 11, Mix(T.panel, T.panel2, ct.hoverT));
    g.RoundStroke(col, 11, T.border);
    g.Text(cfg_.sidebarCollapsed ? L"" : L"", col, Mix(T.dim, T.text, ct.hoverT), F::IconSm, kCenter);
    ui.Tip(HashId(L"collapse"), cfg_.sidebarCollapsed ? L"Show sidebar" : L"Collapse sidebar", col);
    if (ct.clicked) {
        cfg_.sidebarCollapsed = !cfg_.sidebarCollapsed;
        SaveCfg();
    }

    float rightEdge = bar.R() - 46 * 3 - 18;
    float sw = std::clamp(rightEdge - 330 - (col.R() + 14), 180.f, 380.f);
    Rect sr{col.R() + 14, 15, sw, 40};
    holes_.push_back(ui.ToScreen(sr));
    ID sid = HashId(L"search");
    TextBoxOpts so;
    so.placeholder = L"Search macros and pages…";
    so.padLeft = 40;
    so.maxLen = 60;
    auto res = ui.TextBox(sid, sr, search_, so);
    g.Text(L"", {sr.x + 14, sr.y, 18, sr.h}, T.dim, F::IconSm);
    if (search_.empty() && !res.focused) {
        Rect kbd{sr.R() - 34, sr.y + 9, 22, 22};
        g.Round(kbd, 6, T.panel3);
        g.RoundStroke(kbd, 6, T.border2);
        g.Text(L"/", kbd, T.dim, F::SmallSemi, kCenter);
    }
    if (res.changed) searchSel_ = 0;
    if (res.focused) {
        auto hits = SearchResults();
        for (auto& k : ui.in.keys) {
            if (k.vk == VK_DOWN) searchSel_ = std::min(searchSel_ + 1, (int)hits.size() - 1);
            if (k.vk == VK_UP) searchSel_ = std::max(searchSel_ - 1, 0);
        }
        if (res.submitted && !hits.empty()) {
            auto h = hits[std::clamp(searchSel_, 0, (int)hits.size() - 1)];
            search_.clear();
            ui.focus = 0;
            Navigate(h.page);
            if (h.macroId) OpenEditor(h.macroId);
        }
    }

    // right cluster: theme, notifications, macro status
    float x = rightEdge;
    bool on = cfg_.enabled;
    Rect av{x - 42, 13, 42, 42};
    std::wstring t1 = on ? L"Macros on" : L"Macros paused", t2 = Pretty(cfg_.toggleKey) + L" · " + TimingLabel();
    float tw = std::max(g.TextW(t1, F::Semi), g.TextW(t2, F::Small));
    tw = std::min(tw, 230.f);
    Rect status{av.x - tw - 14, 13, tw + 14 + 42, 42};
    holes_.push_back(ui.ToScreen(status));
    Interact st = ui.Press(HashId(L"top-status"), status);
    g.Text(t1, {av.x - tw - 12, 15, tw, 20}, T.text, F::Semi, kRight);
    g.Text(t2, {av.x - tw - 12, 35, tw, 18}, T.dim, F::Small, kRight);
    g.Round(av, 21, Mix(T.panel3, T.border2, st.hoverT));
    g.Text(L"MT", av, T.text, F::SmallSemi, kCenter);
    g.Circle(av.R() - 1, av.B() - 1, 6, T.bg);
    g.Circle(av.R() - 1, av.B() - 1, 4.5f, on ? T.green : T.faint);
    ui.Tip(HashId(L"top-status"), on ? L"Click to pause all macros" : L"Click to turn macros on", status);
    if (st.clicked) SetMaster(!on);

    x = status.x - 18;
    g.Fill({x, 22, 1, 24}, T.border2);
    x -= 18 + 38;
    Rect bell{x, 15, 38, 38};
    holes_.push_back(ui.ToScreen(bell));
    if (ui.IconButton(HashId(L"bell"), bell, L"", L"Notifications")) {
        std::vector<MenuItem> items;
        for (auto& m : cfg_.macros) {
            auto it = issues_.find(m.id);
            if (it == issues_.end()) continue;
            int id = m.id;
            std::wstring cat = m.category;
            items.push_back({m.name + L": " + it->second.text, L"", it->second.error, [this, id, cat] {
                                 Navigate(cat);
                                 OpenEditor(id);
                             }});
            if (items.size() >= 8) break;
        }
        if (items.empty()) items.push_back({L"All macros look good", L"", false, nullptr});
        OpenMenu(ui.ToScreen(bell), items, 360);
    }
    if (!issues_.empty()) {
        g.Circle(bell.R() - 9, bell.y + 9, 5, T.bg);
        g.Circle(bell.R() - 9, bell.y + 9, 3.5f, T.red);
    }
    x -= 8 + 38;
    Rect th{x, 15, 38, 38};
    holes_.push_back(ui.ToScreen(th));
    bool light = cfg_.theme == L"Light";
    if (ui.IconButton(HashId(L"theme-top"), th, light ? L"" : L"", light ? L"Dark theme" : L"Light theme"))
        SetTheme(!light);
}

std::vector<App::SearchHit> App::SearchResults() {
    std::vector<SearchHit> hits;
    std::wstring q = Lower(Trim(search_));
    std::vector<std::wstring> pages = {L"Dashboard", L"Editing", L"Building", L"Movement", L"Utility", L"Custom",
                                       L"Tweaks", L"Tester", L"Profiles", L"Settings"};
    for (auto& p : pages)
        if (q.empty() || Lower(p).find(q) != std::wstring::npos)
            hits.push_back({p, L"Page", p, PageGlyph(p), 0});
    if (!q.empty()) {
        for (auto& m : cfg_.macros) {
            if (hits.size() >= 9) break;
            if (Lower(m.name).find(q) == std::wstring::npos && Lower(m.category).find(q) == std::wstring::npos &&
                Lower(Pretty(m.trigger)) != q)
                continue;
            hits.push_back({m.name, m.category + (m.trigger.empty() ? L"" : L"  ·  " + Pretty(m.trigger)), m.category,
                            PageGlyph(m.category), m.id});
        }
    }
    if (hits.size() > 9) hits.resize(9);
    return hits;
}

void App::DrawSearchResults() {
    ID sid = HashId(L"search");
    float t = ui.Anim(HashId(sid, 7), ui.focus == sid ? 1.f : 0.f, 20.f);
    if (t < 0.01f) return;
    auto hits = SearchResults();
    float sw = std::max(380.f, std::clamp(W_ - 46 * 3 - 18 - 330 - (sideW_ + 76), 180.f, 380.f));
    Rect box{sideW_ + 76, kTopH - 6 + (1 - t) * 8, sw, 16 + std::max(1, (int)hits.size()) * 48.f};
    holes_.push_back(ui.ToScreen(box));
    ui.PushAlpha(t);
    g.Shadow(box, 14, 26, 0.6f, 8);
    g.Round(box, 14, T.panel);
    g.RoundStroke(box, 14, T.border2);
    if (hits.empty()) g.Text(L"Nothing matches “" + search_ + L"”", box.Inset(18, 8), T.dim, F::Body);
    float y = box.y + 8;
    searchSel_ = std::clamp(searchSel_, 0, std::max(0, (int)hits.size() - 1));
    for (int i = 0; i < (int)hits.size(); i++) {
        auto& h = hits[i];
        Rect r{box.x + 8, y, box.w - 16, 46};
        Interact it = ui.Press(HashId(sid, 100 + (uint64_t)i), r);
        if (it.hover) searchSel_ = i;
        float s = ui.Anim(HashId(sid, 200 + (uint64_t)i), i == searchSel_ ? 1.f : 0.f, 24.f);
        g.Round(r, 10, WithA(T.accent, 0.12f * s));
        Rect tile{r.x + 8, r.y + 7, 32, 32};
        g.Round(tile, 9, Mix(T.panel3, WithA(T.accent, 0.25f), s));
        g.Text(h.glyph, tile, Mix(T.dim, T.accentHi, s), F::IconSm, kCenter);
        g.Text(h.title, {tile.R() + 12, r.y + 5, r.w - 120, 20}, T.text, F::Semi);
        g.Text(h.sub, {tile.R() + 12, r.y + 24, r.w - 120, 18}, T.dim, F::Small);
        if (s > 0.5f) g.Text(L"↵", {r.R() - 36, r.y, 24, r.h}, T.faint, F::Body, kCenter);
        if (it.pressed) {
            // act on press: the text box loses focus on this same click
            std::wstring page = h.page;
            int id = h.macroId;
            search_.clear();
            ui.focus = 0;
            ui.PopAlpha();
            Navigate(page);
            if (id) OpenEditor(id);
            return;
        }
        y += 48;
    }
    ui.PopAlpha();
}

// ---------------------------------------------------------------- header & window buttons

void App::DrawHeader(const Rect& r) {
    std::wstring title = page_, sub;
    if (page_ == L"Dashboard") {
        sub = L"See how your macros perform and how often they fire, live.";
    } else if (IsMacroPage()) {
        int total = 0, on = 0;
        for (auto& m : cfg_.macros)
            if (m.category == page_) {
                total++;
                on += m.enabled;
            }
        sub = Fmt(L"%d of %d macros active", on, total);
    } else if (page_ == L"Tweaks") {
        sub = L"Windows settings for FPS, input and ping";
        if (Monitor::pingMs.load() >= 0) sub += Fmt(L"  ·  ping %d ms", Monitor::pingMs.load());
    } else if (page_ == L"Tester") {
        sub = L"Press a macro's key to see every input it sends, with timing";
    } else if (page_ == L"Profiles") {
        sub = L"Save your macros and binds, switch setups in one click";
    } else {
        sub = L"Timing, hotkeys and your in-game binds";
    }
    float t = ui.reduceMotion ? 1.f : EaseOut((float)(ui.time - pageStart_) / 0.4f);
    if (t < 1) ui.Keep();
    ui.PushAlpha(t);
    ui.PushOffset(14 * (1 - t), 0);
    g.Text(title, {r.x, r.y - 2, r.w - 340, 36}, T.text, F::Hero);
    g.Text(sub, {r.x + 1, r.y + 34, r.w - 340, 22}, T.dim, F::Body);
    ui.PopXf();
    ui.PopAlpha();

    float bx = r.R();
    auto btn = [&](const std::wstring& label, const wchar_t* icon, Btn style, float w) {
        bx -= w;
        Rect b{bx, r.y + 6, w, 42};
        bx -= 12;
        return std::pair<bool, Rect>{ui.Button(HashId(label, 0x4D5), b, label, style, icon), b};
    };
    if (page_ == L"Dashboard") {
        if (btn(L"Export", L"", Btn::Primary, 112).first) ExportBackup();
        std::wstring range = rangeDays_ == 1 ? L"Last 24 hours" : Fmt(L"Last %d days", rangeDays_);
        auto [clicked, b] = btn(range + L"  ▾", nullptr, Btn::Secondary, 150);
        if (clicked) {
            OpenMenu(ui.ToScreen(b),
                     {{L"Last 24 hours", L"", false, [this] { rangeDays_ = 1; pageStart_ = ui.time; }},
                      {L"Last 7 days", L"", false, [this] { rangeDays_ = 7; pageStart_ = ui.time; }},
                      {L"Last 30 days", L"", false, [this] { rangeDays_ = 30; pageStart_ = ui.time; }}},
                     190);
        }
    } else if (IsMacroPage()) {
        if (btn(L"New macro", L"", Btn::Primary, 136).first) OpenEditor(0);
        if (btn(L"Import", L"", Btn::Secondary, 108).first)
            ImportMacro();
    } else if (page_ == L"Tweaks") {
        if (btn(L"Revert all", L"", Btn::Secondary, 128).first) RevertAllTweaks();
    } else if (page_ == L"Tester") {
        if (!runs_.empty() && btn(L"Clear", L"", Btn::Secondary, 100).first) runs_.clear();
    } else if (page_ == L"Profiles") {
        if (btn(L"Save current", L"", Btn::Primary, 148).first) {
            Prompt(L"Save profile", L"PROFILE NAME", L"", [this](const std::wstring& name) {
                SaveProfile(name, false);
                return true;
            });
        }
    }
}

void App::DrawWindowButtons() {
    bool maxed = hwnd_ && IsZoomed(hwnd_);
    static const Color dots[3] = {Rgb(0xC9A94F), Rgb(0x55C9A6), Rgb(0xE77FA0)};
    const wchar_t* glyphs[3] = {L"\ue921", maxed ? L"\ue923" : L"\ue922", L"\ue8bb"};
    const wchar_t* tips[3] = {L"Minimize", maxed ? L"Restore" : L"Maximize", L"Close"};
    for (int i = 0; i < 3; i++) {
        float cx = W_ - 74 + 24 * i, cy = kChrome / 2;
        Rect r{cx - 11, cy - 11, 22, 22};
        ID id = HashId(L"winbtn", (uint64_t)i);
        Interact it = ui.Press(id, r);
        g.Circle(cx, cy, 6.5f + 1.f * it.hoverT, Mix(dots[i], Rgb(0xFFFFFF), 0.15f * it.hoverT));
        if (it.hoverT > 0.05f) g.Text(glyphs[i], r, WithA(Rgb(0x1A1A1A), it.hoverT), F::Tiny, kCenter);
        ui.Tip(id, tips[i], r);
        if (it.clicked && hwnd_) {
            if (i == 0) ShowWindow(hwnd_, SW_MINIMIZE);
            else if (i == 1) ShowWindow(hwnd_, maxed ? SW_RESTORE : SW_MAXIMIZE);
            else PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
    }
}

// ---------------------------------------------------------------- modal frames

Rect App::ModalBox(ModalKind k, float t) {
    float w = 440, h = 200;
    switch (k) {
        case ModalKind::Editor:
            w = std::min(680.f, W_ - 80);
            h = std::min(H_ - 64, 900.f);
            break;
        case ModalKind::Prompt: h = pr_.error.empty() ? 214.f : 236.f; break;
        case ModalKind::Confirm: h = 160 + g.TextH(cf_.text, F::Body, w - 48); break;
        case ModalKind::Message:
            w = 480;
            h = 156 + std::min(320.f, g.TextH(msg_.text, F::Body, w - 48));
            break;
    }
    return {std::round((W_ - w) / 2), std::round((H_ - h) / 2 + (1 - t) * 20), w, h};
}

void App::DrawModal(ModalEntry& m, bool input) {
    float t = EaseOut(m.t);
    Rect box = ModalBox(m.kind, t);
    ui.PushAlpha(Clamp01(m.t * 1.5f));
    ui.PushScale(0.96f + 0.04f * t, box.Cx(), box.Cy());
    g.Shadow(box, 20, 46, 0.8f, 16);
    g.Round(box, 20, T.panel);
    g.RoundStroke(box, 20, T.border2);
    switch (m.kind) {
        case ModalKind::Editor: DrawEditor(box, input); break;
        case ModalKind::Prompt: DrawPrompt(box); break;
        case ModalKind::Confirm: DrawConfirm(box); break;
        case ModalKind::Message: DrawMessage(box); break;
    }
    ui.PopXf();
    ui.PopAlpha();
}

void App::DrawPrompt(const Rect& b) {
    g.Text(pr_.title, {b.x + 24, b.y + 20, b.w - 90, 28}, T.text, F::H2);
    if (ui.IconButton(HashId(L"prompt-x"), {b.R() - 20 - 32, b.y + 18, 32, 32}, L"")) CloseModal(ModalKind::Prompt);
    Caption(pr_.label, b.x + 24, b.y + 64, T.faint);
    TextBoxOpts o;
    o.mono = pr_.mono;
    o.placeholder = pr_.placeholder.c_str();
    o.error = !pr_.error.empty();
    o.maxLen = 60000;
    auto r = ui.TextBox(HashId(L"prompt-input"), {b.x + 24, b.y + 84, b.w - 48, 42}, pr_.text, o);
    if (r.changed) pr_.error.clear();
    if (!pr_.error.empty()) {
        g.Text(L"", {b.x + 24, b.y + 134, 16, 20}, T.amber, F::IconSm);
        g.Text(pr_.error, {b.x + 46, b.y + 134, b.w - 70, 20}, T.amber, F::Small);
    }
    float by = b.B() - 24 - 38;
    bool ok = ui.Button(HashId(L"prompt-ok"), {b.R() - 24 - 110, by, 110, 38}, pr_.ok, Btn::Primary);
    if (ui.Button(HashId(L"prompt-cancel"), {b.R() - 24 - 110 - 10 - 100, by, 100, 38}, L"Cancel", Btn::Secondary))
        CloseModal(ModalKind::Prompt);
    if ((ok || r.submitted) && ModalOpen(ModalKind::Prompt)) {
        auto cb = pr_.onOk;
        if (!cb || cb(Trim(pr_.text))) CloseModal(ModalKind::Prompt);
    }
}

void App::DrawConfirm(const Rect& b) {
    float ih = 0;
    if (cf_.danger) {
        g.Circle(b.x + 40, b.y + 40, 16, WithA(T.red, 0.15f));
        g.Text(L"", {b.x + 24, b.y + 24, 32, 32}, T.red, F::IconSm, kCenter);
        ih = 44;
    }
    g.Text(cf_.title, {b.x + 24 + ih, b.y + 26, b.w - 48 - ih, 28}, T.text, F::H2);
    g.Text(cf_.text, {b.x + 24, b.y + 72, b.w - 48, b.h - 150}, T.dim, F::Body, kLeft, false, true);
    float by = b.B() - 24 - 38;
    bool ok = ui.Button(HashId(L"cf-ok"), {b.R() - 24 - 116, by, 116, 38}, cf_.ok, cf_.danger ? Btn::Danger : Btn::Primary);
    if (ui.Button(HashId(L"cf-cancel"), {b.R() - 24 - 116 - 10 - 100, by, 100, 38}, L"Cancel", Btn::Secondary))
        CloseModal(ModalKind::Confirm);
    if (ok) {
        auto cb = cf_.onOk;
        CloseModal(ModalKind::Confirm);
        if (cb) cb();
    }
}

void App::DrawMessage(const Rect& b) {
    Color c = msg_.error ? T.amber : T.accentHi;
    g.Circle(b.x + 40, b.y + 40, 16, WithA(c, 0.15f));
    g.Text(msg_.error ? L"" : L"", {b.x + 24, b.y + 24, 32, 32}, c, F::IconSm, kCenter);
    g.Text(msg_.title, {b.x + 68, b.y + 26, b.w - 92, 28}, T.text, F::H2);
    g.Text(msg_.text, {b.x + 24, b.y + 72, b.w - 48, b.h - 150}, T.dim, F::Body, kLeft, false, true);
    if (ui.Button(HashId(L"msg-ok"), {b.R() - 24 - 100, b.B() - 24 - 38, 100, 38}, L"OK", Btn::Primary))
        CloseModal(ModalKind::Message);
}

void App::DrawMenu() {
    float t = ui.Anim(menu_.anim, menu_.open ? 1.f : 0.f, menu_.open ? 22.f : 30.f);
    if (t <= 0.01f || menu_.items.empty()) return;
    float w = menu_.width, ih = 38;
    float h = 12;
    for (size_t i = 0; i < menu_.items.size(); i++) h += ih + ((menu_.items[i].danger && i > 0 && !menu_.items[i - 1].danger) ? 9 : 0);
    const Rect& a = menu_.anchor;
    Rect box{std::max(8.f, a.R() - w), a.B() + 6, w, h};
    bool up = box.B() > H_ - 8;
    if (up) box.y = a.y - 6 - h;
    bool input = menu_.open;
    ui.PushAlpha(t);
    ui.PushScale(0.92f + 0.08f * EaseOut(t), box.R() - 16, up ? box.B() : box.y);
    g.Shadow(box, 14, 24, 0.65f, 8);
    g.Round(box, 14, T.panel);
    g.RoundStroke(box, 14, T.border2);
    float y = box.y + 6;
    std::function<void()> pending;
    bool clickedItem = false;
    for (size_t i = 0; i < menu_.items.size(); i++) {
        auto& item = menu_.items[i];
        if (item.danger && i > 0 && !menu_.items[i - 1].danger) {
            g.Fill({box.x + 12, y + 4, box.w - 24, 1}, T.border2);
            y += 9;
        }
        Rect r{box.x + 6, y, box.w - 12, ih};
        ui.inputOn = input;
        Interact it = ui.Press(HashId(menu_.anim, i + 1), r, item.act != nullptr);
        g.Round(r, 9, item.danger ? WithA(T.red, 0.14f * it.hoverT) : WithA(T.panel3, it.hoverT));
        g.Text(item.glyph, {r.x + 8, r.y, 22, r.h}, item.danger ? T.red : Mix(T.dim, T.text, it.hoverT), F::IconSm, kCenter);
        g.Text(item.label, {r.x + 38, r.y, r.w - 44, r.h}, item.danger ? T.red : T.text, F::Body);
        if (it.clicked && input) {
            pending = item.act;
            clickedItem = true;
        }
        y += ih;
    }
    if (input && ui.in.pressed && !box.Has(ui.in.mx, ui.in.my)) menu_.open = false;
    ui.PopXf();
    ui.PopAlpha();
    ui.inputOn = true;
    if (clickedItem) {
        menu_.open = false;
        if (pending) pending();
    }
}

void App::DrawToasts() {
    double now = ui.time;
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](auto& t) { return now - t.born > 3.7; }),
                  toasts_.end());
    float y = H_ - 26;
    float cx = sideW_ + (W_ - sideW_) / 2;
    for (int i = (int)toasts_.size() - 1; i >= 0; i--) {
        Toast& t = toasts_[i];
        double age = now - t.born;
        float in = ui.reduceMotion ? 1.f : EaseOutBack((float)age / 0.4f);
        float out = age > 3.3 ? 1 - Clamp01((float)(age - 3.3) / 0.35f) : 1.f;
        if (age < 0.45 || age > 3.3) ui.Keep();
        else ui.WakeIn(3.3 - age);
        float tw = std::min(520.f, g.TextW(t.text, F::Body));
        float w = tw + 64, h = 46;
        float yy = ui.Spring(t.id, y - h, 380.f, 30.f);
        Rect r{std::round(cx - w / 2), yy + (1 - in) * 26, w, h};
        Color c = t.kind == 1 ? T.green : t.kind == 2 ? T.amber : T.accentHi;
        const wchar_t* glyph = t.kind == 1 ? L"" : t.kind == 2 ? L"" : L"";
        ui.PushAlpha(Clamp01(in) * out);
        g.Shadow(r, 14, 20, 0.6f, 6);
        g.Round(r, 14, T.panel);
        g.RoundStroke(r, 14, T.border2);
        g.Circle(r.x + 24, r.Cy(), 12, WithA(c, 0.16f));
        g.Text(glyph, {r.x + 12, r.y, 24, r.h}, c, F::IconSm, kCenter);
        g.Text(t.text, {r.x + 46, r.y, tw + 6, r.h}, T.text, F::Body);
        ui.PopAlpha();
        y -= h + 10;
    }
}

// ---------------------------------------------------------------- license

void App::Activate(const std::wstring& key, bool remember) {
    if (licBusy_) return;
    licBusy_ = true;
    licError_.clear();
    RunAsync([this, key, remember]() -> std::function<void()> {
        LicenseResult r = License::Activate(key);
        return [this, r, key, remember] {
            licBusy_ = false;
            if (!r.ok) {
                licError_ = r.error;
                if (!remember) licKey_ = key;  // a saved key failed: show it so it can be fixed
                return;
            }
            lic_ = r;
            licensed_ = true;
            licKey_.clear();
            License::SaveKey(key);
            engine_.SetLocked(false);
            pageStart_ = ui.time;
            long long rem = r.expiry - (long long)time(nullptr);
            bool low = r.expiry > 0 && rem > 0 && rem < 2 * 86400;
            if (low)
                ShowToast(rem < 86400 ? Fmt(L"Heads up: your key expires in %lld hours.", std::max(1ll, rem / 3600))
                                      : L"Heads up: your key expires in under 2 days.", 2);
            else if (remember)
                ShowToast(L"Activated. Welcome!", 1);
        };
    });
}

void App::SignOut(const std::wstring& why) {
    licensed_ = false;
    lic_ = {};
    licError_ = why;
    engine_.SetLocked(true);
    running_.clear();
    CloseModal(ModalKind::Editor);
    menu_.open = false;
    License::ForgetKey();
}

// Shown instead of the app until a KeyAuth key checks out.
void App::DrawLicenseGate(float W, float H) {
    float t = ui.Anim(0x11CE5E, 1.f, 6.f, 0.f);
    Rect card{std::round((W - 440) / 2), std::round((H - 380) / 2 + 10 * (1 - t)), 440, 380};
    ui.PushAlpha(EaseOut(t));
    g.Round(card, 14, T.panel);
    g.RoundStroke(card, 14, T.border);
    DrawLogo(g, {card.Cx() - 24, card.y + 30, 48, 48});
    g.Text(L"Activate Macro Tool", {card.x, card.y + 92, card.w, 30}, T.text, F::H2, kCenter);
    g.Text(L"Enter your license key. It gets locked to this PC.", {card.x + 30, card.y + 124, card.w - 60, 20}, T.dim,
           F::Body, kCenter);
    Rect box{card.x + 30, card.y + 166, card.w - 60, 44};
    TextBoxOpts o;
    o.mono = true;
    o.placeholder = L"XXXXXX-XXXXXX-XXXXXX";
    o.maxLen = 120;
    o.error = !licError_.empty();
    ID kid = HashId(L"license-key");
    if (ui.focus == 0 && !licBusy_) ui.focus = kid;
    auto res = ui.TextBox(kid, box, licKey_, o);
    if (res.changed) licError_.clear();
    Rect btn{card.x + 30, card.y + 222, card.w - 60, 44};
    bool configured = License::Configured();
    bool go = ui.Button(HashId(L"license-go"), btn, licBusy_ ? L"" : L"Activate", Btn::Primary, nullptr,
                        configured && !licBusy_ && !Trim(licKey_).empty());
    if (licBusy_) ui.Spinner(btn.Cx(), btn.Cy(), 9, T.onPrimary);
    if ((go || res.submitted) && configured && !licBusy_ && !Trim(licKey_).empty()) Activate(licKey_, true);
    std::wstring msg = !configured ? L"This build isn't connected to KeyAuth yet (license.h)."
                       : licBusy_  ? L"Checking your key…"
                                   : licError_;
    if (!msg.empty())
        g.Text(msg, {card.x + 30, card.y + 276, card.w - 60, 40}, licBusy_ ? T.dim : T.red, F::Small, kCenter, false, true);
    std::wstring hw = L"HWID " + License::Hwid().substr(0, 12) + L"…";
    float hw_w = g.TextW(hw, F::MonoSmall);
    g.Text(hw, {card.Cx() - (hw_w + 70) / 2, card.B() - 44, hw_w + 4, 24}, T.faint, F::MonoSmall);
    if (ui.Button(HashId(L"license-copyhwid"), {card.Cx() - (hw_w + 70) / 2 + hw_w + 10, card.B() - 46, 60, 28}, L"Copy",
                  Btn::Ghost)) {
        SetClipboardText(hwnd_, License::Hwid());
        ShowToast(L"HWID copied", 1);
    }
    ui.PopAlpha();
}

// First-run walkthrough: a few centred cards explaining the Fortnite setup.
void App::DrawTutorial(float W, float H) {
    struct Step {
        const wchar_t* glyph;
        const wchar_t* title;
        const wchar_t* body;
    };
    static const Step steps[] = {
        {L"", L"Welcome to Macro Tool",
         L"Fast edits, builds and resets in Fortnite, each fired from a single key. "
         L"This 30-second setup gets you ready."},
        {L"", L"1 · Match your keybinds",
         L"Open Settings → Game keybinds and set Edit, Wall, Floor, Cone… to the exact keys you use in "
         L"Fortnite. Macros refer to them as {Edit}, {Wall} and so on."},
        {L"", L"2 · Give a macro a key",
         L"On any macro, click “Set key” and press the button that should fire it — mouse buttons work "
         L"too. Flip the switch on and it's live."},
        {L"", L"3 · Frame-perfect timing",
         L"Every step lands on its own game frame. It reads your FPS automatically, so there's nothing to tune."},
        {L"", L"4 · F8 toggles everything",
         L"Press F8 in game to turn all macros on or off instantly. You'll always know the state from the dot "
         L"in the corner."},
    };
    int n = (int)std::size(steps);
    int i = std::clamp(tutorialStep_, 0, n - 1);
    const Step& s = steps[i];

    g.Fill({0, 0, W, H}, WithA(T.overlay, 1.1f));
    float appear = ui.Anim(0x70701A, 1.f, 10.f, 0.f);
    float slide = ui.Spring(HashId(L"tut-slide", (uint64_t)i), 0.f, 420.f, 34.f);  // nudge on step change
    Rect card{std::round((W - 460) / 2), std::round((H - 360) / 2 + 12 * (1 - appear)), 460, 360};
    ui.PushAlpha(EaseOut(appear));
    g.Shadow(card, 16, 30, 0.4f, 8);
    g.Round(card, 16, T.panel);
    g.RoundStroke(card, 16, T.border2);
    ui.PushClip(card);
    ui.PushOffset(slide, 0);
    Rect badge{card.Cx() - 32, card.y + 36, 64, 64};
    g.Round(badge, 18, WithA(T.accent, 0.14f));
    g.Text(s.glyph, badge, T.accentHi, F::IconXl, kCenter);
    g.Text(s.title, {card.x + 30, card.y + 118, card.w - 60, 28}, T.text, F::H2, kCenter);
    g.Text(s.body, {card.x + 36, card.y + 156, card.w - 72, 110}, T.dim, F::Body, kCenter, false, true);
    ui.PopXf();
    ui.PopClip();

    // progress dots
    float dotY = card.B() - 64, dw = n * 16.f;
    for (int k = 0; k < n; k++) {
        float on = ui.Anim(HashId(L"tut-dot", (uint64_t)k), k == i ? 1.f : 0.f, 14.f);
        g.Circle(card.Cx() - dw / 2 + 8 + k * 16, dotY, 3.5f + 1.5f * on, Mix(T.border2, T.accent, on));
    }
    // buttons
    Rect back{card.x + 24, card.B() - 24 - 38, 96, 38};
    Rect next{card.R() - 24 - 130, card.B() - 24 - 38, 130, 38};
    bool last = i == n - 1;
    if (i > 0 && ui.Button(HashId(L"tut-back"), back, L"Back", Btn::Ghost)) {
        tutorialStep_ = i - 1;
        ui.SetAnim(HashId(L"tut-slide", (uint64_t)tutorialStep_), 26.f);
    }
    if (!last && ui.Button(HashId(L"tut-skip"), {card.Cx() - 48, card.B() - 24 - 38, 96, 38}, L"Skip", Btn::Ghost)) {
        cfg_.tutorialDone = true;
        SaveCfg();
    }
    if (ui.Button(HashId(L"tut-next"), next, last ? L"Start building" : L"Next", Btn::Primary)) {
        if (last) {
            cfg_.tutorialDone = true;
            SaveCfg();
            Navigate(L"Editing");
            ShowToast(L"You're set. Fire away!", 1);
        } else {
            tutorialStep_ = i + 1;
            ui.SetAnim(HashId(L"tut-slide", (uint64_t)tutorialStep_), -26.f);
            if (i == 0) Navigate(L"Settings");  // show where the keybinds live
        }
    }
    // Enter advances, Esc skips
    for (auto& k : ui.in.keys) {
        if (k.vk == VK_RETURN) {
            if (last) {
                cfg_.tutorialDone = true;
                SaveCfg();
                Navigate(L"Editing");
            } else {
                tutorialStep_ = i + 1;
            }
        } else if (k.vk == VK_ESCAPE) {
            cfg_.tutorialDone = true;
            SaveCfg();
        }
    }
    ui.PopAlpha();
}
