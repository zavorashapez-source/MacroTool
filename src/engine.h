#pragma once
#include "model.h"
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Input {
extern std::atomic<bool> dryRun;
void Send(int vk, bool down);
// Relative mouse movement in raw counts.
void Move(int dx, int dy);
// Sends key-up for anything a macro left held (app exit, macros switched off).
void ReleaseStuck();
}  // namespace Input

// Feed for the Tester page: macro starts and every key the engine sends.
struct TesterEvent {
    bool fire = false;
    std::wstring macro;
    int key = 0;
    bool down = false;
    long long qpc = 0;
    int dx = 0, dy = 0;  // key == kMouseMove
};

namespace Tester {
void SetNotify(HWND wnd, UINT msg);
void SetListening(bool on);
std::vector<TesterEvent> Drain();
}  // namespace Tester

struct EngineMsg {
    enum Type { Fired, Stopped, Toggled, Captured, CaptureCanceled, RecordStopped } type;
    int macroId = 0;
    int key = 0;
    int token = 0;
};

class Engine {
public:
    // Hooks run on their own high-priority thread, so a busy UI can never make Windows
    // drop them (the .NET version hooked on the UI thread).
    void Start(HWND notify, UINT msg);
    void Shutdown();

    void Reindex(const Config& cfg);
    void SetEnabled(bool on);
    bool Enabled() const { return enabled_.load(); }

    void BeginCapture(int token);
    void CancelCapture();
    bool Capturing() const { return capture_.load(); }

    // Esc stops a recording from inside the game.
    void BeginRecord();
    std::vector<InputEvent> EndRecord();
    bool Recording() const { return recording_.load(); }
    size_t RecordCount();

    // While the user types into one of our text boxes, keys go to the text box, not to macros.
    void SetTyping(bool typing, HWND mainWnd) {
        typing_ = typing;
        mainWnd_ = mainWnd;
    }

    std::vector<EngineMsg> Drain();
    bool IsRunning(int macroId);

    // Locked = no license: every key passes straight through and nothing fires.
    void SetLocked(bool locked);
    bool ForegroundIsGame();  // hook thread; cached by foreground window
    bool Handle(int key, bool down);  // hook thread

private:
    struct Compiled {
        int id = 0;
        std::wstring name;
        int trigger = 0;
        std::vector<Step> press, release;
        int holdMs = 0, stepDelay = 0, repeatDelay = 1;
        bool repeat = false, toggle = false, block = false;
        bool editKind = false;  // Editing/Building macro: eligible for the stuck-edit guard
        std::wstring sig;
    };
    struct Run {
        std::shared_ptr<const Compiled> c;
        HANDLE cancel = nullptr;
        ~Run() {
            if (cancel) CloseHandle(cancel);
        }
    };

    void HookThread();
    void StartLocked(const std::shared_ptr<const Compiled>& c);
    void ReleaseLocked(int id);
    void ReleaseAllLocked();
    void Worker(std::shared_ptr<Run> r);
    void RunSteps(const std::vector<Step>& steps, const Compiled& c);
    void MaybeEditReset();  // worker thread; taps reset if still in the edit overlay
    void Post(EngineMsg m);

    std::mutex mu_;
    std::condition_variable idle_;
    std::map<int, std::vector<std::shared_ptr<const Compiled>>> index_;
    std::vector<int> held_;
    std::unordered_map<int, std::shared_ptr<Run>> running_;  // started, not yet released
    std::unordered_map<int, std::shared_ptr<Run>> active_;   // until release steps finish
    std::vector<EngineMsg> msgs_;
    std::vector<InputEvent> rec_;
    long long recStart_ = 0;
    int captureToken_ = 0;

    std::atomic<bool> enabled_{true};
    std::atomic<bool> capture_{false};
    std::atomic<bool> recording_{false};
    std::atomic<bool> typing_{false};
    std::atomic<bool> locked_{false};
    std::atomic<bool> humanize_{false};  // Frame Better: small random timing so inputs look human
    std::atomic<bool> onlyInGame_{false};
    std::atomic<bool> editGuard_{false};        // auto-reset when stuck in edit mode
    std::atomic<int> editResetVk_{'G'};         // key tapped to leave edit mode
    std::atomic<bool> editKeyCheck_{false};     // throttle rapid edit/build fires
    std::atomic<long long> lastEditMs_{0};      // last edit-macro start (ms), for the throttle
    HWND lastFg_ = nullptr;  // hook thread only
    bool lastFgGame_ = false;
    std::atomic<HWND> mainWnd_{nullptr};
    std::atomic<int> toggleVk_{VK_F8};
    std::atomic<bool> sound_{true};
    std::atomic<int> timing_{0};  // 0 frame, 1 ping, 2 zero, 3 per macro

    HWND notify_ = nullptr;
    UINT notifyMsg_ = 0;
    std::thread hookThread_;
    DWORD hookThreadId_ = 0;
};
