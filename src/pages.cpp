#include "app.h"
#include "monitor.h"
#include "menu.h"
#include "osd.h"
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <ctime>

// ---------------------------------------------------------------- navigation & actions

void App::Navigate(const std::wstring& p) {
    if (page_ == p) return;
    bool wasTester = page_ == L"Tester";
    page_ = p;
    pageStart_ = ui.time > 0 ? ui.time : NowSeconds();
    CancelCapture();
    menu_.open = false;
    ui.focus = 0;
    ui.ScrollReset(HashId(L"scroll-" + p));
    if (p == L"Tweaks") {
        tweaksLoaded_ = false;
        LoadTweaks();
    }
    if (p == L"Profiles") RefreshProfiles();
    if (p == L"Tester") {
        Tester::SetListening(true);
    } else if (wasTester) {
        Tester::SetListening(false);
        Input::dryRun = false;
    }
}

void App::OpenEditor(int id) {
    EditorState e;
    if (id) {
        Macro* m = FindMacro(id);
        if (!m) return;
        e.macroId = id;
        e.name = m->name;
        e.trigger = m->trigger;
        e.steps = m->steps;
        e.release = m->releaseSteps;
        for (int i = 0; i < kCategoryCount; i++)
            if (m->category == kCategories[i].name) e.cat = i;
        e.hold = m->holdMs;
        e.delay = m->stepDelay;
        e.repeatDelay = m->repeatDelay;
        e.repeat = m->repeat;
        e.toggle = m->toggle;
        e.block = m->blockTrigger;
    } else {
        e.isNew = true;
        e.name.clear();
        for (int i = 0; i < kCategoryCount; i++)
            if (page_ == kCategories[i].name) e.cat = i;
    }
    ed_ = e;
    OpenModal(ModalKind::Editor);
    ui.ScrollReset(HashId(L"ed-scroll"));
    if (e.isNew) ui.focus = HashId(L"ed-name");
}

void App::CloseEditor() {
    if (ed_.recording) {
        engine_.EndRecord();
        ed_.recording = false;
    }
    CloseModal(ModalKind::Editor);
}

void App::ToggleRecord() {
    if (!ed_.recording) {
        CancelCapture();
        ui.focus = 0;
        engine_.BeginRecord();
        ed_.recording = true;
        ed_.recInfo.clear();
        return;
    }
    FinishRecord();
}

void App::FinishRecord() {
    auto ev = RecorderClean(engine_.EndRecord());
    ed_.recording = false;
    if (!ev.empty()) {
        ed_.steps = RecorderToSteps(ev, cfg_.binds);
        ed_.recInfo = Fmt(L"Recorded %d inputs.", (int)ev.size());
    } else {
        ed_.recInfo = L"Nothing was recorded.";
    }
}

void App::SaveEditor() {
    EditorState& e = ed_;
    if (e.recording) ToggleRecord();
    try {
        ParseSteps(e.steps, cfg_.binds);
        ParseSteps(e.release, cfg_.binds);
    } catch (const FormatError& err) {
        e.saveError = err.msg;
        e.shakeAt = ui.time;
        return;
    }
    Macro fresh;
    Macro* m = e.isNew ? &fresh : FindMacro(e.macroId);
    if (!m) {
        CloseEditor();
        return;
    }
    std::wstring name = Trim(e.name);
    m->name = name.empty() ? L"Macro" : name.substr(0, 60);
    m->trigger = e.trigger;
    m->steps = e.steps;
    m->releaseSteps = e.release;
    m->category = kCategories[e.cat].name;
    m->holdMs = e.hold;
    m->stepDelay = e.delay;
    m->repeatDelay = std::max(1, e.repeatDelay);
    m->repeat = e.repeat;
    m->toggle = e.toggle;
    m->blockTrigger = e.block;
    std::wstring cat = m->category, shown = m->name;
    if (e.isNew) {
        fresh.enabled = true;
        cfg_.macros.push_back(fresh);
    }
    Changed();
    CloseEditor();
    if (e.isNew) {
        if (cat != page_) Navigate(cat);
        ShowToast(L"Created “" + shown + L"”", 1);
    } else {
        ShowToast(cat != page_ ? L"Saved and moved to " + cat : L"Saved “" + shown + L"”", 1);
    }
}

void App::ImportMacro() {
    Prompt(
        L"Import code", L"SHARE OR SETUP CODE", L"",
        [this](const std::wstring& code) {
            try {
                if (IsPackCode(code)) {
                    // MTP1 pack, imports everything
                    MacroPack pack = PackImport(code);
                    int ok = 0;
                    for (auto& m : pack.macros) {
                        try {
                            ParseSteps(m.steps, cfg_.binds);
                            ParseSteps(m.releaseSteps, cfg_.binds);
                        } catch (const FormatError&) {
                            continue;  // skip a macro that doesn't parse against your binds
                        }
                        cfg_.macros.push_back(m);
                        ok++;
                    }
                    if (!ok) {
                        pr_.error = L"None of the macros in this setup could be imported.";
                        return false;
                    }
                    Changed();
                    Navigate(L"Custom");
                    ShowToast(Fmt(ok == 1 ? L"Imported %d macro from the setup. Set keys to use them."
                                         : L"Imported %d macros from the setup. Set keys to use them.",
                                  ok),
                              1);
                    return true;
                }
                Macro m = ShareImport(code);
                ParseSteps(m.steps, cfg_.binds);
                ParseSteps(m.releaseSteps, cfg_.binds);
                std::wstring name = m.name;
                cfg_.macros.push_back(m);
                Changed();
                Navigate(L"Custom");
                ShowToast(L"Imported “" + name + L"”. Set its key to use it.", 1);
                return true;
            } catch (const FormatError& e) {
                pr_.error = e.msg;
                return false;
            }
        },
        L"Import", true, L"MT1:…  or  MTP1:…");
}

void App::DuplicateMacro(int id) {
    for (size_t i = 0; i < cfg_.macros.size(); i++) {
        if (cfg_.macros[i].id != id) continue;
        Macro c = cfg_.macros[i];
        c.id = 0;
        c.name += L" copy";
        c.trigger.clear();
        cfg_.macros.insert(cfg_.macros.begin() + i + 1, c);
        Changed();
        ShowToast(L"Duplicated. Set a key for the copy.", 1);
        return;
    }
}

void App::DeleteMacro(int id) {
    Macro* m = FindMacro(id);
    if (!m) return;
    Confirm(L"Delete macro?", L"“" + m->name + L"” will be removed. This can't be undone.", L"Delete", true,
            [this, id] {
                if (ModalOpen(ModalKind::Editor) && ed_.macroId == id) CloseEditor();
                auto& v = cfg_.macros;
                v.erase(std::remove_if(v.begin(), v.end(), [&](auto& x) { return x.id == id; }), v.end());
                running_.erase(id);
                Changed();
                ShowToast(L"Macro deleted");
            });
}

// ---------------------------------------------------------------- tweaks

void App::LoadTweaks() {
    if (tweaksLoading_) return;
    tweaksLoading_ = true;
    PowerState ps{cfg_.prevPowerScheme, cfg_.maxPowerScheme};
    RunAsync([this, ps]() -> std::function<void()> {
        std::vector<int> st;
        for (auto& t : TweakToggles()) {
            int v = 0;
            try {
                v = t.isOn(ps) ? 1 : 0;
            } catch (...) {
            }
            st.push_back(v);
        }
        return [this, st] {
            for (size_t i = 0; i < st.size() && i < tweakUi_.size(); i++)
                if (!tweakUi_[i].busy) tweakUi_[i].state = st[i];
            tweaksLoading_ = false;
            tweaksLoaded_ = true;
        };
    });
}

void App::SetTweak(size_t i, bool on) {
    tweakUi_[i].busy = true;
    tweakUi_[i].state = on ? 1 : 0;
    PowerState ps{cfg_.prevPowerScheme, cfg_.maxPowerScheme};
    RunAsync([this, i, on, ps]() -> std::function<void()> {
        PowerState p = ps;
        std::wstring err;
        const Tweak& t = TweakToggles()[i];
        try {
            if (on) t.apply(p);
            else t.revert(p);
        } catch (const TweakError& e) {
            err = e.msg;
        } catch (...) {
            err = L"Something went wrong.";
        }
        int now = 0;
        try {
            now = t.isOn(p) ? 1 : 0;
        } catch (...) {
        }
        return [this, i, on, p, err, now] {
            const Tweak& t = TweakToggles()[i];
            tweakUi_[i].busy = false;
            tweakUi_[i].state = now;
            cfg_.prevPowerScheme = p.prev;
            cfg_.maxPowerScheme = p.max;
            if (err.empty()) {
                auto& a = cfg_.appliedTweaks;
                auto it = std::find(a.begin(), a.end(), t.name);
                if (on && it == a.end()) a.push_back(t.name);
                if (!on && it != a.end()) a.erase(it);
                if (t.restart && on) ShowToast(L"Restart Windows for this to take effect.", 2);
            }
            SaveCfg();
            if (!err.empty()) Message(t.name, err, true);
        };
    });
}

void App::RunAction(size_t i) {
    const TweakAction& a = TweakActions()[i];
    auto go = [this, i] {
        actionBusy_[i] = true;
        RunAsync([this, i]() -> std::function<void()> {
            std::wstring res;
            try {
                res = TweakActions()[i].run();
            } catch (const TweakError& e) {
                res = e.msg;
            } catch (...) {
                res = L"Something went wrong.";
            }
            return [this, i, res] {
                actionBusy_[i] = false;
                if (res.size() < 70 && res.find(L'\n') == std::wstring::npos) ShowToast(res, 1);
                else Message(TweakActions()[i].name, res);
            };
        });
    };
    if (a.confirm) Confirm(a.name, a.confirm, a.button, true, go);
    else go();
}

void App::RevertAllTweaks() {
    if (cfg_.appliedTweaks.empty()) {
        Message(L"Nothing to revert", L"No tweaks were turned on from this app.");
        return;
    }
    Confirm(L"Revert all tweaks?", Fmt(L"Undo the %d tweak(s) this app turned on.", (int)cfg_.appliedTweaks.size()),
            L"Revert", true, [this] {
                PowerState ps{cfg_.prevPowerScheme, cfg_.maxPowerScheme};
                std::vector<std::wstring> applied = cfg_.appliedTweaks;
                for (auto& t : tweakUi_) t.busy = true;
                RunAsync([this, ps, applied]() -> std::function<void()> {
                    PowerState p = ps;
                    std::vector<std::wstring> done, errors;
                    for (auto& t : TweakToggles()) {
                        if (std::find(applied.begin(), applied.end(), t.name) == applied.end()) continue;
                        try {
                            t.revert(p);
                            done.push_back(t.name);
                        } catch (const TweakError& e) {
                            errors.push_back(std::wstring(t.name) + L": " + e.msg);
                        } catch (...) {
                            errors.push_back(std::wstring(t.name) + L": failed");
                        }
                    }
                    return [this, p, done, errors] {
                        for (auto& d : done) {
                            auto& a = cfg_.appliedTweaks;
                            a.erase(std::remove(a.begin(), a.end(), d), a.end());
                        }
                        cfg_.prevPowerScheme = p.prev;
                        cfg_.maxPowerScheme = p.max;
                        SaveCfg();
                        for (auto& t : tweakUi_) t.busy = false;
                        tweaksLoaded_ = false;
                        LoadTweaks();
                        if (!errors.empty()) Message(L"Some tweaks couldn't be reverted", Join(errors, L"\n"), true);
                        else ShowToast(Fmt(L"Reverted %d tweak(s).", (int)done.size()), 1);
                    };
                });
            });
}

// ---------------------------------------------------------------- profiles

static std::wstring ProfileNameError(const std::wstring& n) {
    if (n.empty()) return L"Give the profile a name.";
    if (n.size() > 60) return L"Keep the name under 60 characters.";
    for (wchar_t c : n)
        if (c < 32 || wcschr(L"\\/:*?\"<>|", c)) return L"A profile name can't contain \\ / : * ? \" < > |";
    if (n.back() == L'.') return L"A profile name can't end with a dot.";
    std::wstring up = Lower(n);
    static const wchar_t* reserved[] = {L"con", L"prn", L"aux", L"nul", L"com1", L"com2", L"com3", L"com4", L"com5",
                                        L"com6", L"com7", L"com8", L"com9", L"lpt1", L"lpt2", L"lpt3", L"lpt4",
                                        L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9"};
    for (auto r : reserved)
        if (up == r) return L"Windows reserves that name. Pick another.";
    return {};
}

void App::RefreshProfiles() {
    profiles_.clear();
    std::wstring dir = Config::ProfileDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ProfileInfo p;
        p.path = PathJoin(dir, fd.cFileName);
        p.name = FileStem(fd.cFileName);
        Config c;
        int count = Config::LoadFile(p.path, c, nullptr) ? (int)c.macros.size() : -1;
        FILETIME local;
        SYSTEMTIME st;
        FileTimeToLocalFileTime(&fd.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &st);
        wchar_t date[64] = L"", tm[32] = L"";
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"MMM d", date, 64, nullptr);
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, tm, 32);
        p.sub = count < 0 ? L"Can't be read" : Fmt(L"%d macros  ·  saved %s, %s", count, date, tm);
        profiles_.push_back(p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(profiles_.begin(), profiles_.end(), [](auto& a, auto& b) { return Lower(a.name) < Lower(b.name); });
}

void App::SaveProfile(const std::wstring& raw, bool overwriteOk) {
    std::wstring name = Trim(raw);
    std::wstring err = ProfileNameError(name);
    if (!err.empty()) {
        Message(L"Can't save profile", err, true);
        return;
    }
    std::wstring dir = Config::ProfileDir();
    std::wstring path = PathJoin(dir, name + L".json");
    if (FileExists(path) && !overwriteOk) {
        Confirm(L"Overwrite profile?", L"“" + name + L"” already exists. Replace it with your current macros and binds?",
                L"Overwrite", true, [this, name] { SaveProfile(name, true); });
        return;
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    Config p = Config::Defaults();
    p.macros = cfg_.macros;
    p.binds = cfg_.binds;
    if (!p.SaveTo(path)) {
        Message(L"Can't save profile", L"Windows wouldn't let the app write to " + dir + L".", true);
        return;
    }
    RefreshProfiles();
    ShowToast(L"Saved profile “" + name + L"”", 1);
}

void App::LoadProfile(const ProfileInfo& info) {
    std::wstring path = info.path, name = info.name;
    Confirm(L"Load profile?", L"Replace your current macros and binds with “" + name + L"”?", L"Load", false,
            [this, path, name] {
                Config c;
                std::wstring err;
                if (!Config::LoadFile(path, c, &err)) {
                    Message(L"Couldn't load “" + name + L"”", err, true);
                    return;
                }
                cfg_.macros = c.macros;
                cfg_.binds = c.binds;
                for (auto& m : cfg_.macros) m.id = 0;
                cfg_.Normalize();
                running_.clear();
                Changed();
                ShowToast(L"Loaded “" + name + L"”", 1);
            });
}

// ---------------------------------------------------------------- widgets

bool App::KeyCap(ID id, const Rect& r, const std::wstring& label, bool empty, bool capturing, float fire, bool lit,
                 F font) {
    Interact it = ui.Press(id, r);
    float capT = ui.Anim(HashId(id, 1), capturing ? 1.f : 0.f, 14.f);
    float pop = ui.Anim(HashId(id, 0x99), 0.f, 6.f);
    float press = std::max(it.pressT, fire * 0.9f);
    ui.PushScale(1.f + 0.1f * pop, r.Cx(), r.Cy());
    if (empty && !capturing) {
        g.Round(r, 10, WithA(T.panel3, 0.25f + 0.45f * it.hoverT));
        g.RoundStroke(r, 10, Mix(T.border2, T.accent, it.hoverT), 1.3f, true);
        float w = g.TextW(L"Set key", font);
        float x = r.Cx() - (w + 20) / 2;
        g.Text(L"", {x, r.y, 14, r.h}, Mix(T.faint, T.accentHi, it.hoverT), F::IconSm);
        g.Text(L"Set key", {x + 20, r.y, w + 4, r.h}, Mix(T.dim, T.text, it.hoverT), font);
    } else {
        g.Round(r, 10, T.keyEdge);
        Rect face{r.x, r.y + 3 * press, r.w, r.h - 3};
        Color fc = Mix(T.key, Mix(T.key, T.text, 0.08f), it.hoverT);
        fc = Mix(fc, T.accent, std::max(fire * 0.85f, capT * 0.32f));
        g.Round(face, 10, fc);
        g.RoundStroke(face, 10, Mix(WithA(T.border2, 0.6f), WithA(T.accentHi, 0.8f), std::max(fire, it.hoverT * 0.3f)));
        if (capT > 0.01f) {
            float a = 0.5f + 0.5f * (float)std::sin(ui.time * 6.5);
            g.RoundStroke(r.Inset(-3), 13, WithA(T.accentHi, (0.25f + 0.55f * a) * capT), 1.6f);
            ui.Keep();
        }
        g.Text(capturing ? L"Press a key…" : label, face, Mix(T.text, Rgb(0xFFFFFF), std::max(capT, fire * 0.8f)), font, kCenter);
    }
    if (lit) {
        float ph = (float)std::fmod(ui.time, 1.4) / 1.4f;
        g.Circle(r.R() - 3, r.y + 3, 5 + 5 * ph, WithA(T.green, 0.35f * (1 - ph)));
        g.Circle(r.R() - 3, r.y + 3, 4.5f, T.green);
        if (active_) ui.Keep();
    }
    ui.PopXf();
    return it.clicked;
}

// ---------------------------------------------------------------- category page

void App::PageCategory(const Rect& area) {
    bool any = false;
    for (auto& m : cfg_.macros)
        if (m.category == page_) any = true;
    if (!any) {
        float by = EmptyState(area, PageGlyph(page_), L"No macros here yet",
                              L"Create one from scratch, or import a share code from a friend.");
        if (ui.Button(HashId(L"empty-new"), {area.Cx() - 70, by, 140, 40}, L"New macro", Btn::Primary, L""))
            OpenEditor(0);
        return;
    }
    ID sid = HashId(L"scroll-" + page_);
    ui.BeginScroll(sid, {area.x, area.y, area.w + 18, area.h});
    float y = MacroList(page_, area.x, area.y, area.w, 0);
    ui.EndScroll(sid, y - area.y + 16);
}

// macro rows for a category, returns bottom y
float App::MacroList(const std::wstring& category, float x, float y, float w, int firstRow) {
    std::vector<Macro*> list;
    for (auto& m : cfg_.macros)
        if (m.category == category) list.push_back(&m);
    for (size_t i = 0; i < list.size(); i++) {
        float t = RowAppear(firstRow + (int)i);
        ui.PushAlpha(t);
        ui.PushOffset(0, 26 * (1 - t));
        ui.PushScale(0.96f + 0.04f * t, x + w * 0.5f, y + 18);  // unrolls downward
        MacroRow(*list[i], {x, y, w, 76});
        ui.PopXf();  // scale
        ui.PopXf();  // offset
        ui.PopAlpha();
        y += 86;
    }
    return y;
}

void App::MacroRow(Macro& m, const Rect& r) {
    ID id = HashId(L"row", (uint64_t)m.id);
    Interact it = ui.Press(id, r);
    float enT = ui.Anim(HashId(id, 1), m.enabled ? 1.f : 0.f, 12.f);
    float fire = 0;
    if (auto f = flash_.find(m.id); f != flash_.end()) {
        fire = 1.f - Clamp01((float)(ui.time - f->second) / 0.7f);
        if (fire > 0) ui.Keep();
    }
    bool run = running_.count(m.id) > 0;
    auto iss = issues_.find(m.id);
    const Issue* issue = iss == issues_.end() ? nullptr : &iss->second;

    // whole row opens the editor
    float rsp = ui.Spring(HashId(id, 0xB7), it.held ? 1.f : 0.f, 460.f, 28.f);
    ui.PushScale(1.f - 0.014f * rsp, r.Cx(), r.Cy());
    g.Round(r, 12, Mix(T.panel, T.panel2, it.hoverT * 0.9f));
    if (fire > 0) {
        ui.PushClip(r);
        g.Round(r, 12, WithA(T.accent, 0.08f * fire));
        ui.PopClip();
    }
    g.RoundStroke(r, 12, Mix(Mix(T.border, T.border2, it.hoverT), WithA(T.accent, 0.6f), std::max(enT * 0.35f, fire)));
    if (enT > 0.01f) g.Round({r.x, r.y + 22, 3, r.h - 44}, 1.5f, WithA(T.accent, enT));

    std::wstring label = KeyLabel(m.trigger);
    bool capturing = cap_.kind == Capture::MacroKey && cap_.macroId == m.id;
    float kw = std::max(116.f, g.TextW(capturing ? L"Press a key…" : label, F::Semi) + 32);
    Rect kc{r.x + 18, r.y + 17, kw, 42};
    ID chip = HashId(id, 2);
    if (KeyCap(chip, kc, label, m.trigger.empty(), capturing, fire, run)) {
        if (capturing) CancelCapture();
        else StartCapture(Capture::MacroKey, chip, m.id);
    }
    ui.Tip(chip, capturing ? L"Press any key or mouse button. Esc cancels." : L"Click to choose the trigger key", kc);

    Rect more{r.R() - 16 - 32, r.Cy() - 16, 32, 32};
    if (ui.IconButton(HashId(id, 3), more, L"", L"More")) {
        int mid = m.id;
        OpenMenu(ui.ToScreen(more),
                 {{L"Edit…", L"", false, [this, mid] { OpenEditor(mid); }},
                  {L"Duplicate", L"", false, [this, mid] { DuplicateMacro(mid); }},
                  {L"Copy share code", L"", false,
                   [this, mid] {
                       if (Macro* x = FindMacro(mid)) {
                           SetClipboardText(hwnd_, ShareExport(*x));
                           ShowToast(L"Share code copied. Paste it to a friend.", 1);
                       }
                   }},
                  {L"Clear key", L"", false,
                   [this, mid] {
                       if (Macro* x = FindMacro(mid)) {
                           x->trigger.clear();
                           Changed();
                       }
                   }},
                  {L"Delete", L"", true, [this, mid] { DeleteMacro(mid); }}});
    }
    float swx = more.x - 14 - 40;
    bool en = m.enabled;
    if (ui.Switch(HashId(id, 4), swx, r.Cy() - 11, en)) {
        m.enabled = en;
        Changed();
        if (en && m.trigger.empty()) {
            StartCapture(Capture::MacroKey, chip, m.id);
            ShowToast(L"Now press the key that should fire “" + m.name + L"”");
        }
    }

    float tx = swx - 18;
    auto tag = [&](const wchar_t* t, Color c) {
        float w = g.TextW(t, F::Caption) + 18;
        tx -= w;
        ui.Tag({tx, r.Cy() - 11, w, 22}, t, c);
        tx -= 6;
    };
    if (run) tag(L"ACTIVE", T.green);
    if (m.toggle) tag(L"TOGGLE", T.accentHi);
    if (m.repeat) tag(L"REPEAT", T.accent2);
    if (!m.toggle && !Trim(m.releaseSteps).empty()) tag(L"HOLD", T.amber);

    float x0 = kc.R() + 16, tw = tx - x0 - 8;
    g.Text(m.name, {x0, r.y + 14, tw, 22}, T.text, F::H3);
    Rect line{x0, r.y + 39, tw, 20};
    if (issue) {
        Color c = issue->error ? T.red : T.amber;
        g.Text(L"", {x0, line.y, 16, 20}, c, F::IconSm);
        g.Text(issue->text, {x0 + 21, line.y, tw - 21, 20}, c, F::Small);
    } else {
        auto spans = ui.StepSpans(m.steps, cfg_.binds);
        g.Text(m.steps, line, T.faint, F::MonoSmall, kLeft, true, false, spans.data(), spans.size());
    }
    ui.PopXf();  // row press-scale
    if (it.clicked) OpenEditor(m.id);
}

// ---------------------------------------------------------------- tweaks page

void App::PageTweaks(const Rect& area) {
    auto& toggles = TweakToggles();
    auto& actions = TweakActions();
    ID sid = HashId(L"scroll-Tweaks");
    ui.BeginScroll(sid, {area.x, area.y, area.w + 18, area.h});
    float y = area.y;
    int idx = 0;

    float t0 = RowAppear(idx++);
    ui.PushAlpha(t0);
    Rect banner{area.x, y, area.w, 54};
    g.Round(banner, 12, WithA(T.accent, 0.08f));
    g.RoundStroke(banner, 12, WithA(T.accent, 0.24f));
    g.Text(L"", {banner.x + 16, banner.y, 20, banner.h}, T.accentHi, F::Icon);
    g.Text(L"These change Windows settings. Make a restore point first; “Revert all” undoes everything turned on here.",
           {banner.x + 46, banner.y, banner.w - 62, banner.h}, T.text, F::Body, kLeft, true, true);
    ui.PopAlpha();
    y += 74;

    for (auto& grp : kTweakGroups) {
        std::vector<size_t> ti, ai;
        for (size_t i = 0; i < toggles.size(); i++)
            if (std::wstring(toggles[i].group) == grp.name) ti.push_back(i);
        for (size_t i = 0; i < actions.size(); i++)
            if (std::wstring(actions[i].group) == grp.name) ai.push_back(i);
        if (ti.empty() && ai.empty()) continue;
        float t = RowAppear(idx++);
        ui.PushAlpha(t);
        ui.PushOffset(0, 16 * (1 - t));
        g.Text(grp.glyph, {area.x + 2, y, 22, 24}, T.accentHi, F::Icon, kCenter);
        g.Text(grp.name, {area.x + 32, y, 300, 24}, T.text, F::H3);
        y += 34;
        size_t n = ti.size() + ai.size();
        Rect card{area.x, y, area.w, n * 70.f};
        g.Round(card, 14, T.panel);
        g.RoundStroke(card, 14, T.border);
        float ry = y;
        size_t k = 0;
        auto rowText = [&](const Rect& row, const wchar_t* name, const wchar_t* info, bool restart) {
            float textW = row.w - 170;
            g.Text(name, {row.x + 20, row.y + 15, textW, 20}, T.text, F::Semi);
            if (restart) {
                float nw = g.TextW(name, F::Semi);
                float w = g.TextW(L"RESTART", F::Caption) + 18;
                ui.Tag({row.x + 20 + nw + 10, row.y + 14, w, 20}, L"RESTART", T.amber);
            }
            g.Text(info, {row.x + 20, row.y + 38, textW, 18}, T.dim, F::Small);
        };
        for (size_t i : ti) {
            Rect row{area.x, ry, area.w, 70};
            if (k++) g.Fill({row.x + 18, row.y, row.w - 36, 1}, T.border);
            rowText(row, toggles[i].name, toggles[i].info, toggles[i].restart);
            TweakUi& st = tweakUi_[i];
            if (st.state < 0) {
                ui.Skeleton({row.R() - 20 - 40, row.Cy() - 11, 40, 22}, 11);
            } else {
                bool v = st.state == 1;
                if (ui.Switch(HashId(L"tw", (uint64_t)i), row.R() - 20 - 40, row.Cy() - 11, v, true, st.busy))
                    SetTweak(i, v);
            }
            ry += 70;
        }
        for (size_t i : ai) {
            Rect row{area.x, ry, area.w, 70};
            if (k++) g.Fill({row.x + 18, row.y, row.w - 36, 1}, T.border);
            rowText(row, actions[i].name, actions[i].info, false);
            Rect b{row.R() - 20 - 104, row.Cy() - 18, 104, 36};
            bool busy = actionBusy_[i];
            if (ui.Button(HashId(L"ta", (uint64_t)i), b, busy ? L"" : actions[i].button, Btn::Secondary, nullptr, !busy))
                RunAction(i);
            if (busy) ui.Spinner(b.Cx(), b.Cy(), 8, T.accentHi);
            ry += 70;
        }
        y = card.B() + 28;
        ui.PopXf();
        ui.PopAlpha();
    }
    ui.EndScroll(sid, y - area.y);
}

// ---------------------------------------------------------------- tester page

void App::PageTester(const Rect& area) {
    Rect top{area.x, area.y, area.w, 72};
    g.Round(top, 14, T.panel);
    g.RoundStroke(top, 14, Input::dryRun ? WithA(T.amber, 0.45f) : T.border);
    bool dry = Input::dryRun;
    if (ui.Switch(HashId(L"dry"), top.x + 20, top.Cy() - 11, dry)) Input::dryRun = dry;
    g.Text(L"Dry run", {top.x + 76, top.y + 15, 300, 20}, T.text, F::Semi);
    g.Text(L"Macros only show up here, nothing is sent to the game or other apps.", {top.x + 76, top.y + 38, top.w - 400, 18},
           T.dim, F::Small);
    std::wstring tl = TimingLabel();
    float cw = g.TextW(tl, F::Small) + 46;
    Rect chip{top.R() - 20 - cw, top.Cy() - 15, cw, 30};
    g.Round(chip, 15, T.panel3);
    g.Text(L"", {chip.x + 12, chip.y, 16, chip.h}, T.accent2, F::IconSm);
    g.Text(tl, {chip.x + 34, chip.y, cw - 40, chip.h}, T.text, F::Small);

    Rect list{area.x, top.B() + 18, area.w, area.B() - top.B() - 18};
    if (runs_.empty()) {
        EmptyState(list, L"", L"Waiting for a macro",
                   L"Turn a macro on, then press its key. Every input it sends shows up here with exact timing.");
        return;
    }
    ID sid = HashId(L"scroll-Tester");
    ui.BeginScroll(sid, {list.x, list.y, list.w + 18, list.h});
    float y = list.y;
    for (auto& run : runs_) {
        int shown = std::min<int>((int)run.ev.size(), 12);
        float h = 64 + 40 + shown * 21.f + (run.ev.size() > 12 ? 24.f : 0.f) + 12;
        float t = ui.Anim(run.id, 1.f, 12.f, 0.f);
        ui.PushAlpha(t);
        ui.PushOffset(0, -14 * (1 - t));
        Rect card{list.x, y, list.w, h};
        g.Round(card, 14, T.panel);
        g.RoundStroke(card, 14, Mix(WithA(T.accent, 0.6f), T.border, t));
        g.Circle(card.x + 34, card.y + 32, 15, WithA(T.accent, 0.18f));
        g.Text(L"", {card.x + 19, card.y + 17, 30, 30}, T.accentHi, F::IconSm, kCenter);
        g.Text(run.name, {card.x + 60, card.y + 21, card.w - 320, 22}, T.text, F::H3);
        double total = run.ev.empty() ? 0 : QpcToMs(run.ev.back().qpc - run.qpc0);
        std::wstring meta = Fmt(L"%d inputs  ·  %.1f ms", (int)run.ev.size(), total);
        g.Text(meta, {card.R() - 20 - 260, card.y + 21, 260, 22}, T.dim, F::Small, kRight);

        // timeline: ticks for every input, bars show how long keys stayed down
        Rect tl{card.x + 20, card.y + 60, card.w - 40, 26};
        g.Round(tl, 8, T.field);
        float span = (float)std::max(1.0, total);
        auto xAt = [&](long long q) { return tl.x + 10 + (tl.w - 20) * (float)(QpcToMs(q - run.qpc0) / span); };
        std::unordered_map<int, float> downAt;
        for (auto& e : run.ev) {
            float x = xAt(e.qpc);
            if (e.key == kMouseMove) {
                continue;
            } else if (e.down) {
                downAt[e.key] = x;
            } else if (auto d = downAt.find(e.key); d != downAt.end()) {
                g.Round({d->second, tl.Cy() - 3, std::max(2.f, x - d->second), 6}, 3, WithA(T.accent2, 0.35f));
                downAt.erase(d);
            }
        }
        for (auto& e : run.ev) {
            float x = xAt(e.qpc);
            if (e.key == kMouseMove) g.Circle(x, tl.Cy(), 3.5f, T.pink);
            else if (e.down) g.Fill({x - 1, tl.y + 5, 2, tl.h - 10}, T.accentHi);
            else g.Fill({x - 0.5f, tl.y + 9, 1, tl.h - 18}, T.dim);
        }

        float ly = tl.B() + 12;
        for (int i = 0; i < shown; i++) {
            auto& e = run.ev[i];
            double ms = QpcToMs(e.qpc - run.qpc0);
            g.Text(Fmt(L"+%.1f ms", ms), {card.x + 22, ly, 92, 20}, T.faint, F::MonoSmall, kRight);
            if (e.key == kMouseMove) {
                g.Text(L"Mouse move", {card.x + 132, ly, 160, 20}, T.pink, F::MonoSmall);
                g.Text(Fmt(L"%+d, %+d", e.dx, e.dy), {card.x + 300, ly, 200, 20}, T.pink, F::MonoSmall);
            } else {
                g.Text(Pretty(KeyName(e.key)), {card.x + 132, ly, 160, 20}, T.text, F::MonoSmall);
                g.Text(e.down ? L"↓ down" : L"↑ up", {card.x + 300, ly, 100, 20}, e.down ? T.accent2 : T.dim,
                       F::MonoSmall);
            }
            ly += 21;
        }
        if (run.ev.size() > 12)
            g.Text(Fmt(L"+ %d more", (int)run.ev.size() - 12), {card.x + 22, ly + 2, 200, 20}, T.faint, F::Small);
        ui.PopXf();
        ui.PopAlpha();
        y += h + 12;
    }
    ui.EndScroll(sid, y - list.y);
}

// ---------------------------------------------------------------- profiles page

void App::PageProfiles(const Rect& area) {
    if (profiles_.empty()) {
        float by = EmptyState(area, L"", L"No profiles yet",
                              L"Save your current macros and binds as a profile, then switch between setups in one click.");
        if (ui.Button(HashId(L"empty-save"), {area.Cx() - 158, by, 152, 40}, L"Save current", Btn::Primary, L"")) {
            Prompt(L"Save profile", L"PROFILE NAME", L"", [this](const std::wstring& n) {
                SaveProfile(n, false);
                return true;
            });
        }
        if (ui.Button(HashId(L"empty-import"), {area.Cx() + 6, by, 152, 40}, L"Import code", Btn::Secondary, L""))
            ImportMacro();
        return;
    }
    ID sid = HashId(L"scroll-Profiles");
    // setup code header
    {
        Rect hdr{area.x, area.y, area.w, 40};
        if (ui.Button(HashId(L"prof-import"), {hdr.R() - 142, hdr.y, 142, 40}, L"Import code", Btn::Primary, L""))
            ImportMacro();
        if (ui.Button(HashId(L"prof-copy"), {hdr.R() - 142 - 10 - 150, hdr.y, 150, 40}, L"Copy my setup", Btn::Secondary,
                      L"")) {
            SetClipboardText(hwnd_, PackExport(cfg_.macros, cfg_.binds));
            ShowToast(Fmt(L"Setup code copied — %d macros. Paste it to a friend.", (int)cfg_.macros.size()), 1);
        }
        g.Text(L"Share or switch whole setups", {hdr.x, hdr.y + 10, hdr.w - 320, 22}, T.dim, F::Body);
    }
    float listTop = area.y + 56;
    ui.BeginScroll(sid, {area.x, listTop, area.w + 18, area.h - 56});
    float y = listTop;
    for (size_t i = 0; i < profiles_.size(); i++) {
        ProfileInfo p = profiles_[i];
        float t = RowAppear((int)i);
        ui.PushAlpha(t);
        ui.PushOffset(0, 16 * (1 - t));
        Rect r{area.x, y, area.w, 72};
        ID id = HashId(L"prof-" + p.name);
        Interact it = ui.Press(id, r);
        g.Round(r, 14, Mix(T.panel, T.panel2, it.hoverT));
        g.RoundStroke(r, 14, Mix(T.border, T.border2, it.hoverT));
        Rect tile{r.x + 16, r.y + 14, 44, 44};
        g.Round(tile, 22, T.panel3);
        g.Text(p.name.substr(0, 1), tile, T.text, F::H2, kCenter);
        g.Text(p.name, {tile.R() + 16, r.y + 15, r.w - 330, 22}, T.text, F::H3);
        g.Text(p.sub, {tile.R() + 16, r.y + 39, r.w - 330, 18}, T.dim, F::Small);
        Rect more{r.R() - 16 - 32, r.Cy() - 16, 32, 32};
        if (ui.IconButton(HashId(id, 1), more, L"", L"More")) {
            OpenMenu(ui.ToScreen(more),
                     {{L"Overwrite with current", L"", false, [this, p] { SaveProfile(p.name, false); }},
                      {L"Copy as share code", L"", false,
                       [this, p] {
                           Config pc;
                           std::wstring err;
                           if (Config::LoadFile(p.path, pc, &err)) {
                               SetClipboardText(hwnd_, PackExport(pc.macros, pc.binds));
                               ShowToast(Fmt(L"Setup code copied — %d macros.", (int)pc.macros.size()), 1);
                           } else {
                               ShowToast(L"Couldn't read that profile.", 2);
                           }
                       }},
                      {L"Show in folder", L"", false,
                       [p] {
                           std::wstring args = L"/select,\"" + p.path + L"\"";
                           ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                       }},
                      {L"Delete", L"", true, [this, p] {
                           Confirm(L"Delete profile?", L"“" + p.name + L"” will be deleted.", L"Delete", true,
                                   [this, p] {
                                       DeleteFileW(p.path.c_str());
                                       RefreshProfiles();
                                       ShowToast(L"Profile deleted");
                                   });
                       }}});
        }
        if (ui.Button(HashId(id, 2), {more.x - 10 - 96, r.Cy() - 18, 96, 36}, L"Load", Btn::Secondary, L"")) LoadProfile(p);
        ui.PopXf();
        ui.PopAlpha();
        y += 82;
    }
    ui.EndScroll(sid, y - listTop + 8);
}

// ---------------------------------------------------------------- settings page

void App::PageSettings(const Rect& area) {
    ID sid = HashId(L"scroll-Settings");
    ui.BeginScroll(sid, {area.x, area.y, area.w + 18, area.h});
    float y = area.y;
    int idx = 0;
    auto section = [&](const wchar_t* title) {
        Caption(title, area.x + 4, y, T.faint);
        y += 22;
    };

    // timing
    float t = RowAppear(idx++);
    ui.PushAlpha(t);
    ui.PushOffset(0, 16 * (1 - t));
    section(L"TIMING");
    Rect card{area.x, y, area.w, 150};
    g.Round(card, 14, T.panel);
    g.RoundStroke(card, 14, T.border);
    int sel = cfg_.timing == L"FrameHuman" ? 0 : 1;
    if (ui.Segmented(HashId(L"timing"), {card.x + 18, card.y + 18, card.w - 36, 42},
                     {L"Frame better", L"Frame legacy"}, sel)) {
        cfg_.timing = sel == 0 ? L"FrameHuman" : L"Frame";
        Changed();
    }
    g.Text(sel == 0 ? L"Frame sync with tiny random timing, so inputs look human and taps always register."
                    : L"Plain frame sync: one frame per step, exactly the same every time.",
           {card.x + 20, card.y + 76, card.w - 40, 20}, T.text, F::Body);
    g.Text(L"", {card.x + 20, card.y + 106, 16, 22}, T.accent2, F::IconSm);
    g.Text(Monitor::Describe(cfg_.timing), {card.x + 42, card.y + 106, card.w - 62, 22}, T.dim, F::Small);
    y = card.B() + 30;
    ui.PopXf();
    ui.PopAlpha();

    // general
    t = RowAppear(idx++);
    ui.PushAlpha(t);
    ui.PushOffset(0, 16 * (1 - t));
    section(L"GENERAL");
    Rect gen{area.x, y, area.w, 8 * 70.f};
    g.Round(gen, 14, T.panel);
    g.RoundStroke(gen, 14, T.border);
    auto row = [&](int i, const wchar_t* title, const std::wstring& desc) {
        Rect r{gen.x, gen.y + i * 70.f, gen.w, 70};
        if (i) g.Fill({r.x + 18, r.y, r.w - 36, 1}, T.border);
        g.Text(title, {r.x + 20, r.y + 15, r.w - 260, 20}, T.text, F::Semi);
        g.Text(desc, {r.x + 20, r.y + 38, r.w - 260, 18}, T.dim, F::Small);
        return r;
    };
    Rect r0 = row(0, L"On / off key", L"Turns every macro on or off, even while you're in game.");
    {
        bool capturing = cap_.kind == Capture::ToggleKey;
        std::wstring lbl = Pretty(cfg_.toggleKey);
        float kw = std::max(84.f, g.TextW(capturing ? L"Press a key…" : lbl, F::Semi) + 32);
        ID chip = HashId(L"toggle-chip");
        if (KeyCap(chip, {r0.R() - 20 - kw, r0.Cy() - 20, kw, 40}, lbl, false, capturing, 0, false)) {
            if (capturing) CancelCapture();
            else StartCapture(Capture::ToggleKey, chip);
        }
    }
    Rect r1 = row(1, L"Toggle sound", L"Beeps high when macros turn on, low when they turn off.");
    bool snd = cfg_.toggleSound;
    if (ui.Switch(HashId(L"sound"), r1.R() - 20 - 40, r1.Cy() - 11, snd)) {
        cfg_.toggleSound = snd;
        Changed();
    }
    Rect r2 = row(2, L"High priority", L"Windows runs the macros first, so timings stay tight.");
    bool hp = cfg_.highPriority;
    if (ui.Switch(HashId(L"prio"), r2.R() - 20 - 40, r2.Cy() - 11, hp)) {
        cfg_.highPriority = hp;
        if (!preview_) SetPriorityClass(GetCurrentProcess(), hp ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
        Changed();
    }
    Rect r4 = row(3, L"Only in Fortnite", L"Macros fire only while Fortnite is the active window. Nothing on your desktop.");
    bool oig = cfg_.onlyInGame;
    if (ui.Switch(HashId(L"onlyingame"), r4.R() - 20 - 40, r4.Cy() - 11, oig)) {
        cfg_.onlyInGame = oig;
        Changed();
    }
    Rect r5 = row(4, L"In-game popup", L"A small “Macros ON/OFF” appears when you press the key. Needs borderless/windowed.");
    bool od = cfg_.osd;
    if (ui.Switch(HashId(L"osd"), r5.R() - 20 - 40, r5.Cy() - 11, od)) {
        cfg_.osd = od;
        Osd::SetEnabled(od);
        if (od) Osd::Flash(cfg_.enabled);  // quick preview
        SaveCfg();
    }
    Rect r6 = row(5, L"Animations",
                  L"Smooth UI motion. Keep on even if Windows has animations turned off.");
    bool anim = cfg_.animations;
    if (ui.Switch(HashId(L"animations"), r6.R() - 20 - 40, r6.Cy() - 11, anim)) {
        cfg_.animations = anim;
        ui.reduceMotion = !anim;
        Changed();
    }
    Rect r7 = row(6, L"In-game menu",
                  Pretty(cfg_.menuKey) + L" opens a clickable menu over the game to switch macros. Needs borderless/windowed.");
    bool mn = cfg_.menu;
    if (ui.Switch(HashId(L"menu"), r7.R() - 20 - 40, r7.Cy() - 11, mn)) {
        cfg_.menu = mn;
        if (!mn) Menu::Hide();
        Changed();
    }
    Rect r8 = row(7, L"Edit / reset on release",
                  L"Turn on if Fortnite has Edit and Reset on release on. Edit macros skip the confirm press.");
    bool eor = cfg_.editOnRelease;
    if (ui.Switch(HashId(L"editonrelease"), r8.R() - 20 - 40, r8.Cy() - 11, eor)) {
        cfg_.editOnRelease = eor;
        Changed();
    }

    y = gen.B() + 30;
    ui.PopXf();
    ui.PopAlpha();

    // edit fix
    t = RowAppear(idx++);
    ui.PushAlpha(t);
    ui.PushOffset(0, 16 * (1 - t));
    section(L"EDIT FIX");
    Rect ef{area.x, y, area.w, 3 * 70.f};
    g.Round(ef, 14, T.panel);
    g.RoundStroke(ef, 14, T.border);
    auto efrow = [&](int i, const wchar_t* title, const std::wstring& desc) {
        Rect r{ef.x, ef.y + i * 70.f, ef.w, 70};
        if (i) g.Fill({r.x + 18, r.y, r.w - 36, 1}, T.border);
        g.Text(title, {r.x + 20, r.y + 15, r.w - 160, 20}, T.text, F::Semi);
        g.Text(desc, {r.x + 20, r.y + 38, r.w - 160, 18}, T.dim, F::Small);
        return r;
    };
    Rect eg = efrow(0, L"Auto-reset stuck edit",
                    L"After an edit/build macro, if you're still in the blue edit overlay it taps your reset key.");
    bool egOn = cfg_.editGuard;
    if (ui.Switch(HashId(L"editguard"), eg.R() - 20 - 40, eg.Cy() - 11, egOn)) {
        cfg_.editGuard = egOn;
        Changed();
    }
    {
        Rect er = efrow(1, L"Reset key", L"The key that drops you out of edit mode. Needs borderless/windowed.");
        ui.PushAlpha(cfg_.editGuard ? 1.f : 0.45f);
        bool capturing = cap_.kind == Capture::ResetKey;
        std::wstring lbl = Pretty(cfg_.editResetKey);
        float kw = std::max(84.f, g.TextW(capturing ? L"Press a key…" : lbl, F::Semi) + 32);
        ID chip = HashId(L"reset-chip");
        if (KeyCap(chip, {er.R() - 20 - kw, er.Cy() - 20, kw, 40}, lbl, cfg_.editResetKey.empty(), capturing, 0,
                   false) &&
            cfg_.editGuard) {
            if (capturing) CancelCapture();
            else StartCapture(Capture::ResetKey, chip);
        }
        ui.PopAlpha();
    }
    Rect ek = efrow(2, L"Key check",
                    L"Spaces out rapid edit/build spam so the game keeps up and it can't jam in edit mode.");
    bool ekOn = cfg_.editKeyCheck;
    if (ui.Switch(HashId(L"editkeycheck"), ek.R() - 20 - 40, ek.Cy() - 11, ekOn)) {
        cfg_.editKeyCheck = ekOn;
        Changed();
    }
    y = ef.B() + 30;
    ui.PopXf();
    ui.PopAlpha();

    // binds
    t = RowAppear(idx++);
    ui.PushAlpha(t);
    ui.PushOffset(0, 16 * (1 - t));
    section(L"GAME KEYBINDS");
    g.Text(L"Set these to the keys you use in game. Macros refer to them as {Edit}, {Wall} and so on.",
           {area.x + 4, y - 4, area.w, 20}, T.dim, F::Small);
    y += 24;
    int cols = area.w > 780 ? 3 : 2;
    float gap = 12, tw = (area.w - gap * (cols - 1)) / cols, th = 62;
    for (size_t i = 0; i < cfg_.binds.size(); i++) {
        auto& [name, key] = cfg_.binds[i];
        Rect tile{area.x + (i % cols) * (tw + gap), y + (i / cols) * (th + gap), tw, th};
        ID tid = HashId(L"bind-" + name);
        Interact it = ui.Press(tid, tile);
        g.Round(tile, 12, Mix(T.panel, T.panel2, it.hoverT));
        g.RoundStroke(tile, 12, T.border);
        g.Text(name, {tile.x + 16, tile.y + 12, tw - 130, 20}, T.text, F::Semi);
        g.Text(L"{" + name + L"}", {tile.x + 16, tile.y + 33, tw - 130, 18}, T.accentHi, F::MonoSmall);
        bool capturing = cap_.kind == Capture::BindKey && cap_.bind == name;
        std::wstring lbl = Pretty(key);
        float kw = std::max(70.f, g.TextW(capturing ? L"Press a key…" : lbl, F::Semi) + 28);
        ID chip = HashId(tid, 1);
        if (KeyCap(chip, {tile.R() - 14 - kw, tile.Cy() - 18, kw, 36}, lbl, key.empty(), capturing, 0, false)) {
            if (capturing) CancelCapture();
            else StartCapture(Capture::BindKey, chip, 0, name);
        }
    }
    y += ((cfg_.binds.size() + cols - 1) / cols) * (th + gap) + 18;
    ui.PopXf();
    ui.PopAlpha();

    // about (free build)
    t = RowAppear(idx++);
    ui.PushAlpha(t);
    ui.PushOffset(0, 16 * (1 - t));
    section(L"ABOUT");
    {
        Rect lc{area.x, y, area.w, 70};
        g.Round(lc, 12, T.panel);
        g.RoundStroke(lc, 12, T.border);
        g.Text(L"Free release", {lc.x + 20, lc.y + 15, lc.w - 40, 20}, T.text, F::Semi);
        g.Text(L"Open source, no key needed. Use it however you like.", {lc.x + 20, lc.y + 38, lc.w - 40, 18}, T.dim, F::Small);
        y = lc.B() + 30;
    }
    ui.PopXf();
    ui.PopAlpha();

    // data
    t = RowAppear(idx++);
    ui.PushAlpha(t);
    ui.PushOffset(0, 16 * (1 - t));
    section(L"DATA");
    Rect data{area.x, y, area.w, 210};
    g.Round(data, 14, T.panel);
    g.RoundStroke(data, 14, T.border);
    g.Text(L"Config file", {data.x + 20, data.y + 15, data.w - 260, 20}, T.text, F::Semi);
    g.Text(Config::FilePath(), {data.x + 20, data.y + 38, data.w - 260, 18}, T.dim, F::MonoSmall);
    if (ui.Button(HashId(L"openfolder"), {data.R() - 20 - 134, data.y + 17, 134, 36}, L"Open folder", Btn::Secondary, L"")) {
        std::wstring args = L"/select,\"" + Config::FilePath() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
    g.Fill({data.x + 18, data.y + 70, data.w - 36, 1}, T.border);
    g.Text(L"Walkthrough", {data.x + 20, data.y + 85, data.w - 260, 20}, T.text, F::Semi);
    g.Text(L"Replay the first-run setup guide.", {data.x + 20, data.y + 108, data.w - 260, 18}, T.dim, F::Small);
    if (ui.Button(HashId(L"replaytut"), {data.R() - 20 - 134, data.y + 87, 134, 36}, L"Show again", Btn::Secondary)) {
        cfg_.tutorialDone = false;
        SaveCfg();
        Navigate(L"Dashboard");
    }
    g.Fill({data.x + 18, data.y + 140, data.w - 36, 1}, T.border);
    g.Text(L"Reset to defaults", {data.x + 20, data.y + 155, data.w - 260, 20}, T.text, F::Semi);
    g.Text(L"Restores the built-in macros, binds and the F8 on/off key.", {data.x + 20, data.y + 178, data.w - 260, 18}, T.dim,
           F::Small);
    if (ui.Button(HashId(L"reset"), {data.R() - 20 - 134, data.y + 157, 134, 36}, L"Reset", Btn::Danger, L"")) {
        Confirm(L"Reset everything?", L"All macros and binds go back to the defaults. Your custom macros will be deleted.",
                L"Reset", true, [this] {
                    Config d = Config::Defaults();
                    cfg_.macros = d.macros;
                    cfg_.binds = d.binds;
                    cfg_.toggleKey = d.toggleKey;
                    for (auto& m : cfg_.macros) m.id = 0;
                    running_.clear();
                    Changed();
                    ShowToast(L"Back to defaults", 1);
                });
    }
    y = data.B() + 24;
    g.Text(L"Macro Tool · native C++ edition", {area.x, y, area.w, 18}, T.faint, F::Small, kCenter);
    y += 36;
    ui.PopXf();
    ui.PopAlpha();
    ui.EndScroll(sid, y - area.y);
}

// ---------------------------------------------------------------- editor

void App::DrawEditor(const Rect& box, bool input) {
    EditorState& e = ed_;
    float shake = 0;
    if (ui.time - e.shakeAt < 0.45) {
        float k = (float)(ui.time - e.shakeAt) / 0.45f;
        shake = std::sin(k * 32.f) * 9.f * (1 - k);
        ui.Keep();
    }
    ui.PushOffset(shake, 0);

    Rect tile{box.x + 24, box.y + 18, 40, 40};
    g.Round(tile, 20, T.panel3);
    g.Text(e.isNew ? L"" : L"", tile, T.text, F::Icon, kCenter);
    g.Text(e.isNew ? L"New macro" : L"Edit macro", {tile.R() + 14, box.y + 16, box.w - 200, 26}, T.text, F::H2);
    g.Text(Trim(e.name).empty() ? L"Untitled" : e.name, {tile.R() + 15, box.y + 42, box.w - 200, 18}, T.dim, F::Small);
    if (ui.IconButton(HashId(L"ed-close"), {box.R() - 20 - 32, box.y + 21, 32, 32}, L"", L"Close (Esc)")) CloseEditor();
    g.Fill({box.x, box.y + 74, box.w, 1}, T.border);

    Rect foot{box.x, box.B() - 74, box.w, 74};
    Rect view{box.x, box.y + 75, box.w, foot.y - box.y - 75};
    float cx = box.x + 28, cw = box.w - 56;
    ID sid = HashId(L"ed-scroll");
    ui.BeginScroll(sid, view);
    float y = view.y + 22;

    Caption(L"NAME", cx, y, T.faint);
    y += 20;
    TextBoxOpts nameOpts;
    nameOpts.placeholder = L"e.g. Quick Edit";
    nameOpts.maxLen = 60;
    ui.TextBox(HashId(L"ed-name"), {cx, y, cw, 40}, e.name, nameOpts);
    y += 58;

    Caption(L"CATEGORY", cx, y, T.faint);
    y += 20;
    std::vector<std::wstring> cats;
    for (auto& c : kCategories) cats.push_back(c.name);
    ui.Segmented(HashId(L"ed-cat"), {cx, y, cw, 40}, cats, e.cat);
    y += 58;

    Caption(L"TRIGGER KEY", cx, y, T.faint);
    y += 20;
    {
        bool capturing = cap_.kind == Capture::EditorKey;
        std::wstring lbl = KeyLabel(e.trigger);
        float kw = std::max(100.f, g.TextW(capturing ? L"Press a key…" : lbl, F::Semi) + 36);
        ID chip = HashId(L"ed-chip");
        if (KeyCap(chip, {cx, y, kw, 42}, lbl, e.trigger.empty(), capturing, 0, false)) {
            if (capturing) CancelCapture();
            else StartCapture(Capture::EditorKey, chip);
        }
        float hx = cx + kw + 12;
        if (!e.trigger.empty() && !capturing) {
            if (ui.Button(HashId(L"ed-clear"), {hx, y + 4, 72, 34}, L"Clear", Btn::Ghost)) e.trigger.clear();
            hx += 82;
        }
        std::wstring hint = capturing ? L"Press any key or mouse button. Esc cancels."
                                      : L"Click the key, then press what should fire this macro.";
        Color hc = T.dim;
        int k = 0, tk = VK_F8;
        KeyParse(cfg_.toggleKey, tk);
        if (!capturing && KeyParse(e.trigger, k)) {
            if (k == tk) {
                hint = L"That's the on/off key, so this macro would never fire.";
                hc = T.amber;
            } else {
                for (auto& m : cfg_.macros)
                    if (m.id != e.macroId && m.enabled && IEquals(m.trigger, e.trigger)) {
                        hint = L"“" + m.name + L"” uses this key too. Both will fire.";
                        hc = T.amber;
                        break;
                    }
            }
        }
        g.Text(hint, {hx, y, box.R() - 28 - hx, 42}, hc, F::Small, kLeft, true, true);
    }
    y += 64;

    // steps on press, with recording
    Caption(L"ON PRESS", cx, y, T.faint);
    Rect rb{cx + cw - 118, y - 9, 118, 30};
    if (e.recording) {
        if (ui.Button(HashId(L"ed-rec"), rb, L"Stop", Btn::Record, L"", true, true)) ToggleRecord();
    } else {
        if (ui.Button(HashId(L"ed-rec"), rb, L"Record", Btn::Secondary, L"")) ToggleRecord();
    }
    y += 22;
    // quick insert chips
    {
        auto insert = [&](const std::wstring& tok) {
            std::wstring t = Trim(e.steps);
            if (t.empty()) e.steps = tok;
            else if (t.back() == L',') e.steps = t + L" " + tok;
            else e.steps = t + L", " + tok;
            e.saveError.clear();
            e.recInfo.clear();
        };
        struct Chip {
            const wchar_t* tok;
            int kind;  // 0 bind, 1 key, 2 up/down, 3 wait
        };
        const Chip chips[] = {{L"{Edit}", 0}, {L"{Wall}", 0},   {L"{Floor}", 0}, {L"{Stair}", 0}, {L"{Cone}", 0},
                              {L"LButton", 1}, {L"RButton", 1}, {L"down", 2},    {L"up", 2},      {L"wait 50", 3}};
        float chx = cx, chy = y;
        int idx = 0;
        for (auto& c : chips) {
            std::wstring tok = c.tok;
            Color col = c.kind == 0 ? T.accentHi : c.kind == 2 ? T.accent2 : c.kind == 3 ? T.amber : T.text;
            float w = g.TextW(tok, F::MonoSmall) + 22;
            if (chx + w > cx + cw) {
                chx = cx;
                chy += 32;
            }
            Rect cr{chx, chy, w, 27};
            ID id = HashId(L"ed-ins", (uint64_t)idx++);
            Interact it = ui.Press(id, cr);
            float sp = ui.Spring(HashId(id, 0xB7), it.held ? 1.f : 0.f, 460.f, 26.f);
            ui.PushScale(1.f - 0.06f * sp, cr.Cx(), cr.Cy());
            g.Round(cr, 8, Mix(T.panel2, T.panel3, it.hoverT));
            g.RoundStroke(cr, 8, Mix(T.border, col, 0.45f * it.hoverT));
            g.Text(tok, cr, Mix(T.dim, col, 0.55f + 0.45f * it.hoverT), F::MonoSmall, kCenter);
            ui.PopXf();
            if (it.clicked) insert(tok);
            chx += w + 8;
        }
        y = chy + 38;
    }
    std::wstring pressErr, releaseErr;
    int pressCount = 0;
    try {
        pressCount = (int)ParseSteps(e.steps, cfg_.binds).size();
    } catch (const FormatError& err) {
        pressErr = err.msg;
    }
    try {
        ParseSteps(e.release, cfg_.binds);
    } catch (const FormatError& err) {
        releaseErr = err.msg;
    }
    TextBoxOpts so;
    so.multiline = true;
    so.mono = true;
    so.placeholder = L"{Edit}, LButton, {Edit}";
    so.colorize = &cfg_.binds;
    so.error = !pressErr.empty();
    auto r1 = ui.TextBox(HashId(L"ed-steps"), {cx, y, cw, 100}, e.steps, so);
    if (r1.changed) {
        e.saveError.clear();
        e.recInfo.clear();
    }
    y += 108;
    {
        Rect st{cx, y, cw, 20};
        if (e.recording) {
            float a = 0.5f + 0.5f * (float)std::sin(ui.time * 6);
            g.Circle(st.x + 6, st.Cy(), 5, WithA(T.red, 0.5f + 0.5f * a));
            g.Text(Fmt(L"Recording… %d inputs so far. Press Esc or click Stop when you're done.", (int)engine_.RecordCount()),
                   {st.x + 18, st.y, st.w - 18, st.h}, T.red, F::Small);
            ui.Keep();
        } else if (!pressErr.empty()) {
            g.Text(L"", {st.x, st.y, 16, st.h}, T.red, F::IconSm);
            g.Text(pressErr, {st.x + 20, st.y, st.w - 20, st.h}, T.red, F::Small);
        } else if (!e.recInfo.empty()) {
            g.Text(L"", {st.x, st.y, 16, st.h}, T.accent2, F::IconSm);
            g.Text(e.recInfo, {st.x + 20, st.y, st.w - 20, st.h}, T.dim, F::Small);
        } else if (pressCount > 0) {
            g.Text(L"", {st.x, st.y, 16, st.h}, T.green, F::IconSm);
            g.Text(Fmt(pressCount == 1 ? L"%d step" : L"%d steps", pressCount), {st.x + 20, st.y, st.w - 20, st.h}, T.dim,
                   F::Small);
        } else {
            g.Text(L"Separate steps with commas. Ctrl+Enter saves.", st, T.faint, F::Small);
        }
    }
    y += 34;

    Caption(L"ON RELEASE  ·  OPTIONAL", cx, y, T.faint);
    y += 20;
    TextBoxOpts ro = so;
    ro.placeholder = L"e.g. LButton up";
    ro.error = !releaseErr.empty();
    ui.TextBox(HashId(L"ed-release"), {cx, y, cw, 66}, e.release, ro);
    y += 74;
    {
        Rect st{cx, y, cw, 20};
        if (!releaseErr.empty()) {
            g.Text(L"", {st.x, st.y, 16, st.h}, T.red, F::IconSm);
            g.Text(releaseErr, {st.x + 20, st.y, st.w - 20, st.h}, T.red, F::Small);
        } else {
            g.Text(L"Runs when you let go of the key, or press it again in toggle mode.", st, T.faint, F::Small);
        }
    }
    y += 38;

    Caption(L"BEHAVIOUR", cx, y, T.faint);
    y += 20;
    Rect beh{cx, y, cw, 3 * 62.f};
    g.Round(beh, 12, T.panel2);
    g.RoundStroke(beh, 12, T.border);
    struct Opt {
        const wchar_t* title;
        const wchar_t* desc;
        bool* v;
    } opts[3] = {
        {L"Repeat while held", L"Runs the steps again and again until you let go.", &e.repeat},
        {L"Toggle mode", L"Press once to start, press again to stop.", &e.toggle},
        {L"Block the trigger key", L"The game never sees the key that fires this macro.", &e.block},
    };
    for (int i = 0; i < 3; i++) {
        Rect r{beh.x, beh.y + i * 62.f, beh.w, 62};
        if (i) g.Fill({r.x + 16, r.y, r.w - 32, 1}, T.border);
        g.Text(opts[i].title, {r.x + 18, r.y + 12, r.w - 100, 20}, T.text, F::Semi);
        g.Text(opts[i].desc, {r.x + 18, r.y + 33, r.w - 100, 18}, T.dim, F::Small);
        ui.Switch(HashId(L"ed-opt", (uint64_t)i), r.R() - 18 - 40, r.Cy() - 11, *opts[i].v);
    }
    y = beh.B() + 22;

    Caption(L"REPEAT", cx, y, T.faint);
    y += 20;
    Rect tim{cx, y, cw, 66};
    g.Round(tim, 12, T.panel2);
    g.RoundStroke(tim, 12, T.border);
    ui.PushAlpha(e.repeat ? 1.f : 0.5f);
    g.Text(L"Repeat every", {tim.x + 18, tim.y + 14, 220, 20}, T.text, F::Semi);
    g.Text(e.repeat ? Fmt(L"%d ms", e.repeatDelay) : L"needs “Repeat while held”",
           {tim.R() - 18 - 260, tim.y + 14, 260, 20}, e.repeat ? T.accentHi : T.dim, F::Semi, kRight);
    ui.Slider(HashId(L"ed-rep"), {tim.x + 20, tim.y + 42, tim.w - 40, 22}, e.repeatDelay, 1, 500, e.repeat);
    ui.PopAlpha();
    y = tim.B() + 22;

    Caption(L"SYNTAX", cx, y, T.faint);
    y += 20;
    struct Ex {
        const wchar_t* code;
        const wchar_t* what;
    } exs[] = {
        {L"{Edit}, LButton, {Edit}", L"Steps run left to right, separated by commas."},
        {L"LButton down, wait 80, LButton up", L"Hold a key, wait, then let go."},
        {L"wait 50", L"Pause for 50 ms."},
        {L"{Wall}  {Jump}  {Crouch}", L"Your game keybinds, set in Settings."},
        {L"XButton1  XButton2  WheelUp", L"Mouse 4, Mouse 5 and the scroll wheel."},
        {L"move 120 -40", L"Move the mouse: counts right and down."},
    };
    Rect syn{cx, y, cw, (float)std::size(exs) * 30 + 52};
    g.Round(syn, 12, T.field);
    g.RoundStroke(syn, 12, T.border);
    float sy = syn.y + 12;
    for (auto& ex : exs) {
        std::wstring code = ex.code;
        auto spans = ui.StepSpans(code, cfg_.binds);
        g.Text(code, {syn.x + 16, sy, cw * 0.52f, 26}, T.dim, F::MonoSmall, kLeft, true, false, spans.data(), spans.size());
        g.Text(ex.what, {syn.x + 16 + cw * 0.52f, sy, cw * 0.48f - 28, 26}, T.dim, F::Small);
        sy += 30;
    }
    g.Text(L"Keys: A–Z, 0–9, F1–F24, Space, LShiftKey, LControlKey, LMenu (Alt), Tab, LButton, RButton, MButton.",
           {syn.x + 16, sy + 2, cw - 32, 34}, T.faint, F::Small, kLeft, false, true);
    y = syn.B() + 24;
    ui.EndScroll(sid, y - view.y, T.panel);

    // footer
    g.Fill({box.x, foot.y, box.w, 1}, T.border);
    float by = foot.y + 18;
    if (!e.isNew && ui.Button(HashId(L"ed-del"), {box.x + 24, by, 104, 38}, L"Delete", Btn::Danger, L""))
        DeleteMacro(e.macroId);
    if (!e.saveError.empty()) {
        float x0 = box.x + (e.isNew ? 24 : 140);
        g.Text(L"", {x0, by, 16, 38}, T.red, F::IconSm);
        g.Text(e.saveError, {x0 + 20, by, box.R() - 280 - x0, 38}, T.red, F::Small);
    }
    if (ui.Button(HashId(L"ed-save"), {box.R() - 24 - 124, by, 124, 38}, e.isNew ? L"Create" : L"Save", Btn::Primary, L""))
        SaveEditor();
    if (ui.Button(HashId(L"ed-cancel"), {box.R() - 24 - 124 - 10 - 100, by, 100, 38}, L"Cancel", Btn::Secondary))
        CloseEditor();
    if (input && r1.submitted) SaveEditor();
    ui.PopXf();
}

// ---------------------------------------------------------------- preview scenes for screenshots

void App::PreviewSetup(const std::wstring& s) {
    licensed_ = s.rfind(L"license", 0) != 0;
    cfg_.tutorialDone = s.rfind(L"tutorial", 0) != 0;  // show the walkthrough only for its own shot
    if (s == L"splash") splashAt_ = NowSeconds();
    if (s == L"tutorial2") tutorialStep_ = 1;
    if (s == L"tutorial-last") tutorialStep_ = 4;
    if (s == L"license-error") licError_ = L"Key not found.";
    // demo countdown for screenshots
    lic_.plan = L"weekly";
    lic_.expiry = (long long)time(nullptr) + 5 * 86400 + 3 * 3600 + 24 * 60;
    auto set = [&](const wchar_t* name, const wchar_t* key, bool en) {
        for (auto& m : cfg_.macros)
            if (m.name == name) {
                m.trigger = key;
                m.enabled = en;
            }
    };
    set(L"Single Edit", L"XButton2", true);
    set(L"Drag Edit", L"XButton1", true);
    set(L"Edit Reset", L"Z", true);
    set(L"Edit Spam", L"", false);
    set(L"Wall Retake", L"Q", true);
    set(L"Box Up", L"MButton", true);
    set(L"Auto Run", L"OemPeriod", true);
    set(L"Auto Clicker", L"F6", false);
    if (s.size() > 6 && s.substr(s.size() - 6) == L"-light") {
        cfg_.theme = L"Light";
        T = kLight;
    }
    if (s.find(L"-collapsed") != std::wstring::npos) cfg_.sidebarCollapsed = true;
    RecomputeIssues();
    engine_.Reindex(cfg_);
    double now = NowSeconds();
    lastFired_ = L"Single Edit";
    lastFiredAt_ = now - 4.2;
    // fake stats for screenshots
    const wchar_t* names[] = {L"Single Edit", L"Box Up", L"Wall Retake", L"Drag Edit", L"Ramp Rush", L"Edit Reset"};
    const int weights[] = {38, 22, 16, 12, 7, 5};
    for (int d = 0; d < 30; d++) {
        int day = (int)(180 + 140 * std::sin(d * 0.7) + 90 * std::sin(d * 1.9) + (29 - d) * 4);
        stats_.days[StatsDayKey(d)] = std::max(20, day);
    }
    for (int h = 0; h < 48; h++) stats_.hours[StatsHourKey(h)] = std::max(0, (int)(18 + 15 * std::sin(h * 0.5) + (h % 5) * 3));
    for (int i = 0; i < 6; i++) stats_.macros[names[i]] = weights[i] * 61;
    stats_.total = 9120;
    for (int i = 0; i < 6; i++) recent_.push_back({names[i], i % 2 ? L"Mouse 5" : L"Q", now - 3 - i * 11.0});
    auto idOf = [&](const wchar_t* name) {
        for (auto& m : cfg_.macros)
            if (m.name == name) return m.id;
        return 0;
    };
    if (s == L"fire") {
        Navigate(L"Editing");
        flash_[idOf(L"Single Edit")] = now + 0.05;
    }
    if (s == L"editing") Navigate(L"Editing");
    if (s == L"search") {
        ui.focus = HashId(L"search");
        search_ = L"edit";
    }
    if (s == L"building") {
        Navigate(L"Building");
    } else if (s == L"movement") {
        running_.insert(idOf(L"Auto Run"));
        Navigate(L"Movement");
    } else if (s == L"custom") {
        Navigate(L"Custom");
    } else if (s == L"editor") {
        OpenEditor(idOf(L"Drag Edit"));
    } else if (s == L"new") {
        OpenEditor(0);
        ed_.steps = L"{Edit}, LButton, wait 30, {Nope}";
    } else if (s == L"confirm") {
        DeleteMacro(idOf(L"Edit Reset"));
    } else if (s == L"import") {
        ImportMacro();
        pr_.text = L"MT1:hello";
        pr_.error = L"The share code is damaged (copy all of it).";
    } else if (s == L"tweaks") {
        Navigate(L"Tweaks");
    } else if (s == L"settings") {
        Navigate(L"Settings");
    } else if (s == L"profiles") {
        Navigate(L"Profiles");
        profiles_ = {{L"Arena", L"", L"24 macros  ·  saved Oct 24, 21:14"},
                     {L"Creative warmup", L"", L"26 macros  ·  saved Oct 25, 18:02"},
                     {L"Zone wars", L"", L"22 macros  ·  saved Oct 26, 09:40"}};
    } else if (s == L"tester") {
        Navigate(L"Tester");
        long long f = QpcNow();
        double per = QpcToMs(1);
        auto q = [&](double ms) { return f + (long long)(ms / per); };
        TesterRun r;
        r.name = L"Single Edit";
        r.qpc0 = f;
        r.id = HashId(L"run", 1);
        r.ev = {{false, L"", 'G', true, q(0.02)},  {false, L"", 'G', false, q(0.05)},
                {false, L"", VK_LBUTTON, true, q(7.1)}, {false, L"", VK_LBUTTON, false, q(7.2)},
                {false, L"", 'G', true, q(14.2)}, {false, L"", 'G', false, q(14.3)}};
        runs_.push_front(r);
        TesterRun r2;
        r2.name = L"Drag Edit";
        r2.qpc0 = f;
        r2.id = HashId(L"run", 2);
        r2.ev = {{false, L"", 'G', true, q(0.02)}, {false, L"", 'G', false, q(0.04)},
                 {false, L"", VK_LBUTTON, true, q(7.1)}, {false, L"", VK_LBUTTON, false, q(182.4)},
                 {false, L"", 'G', true, q(189.5)}, {false, L"", 'G', false, q(189.6)}};
        runs_.push_back(r2);
    } else if (s == L"menu") {
        OpenMenu({W_ > 0 ? W_ - 84 : 1036, 166, 32, 32},
                 {{L"Edit…", L"", false, nullptr},
                  {L"Duplicate", L"", false, nullptr},
                  {L"Copy share code", L"", false, nullptr},
                  {L"Clear key", L"", false, nullptr},
                  {L"Delete", L"", true, nullptr}});
    } else if (s == L"capture") {
        StartCapture(Capture::MacroKey, HashId(HashId(L"row", (uint64_t)idOf(L"Edit Spam")), 2), idOf(L"Edit Spam"));
    } else if (s == L"toast") {
        ShowToast(L"Share code copied. Paste it to a friend.", 1);
    }
}
