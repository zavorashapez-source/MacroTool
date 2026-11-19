#include "engine.h"
#include "monitor.h"
#include <algorithm>
#include <random>
#include <unordered_set>

// rng for frame better jitter
static int RandInt(int lo, int hi)
{
    if (hi <= lo)
        return lo;
    thread_local std::mt19937 rng((unsigned)(QpcNow() ^ (uintptr_t)GetCurrentThreadId()));
    return lo + (int)(rng() % (unsigned)(hi - lo + 1));
}

namespace
{

struct TimerHolder
{
    HANDLE h = nullptr;
    ~TimerHolder()
    {
        if (h)
            CloseHandle(h);
    }
};
thread_local TimerHolder tlTimer;

HANDLE ThreadTimer()
{
    if (!tlTimer.h)
    {
        tlTimer.h = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!tlTimer.h)
            tlTimer.h = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
    return tlTimer.h;
}

// hi-res sleep (sub ms on win10 1803+). true if cancel fired first so repeats stop
// right away
bool Wait(int ms, HANDLE cancel)
{
    if (ms <= 0)
        return cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0;
    HANDLE t = ThreadTimer();
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)ms * 10000;
    if (!t || !SetWaitableTimer(t, &due, 0, nullptr, nullptr, FALSE))
    {
        if (cancel)
            return WaitForSingleObject(cancel, ms) == WAIT_OBJECT_0;
        Sleep(ms);
        return false;
    }
    if (cancel)
    {
        HANDLE hs[2] = {cancel, t};
        if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) == WAIT_OBJECT_0)
        {
            CancelWaitableTimer(t);
            return true;
        }
        return false;
    }
    WaitForSingleObject(t, INFINITE);
    return false;
}

Engine *g_engine = nullptr;

LRESULT CALLBACK KbProc(int n, WPARAM w, LPARAM l)
{
    if (n == HC_ACTION && g_engine)
    {
        auto *k = (KBDLLHOOKSTRUCT *)l;
        if (!(k->flags & LLKHF_INJECTED))
        {
            bool down = w == WM_KEYDOWN || w == WM_SYSKEYDOWN;
            if (g_engine->Handle((int)k->vkCode, down))
                return 1;
        }
    }
    return CallNextHookEx(nullptr, n, w, l);
}

LRESULT CALLBACK MsProc(int n, WPARAM w, LPARAM l)
{
    if (n == HC_ACTION && w != WM_MOUSEMOVE && g_engine)
    {
        auto *m = (MSLLHOOKSTRUCT *)l;
        if (!(m->flags & LLMHF_INJECTED))
        {
            if (w == WM_MOUSEWHEEL)
            {
                int key = (short)HIWORD(m->mouseData) > 0 ? kWheelUp : kWheelDown;
                bool block = g_engine->Handle(key, true);
                g_engine->Handle(key, false);
                if (block)
                    return 1;
            }
            else
            {
                int key = 0;
                bool down = false;
                switch (w)
                {
                case WM_LBUTTONDOWN:
                    key = VK_LBUTTON;
                    down = true;
                    break;
                case WM_LBUTTONUP:
                    key = VK_LBUTTON;
                    break;
                case WM_RBUTTONDOWN:
                    key = VK_RBUTTON;
                    down = true;
                    break;
                case WM_RBUTTONUP:
                    key = VK_RBUTTON;
                    break;
                case WM_MBUTTONDOWN:
                    key = VK_MBUTTON;
                    down = true;
                    break;
                case WM_MBUTTONUP:
                    key = VK_MBUTTON;
                    break;
                case WM_XBUTTONDOWN:
                case WM_XBUTTONUP:
                    key = HIWORD(m->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
                    down = w == WM_XBUTTONDOWN;
                    break;
                }
                if (key && g_engine->Handle(key, down))
                    return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, n, w, l);
}

} // namespace

// ---------------------------------------------------------------- tester feed

namespace Tester
{
static std::mutex mu;
static std::vector<TesterEvent> queue;
static std::atomic<bool> listening{false};
static HWND wnd = nullptr;
static UINT wmsg = 0;

void SetNotify(HWND w, UINT m)
{
    wnd = w;
    wmsg = m;
}
void SetListening(bool on)
{
    listening = on;
    if (!on)
    {
        std::lock_guard lk(mu);
        queue.clear();
    }
}
static void Push(TesterEvent e)
{
    if (!listening)
        return;
    std::lock_guard lk(mu);
    bool was = queue.empty();
    if (queue.size() < 5000)
        queue.push_back(std::move(e));
    if (was && wnd)
        PostMessageW(wnd, wmsg, 0, 0);
}
std::vector<TesterEvent> Drain()
{
    std::lock_guard lk(mu);
    return std::exchange(queue, {});
}
} // namespace Tester

// ---------------------------------------------------------------- input

namespace Input
{
std::atomic<bool> dryRun{false};
static std::mutex mu;
static std::unordered_set<int> held;

static bool Extended(int k)
{
    switch (k)
    {
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
        return true;
    }
    return false;
}

static void Raw(int k, bool down)
{
    INPUT in{};
    if (k == kWheelUp || k == kWheelDown)
    {
        if (!down)
            return;
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = (DWORD)(k == kWheelUp ? WHEEL_DELTA : -WHEEL_DELTA);
        SendInput(1, &in, sizeof(INPUT));
        return;
    }
    DWORD dn = 0, up = 0, data = 0;
    switch (k)
    {
    case VK_LBUTTON:
        dn = MOUSEEVENTF_LEFTDOWN;
        up = MOUSEEVENTF_LEFTUP;
        break;
    case VK_RBUTTON:
        dn = MOUSEEVENTF_RIGHTDOWN;
        up = MOUSEEVENTF_RIGHTUP;
        break;
    case VK_MBUTTON:
        dn = MOUSEEVENTF_MIDDLEDOWN;
        up = MOUSEEVENTF_MIDDLEUP;
        break;
    case VK_XBUTTON1:
        dn = MOUSEEVENTF_XDOWN;
        up = MOUSEEVENTF_XUP;
        data = XBUTTON1;
        break;
    case VK_XBUTTON2:
        dn = MOUSEEVENTF_XDOWN;
        up = MOUSEEVENTF_XUP;
        data = XBUTTON2;
        break;
    }
    if (dn)
    {
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = down ? dn : up;
        in.mi.mouseData = data;
    }
    else
    {
        in.type = INPUT_KEYBOARD;
        WORD scan = (WORD)MapVirtualKeyW((UINT)k, MAPVK_VK_TO_VSC);
        in.ki.wVk = (WORD)k;
        in.ki.wScan = scan;
        // games want scan codes, media keys etc don't have one
        in.ki.dwFlags =
            (scan ? KEYEVENTF_SCANCODE : 0) | (down ? 0 : KEYEVENTF_KEYUP) | (Extended(k) ? KEYEVENTF_EXTENDEDKEY : 0);
    }
    {
        std::lock_guard lk(mu);
        if (down)
            held.insert(k);
        else
            held.erase(k);
    }
    SendInput(1, &in, sizeof(INPUT));
}

void Send(int k, bool down)
{
    Tester::Push({false, L"", k, down, QpcNow()});
    if (dryRun)
        return;
    Raw(k, down);
}

void Move(int dx, int dy)
{
    TesterEvent te{false, L"", kMouseMove, false, QpcNow()};
    te.dx = dx;
    te.dy = dy;
    Tester::Push(te);
    if (dryRun || (!dx && !dy))
        return;
    int biggest = std::max(std::abs(dx), std::abs(dy));
    // some games clamp big deltas, so split them
    int chunks = std::max(1, (biggest + 199) / 200);
    std::vector<INPUT> ins;
    for (int i = 0; i < chunks; i++)
    {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_MOVE;
        in.mi.dx = (int)((long long)dx * (i + 1) / chunks - (long long)dx * i / chunks);
        in.mi.dy = (int)((long long)dy * (i + 1) / chunks - (long long)dy * i / chunks);
        ins.push_back(in);
    }
    if (!ins.empty())
        SendInput((UINT)ins.size(), ins.data(), sizeof(INPUT));
}

void ReleaseStuck()
{
    std::vector<int> keys;
    {
        std::lock_guard lk(mu);
        keys.assign(held.begin(), held.end());
    }
    for (int k : keys)
        Raw(k, false);
}
} // namespace Input

// ---------------------------------------------------------------- engine

// "is fortnite focused" for worker threads (ForegroundIsGame is hook thread only)
static bool FgIsGame()
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid)
        return false;
    bool game = false;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
    {
        wchar_t path[MAX_PATH * 2];
        DWORD len = (DWORD)std::size(path);
        if (QueryFullProcessImageNameW(h, 0, path, &len))
        {
            std::wstring_view p(path, len);
            size_t slash = p.find_last_of(L"\\/");
            std::wstring_view name = slash == std::wstring_view::npos ? p : p.substr(slash + 1);
            game = IEquals(name, L"FortniteClient-Win64-Shipping.exe");
        }
        CloseHandle(h);
    }
    return game;
}

// sample a few pixels around the crosshair. edit mode tints the piece cyan-blue,
// if most of them are blue we're stuck -> tap reset once.
// just GetPixel on our own screen, nothing kept.
void Engine::MaybeEditReset()
{
    if (!enabled_.load() || !FgIsGame())
        return;
    HDC dc = GetDC(nullptr);
    if (!dc)
        return;
    int cx = GetSystemMetrics(SM_CXSCREEN) / 2;
    int cy = GetSystemMetrics(SM_CYSCREEN) / 2;
    int blue = 0, n = 0;
    // skip the crosshair dot itself
    for (int ox = -14; ox <= 14; ox += 7)
        for (int oy = -14; oy <= 14; oy += 7)
        {
            if (!ox && !oy)
                continue;
            COLORREF col = GetPixel(dc, cx + ox, cy + oy);
            if (col == CLR_INVALID)
                continue;
            int r = GetRValue(col), g = GetGValue(col), b = GetBValue(col);
            n++;
            if (b > 110 && b > r + 45 && b >= g && g > r)
                blue++; // cyan-blue edit tint
        }
    ReleaseDC(nullptr, dc);
    if (n > 0 && blue * 10 >= n * 6)
    { // strong majority -> stuck in edit
        int vk = editResetVk_.load();
        Input::Send(vk, true);
        Wait(8, nullptr);
        Input::Send(vk, false);
    }
}

void Engine::Start(HWND notify, UINT msg)
{
    notify_ = notify;
    notifyMsg_ = msg;
    g_engine = this;
    hookThread_ = std::thread([this] { HookThread(); });
}

void Engine::HookThread()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    MSG m;
    PeekMessageW(&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    HHOOK hk = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, GetModuleHandleW(nullptr), 0);
    HHOOK hm = SetWindowsHookExW(WH_MOUSE_LL, MsProc, GetModuleHandleW(nullptr), 0);
    while (GetMessageW(&m, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (hk)
        UnhookWindowsHookEx(hk);
    if (hm)
        UnhookWindowsHookEx(hm);
}

void Engine::Shutdown()
{
    {
        std::unique_lock lk(mu_);
        capture_ = false;
        recording_ = false;
        ReleaseAllLocked();
        idle_.wait_for(lk, std::chrono::milliseconds(1500), [&] { return active_.empty(); });
    }
    Input::ReleaseStuck();
    if (hookThread_.joinable())
    {
        DWORD tid = GetThreadId(hookThread_.native_handle());
        for (int i = 0; i < 200 && !PostThreadMessageW(tid, WM_QUIT, 0, 0); i++)
            Sleep(5);
        hookThread_.join();
    }
    g_engine = nullptr;
}

static std::wstring Sig(const std::vector<Step> &steps)
{
    std::wstring s;
    for (auto &st : steps)
        s += Fmt(L"%d%c%d,%d,%d;", st.key, st.kind, st.ms, st.dx, st.dy);
    return s;
}

void Engine::Reindex(const Config &cfg)
{
    int tk;
    toggleVk_ = KeyParse(cfg.toggleKey, tk) ? tk : VK_F8;
    menuVk_ = cfg.menu && KeyParse(cfg.menuKey, tk) ? tk : 0;
    sound_ = cfg.toggleSound;
    onlyInGame_ = cfg.onlyInGame;
    editGuard_ = cfg.editGuard;
    editKeyCheck_ = cfg.editKeyCheck;
    int ek;
    editResetVk_ = KeyParse(cfg.editResetKey, ek) ? ek : 'G';
    timing_ = 0; // only frame timing remains
    humanize_ = cfg.timing == L"FrameHuman";

    int editVk = 0;
    if (cfg.editOnRelease)
        try
        {
            editVk = ParseKey(L"{Edit}", cfg.binds);
        }
        catch (const FormatError &)
        {
        }

    std::map<int, std::vector<std::shared_ptr<const Compiled>>> idx;
    std::unordered_map<int, std::shared_ptr<const Compiled>> byId;
    for (auto &m : cfg.macros)
    {
        int k;
        if (!m.enabled || !KeyParse(m.trigger, k))
            continue;
        try
        {
            auto c = std::make_shared<Compiled>();
            c->id = m.id;
            c->name = m.name;
            c->trigger = k;
            c->press = ParseSteps(m.steps, cfg.binds);
            c->release = ParseSteps(m.releaseSteps, cfg.binds);
            if (editVk)
                AdaptForEditOnRelease(c->press, c->release, editVk);
            c->holdMs = m.holdMs;
            c->stepDelay = m.stepDelay;
            c->repeatDelay = std::max(1, m.repeatDelay);
            c->repeat = m.repeat;
            c->toggle = m.toggle;
            c->block = m.blockTrigger;
            c->editKind = m.category == L"Editing" || m.category == L"Building";
            c->sig = Fmt(L"%d|%d|%d|%d|%d%d%d|", k, c->holdMs, c->stepDelay, c->repeatDelay, c->repeat, c->toggle,
                         c->block) +
                     Sig(c->press) + L"|" + Sig(c->release);
            idx[k].push_back(c);
            byId[m.id] = c;
        }
        catch (const FormatError &)
        {
            // UI shows the error, just skip
        }
    }
    std::lock_guard lk(mu_);
    index_ = std::move(idx);
    // edited/disabled/deleted while running: still release its keys
    std::vector<int> stale;
    for (auto &[id, r] : running_)
    {
        auto f = byId.find(id);
        if (f == byId.end() || f->second->sig != r->c->sig)
            stale.push_back(id);
    }
    for (int id : stale)
        ReleaseLocked(id);
}

void Engine::SetEnabled(bool on)
{
    std::lock_guard lk(mu_);
    enabled_ = on;
    if (!on)
        ReleaseAllLocked();
}

void Engine::BeginCapture(int token)
{
    std::lock_guard lk(mu_);
    captureToken_ = token;
    capture_ = true;
}

void Engine::CancelCapture()
{
    capture_ = false;
}

void Engine::BeginRecord()
{
    std::lock_guard lk(mu_);
    rec_.clear();
    recStart_ = QpcNow();
    recording_ = true;
}

std::vector<InputEvent> Engine::EndRecord()
{
    std::lock_guard lk(mu_);
    recording_ = false;
    return std::exchange(rec_, {});
}

size_t Engine::RecordCount()
{
    std::lock_guard lk(mu_);
    return rec_.size();
}

std::vector<EngineMsg> Engine::Drain()
{
    std::lock_guard lk(mu_);
    return std::exchange(msgs_, {});
}

bool Engine::IsRunning(int macroId)
{
    std::lock_guard lk(mu_);
    return running_.count(macroId) > 0;
}

void Engine::Post(EngineMsg m)
{
    bool was = msgs_.empty();
    msgs_.push_back(m);
    if (was && notify_)
        PostMessageW(notify_, notifyMsg_, 0, 0);
}

bool Engine::ForegroundIsGame()
{
    HWND fg = GetForegroundWindow();
    if (fg == lastFg_)
        return lastFgGame_;
    lastFg_ = fg;
    lastFgGame_ = false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid)
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h)
        {
            wchar_t path[MAX_PATH * 2];
            DWORD len = (DWORD)std::size(path);
            if (QueryFullProcessImageNameW(h, 0, path, &len))
            {
                std::wstring_view p(path, len);
                size_t slash = p.find_last_of(L"\\/");
                std::wstring_view name = slash == std::wstring_view::npos ? p : p.substr(slash + 1);
                lastFgGame_ = IEquals(name, L"FortniteClient-Win64-Shipping.exe");
            }
            CloseHandle(h);
        }
    }
    return lastFgGame_;
}

void Engine::SetLocked(bool locked)
{
    std::lock_guard lk(mu_);
    locked_ = locked;
    if (locked)
    {
        capture_ = false;
        recording_ = false;
        ReleaseAllLocked();
    }
}

bool Engine::Handle(int k, bool down)
{
    if (locked_)
        return false;
    std::lock_guard lk(mu_);
    if (capture_)
    {
        if (down && k != VK_LBUTTON)
        {
            capture_ = false;
            Post({k == VK_ESCAPE ? EngineMsg::CaptureCanceled : EngineMsg::Captured, 0, k, captureToken_});
        }
        return k != VK_LBUTTON;
    }
    if (recording_)
    {
        long long now = (long long)QpcToMs(QpcNow() - recStart_);
        if (k == VK_ESCAPE)
        {
            // Esc stops recording, game never sees it
            if (down)
            {
                recording_ = false;
                Post({EngineMsg::RecordStopped});
            }
            return true;
        }
        rec_.push_back({k, down, now});
        return false;
    }
    bool repeatPress = false;
    auto it = std::find(held_.begin(), held_.end(), k);
    if (down)
    {
        if (it != held_.end())
            repeatPress = true;
        else
            held_.push_back(k);
    }
    else
    {
        if (it != held_.end())
            held_.erase(it);
        std::vector<int> rel;
        for (auto &[id, r] : running_)
            if (r->c->trigger == k && !r->c->toggle)
                rel.push_back(id);
        for (int id : rel)
            ReleaseLocked(id);
    }
    if (k == toggleVk_)
    {
        if (down && !repeatPress)
        {
            bool on = !enabled_;
            enabled_ = on;
            if (!on)
                ReleaseAllLocked();
            if (sound_)
                std::thread([on] { Beep(on ? 880 : 440, 90); }).detach();
            Post({EngineMsg::Toggled});
        }
        return true;
    }
    // menu key works even with macros off. outside the game it's a normal key
    if (menuVk_ && k == menuVk_)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (!onlyInGame_ || pid == GetCurrentProcessId() || ForegroundIsGame())
        {
            if (down && !repeatPress)
                Post({EngineMsg::Menu});
            return true;
        }
    }
    if (!enabled_)
        return false;
    if (typing_ && GetForegroundWindow() == mainWnd_.load())
        return false;
    // only in fortnite
    if (onlyInGame_ && !ForegroundIsGame())
        return false;
    auto f = index_.find(k);
    if (f == index_.end())
        return false;
    bool block = false;
    for (auto &c : f->second)
    {
        block |= c->block;
        if (down && !repeatPress)
        {
            if (c->toggle && running_.count(c->id))
                ReleaseLocked(c->id);
            else
                StartLocked(c);
        }
    }
    return block;
}

void Engine::StartLocked(const std::shared_ptr<const Compiled> &c)
{
    if (active_.count(c->id))
        return;
    auto r = std::make_shared<Run>();
    r->c = c;
    r->cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    running_[c->id] = r;
    active_[c->id] = r;
    Post({EngineMsg::Fired, c->id});
    Tester::Push({true, c->name, 0, false, QpcNow()});
    std::thread([this, r] { Worker(r); }).detach();
}

void Engine::ReleaseLocked(int id)
{
    auto it = running_.find(id);
    if (it == running_.end())
        return;
    SetEvent(it->second->cancel);
    running_.erase(it);
    Post({EngineMsg::Stopped, id});
}

void Engine::ReleaseAllLocked()
{
    std::vector<int> ids;
    for (auto &[id, r] : running_)
        ids.push_back(id);
    for (int id : ids)
        ReleaseLocked(id);
}

// key check: min gap between edit/build fires
static constexpr int kEditThrottleMs = 45;

void Engine::Worker(std::shared_ptr<Run> r)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    const Compiled &c = *r->c;
    if (c.editKind && editKeyCheck_.load())
    {
        long long now = (long long)QpcToMs(QpcNow());
        long long last = lastEditMs_.exchange(now);
        long long gap = now - last;
        if (last > 0 && gap >= 0 && gap < kEditThrottleMs)
            Wait((int)(kEditThrottleMs - gap), r->cancel);
    }
    for (;;)
    {
        RunSteps(c.press, c);
        int rd = humanize_.load() ? RandInt(c.repeatDelay * 85 / 100, c.repeatDelay * 118 / 100) : c.repeatDelay;
        if (!c.repeat || Wait(rd, r->cancel))
            break;
    }
    // done, reset if still stuck in edit
    if (editGuard_.load() && c.editKind)
        MaybeEditReset();
    WaitForSingleObject(r->cancel, INFINITE);
    RunSteps(c.release, c);
    std::lock_guard lk(mu_);
    auto it = active_.find(c.id);
    if (it != active_.end() && it->second == r)
        active_.erase(it);
    idle_.notify_all();
}

void Engine::RunSteps(const std::vector<Step> &steps, const Compiled &c)
{
    (void)c;
    bool human = humanize_.load();
    int frame = Monitor::frameMs.load();
    for (size_t i = 0; i < steps.size(); i++)
    {
        const Step &s = steps[i];
        switch (s.kind)
        {
        // frame better: vary waits a bit
        case L'w':
            Wait(human ? s.ms + RandInt(0, s.ms / 10 + 2) : s.ms, nullptr);
            continue;
        case L'd':
            Input::Send(s.key, true);
            break;
        case L'u':
            Input::Send(s.key, false);
            break;
        case L'm':
            Input::Move(s.dx, s.dy);
            break;
        default:
            // legacy = instant tap, better = hold 3-10ms
            Input::Send(s.key, true);
            int hold = human ? RandInt(3, 10) : 0;
            if (hold > 0)
                Wait(hold, nullptr);
            Input::Send(s.key, false);
            break;
        }
        if (i + 1 < steps.size())
        {
            int delay = frame + (human ? RandInt(0, 6) : 0);
            if (delay > 0)
                Wait(delay, nullptr);
        }
    }
}
