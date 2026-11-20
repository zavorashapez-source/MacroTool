#pragma once
#include "devices.h"
#include "engine.h"
#include "license.h"
#include "pad.h"
#include "tweaks.h"
#include "ui.h"
#include <deque>
#include <functional>
#include <map>
#include <set>

constexpr UINT WM_APP_ENGINE = WM_APP + 1;
constexpr UINT WM_APP_TESTER = WM_APP + 2;
constexpr UINT WM_APP_MONITOR = WM_APP + 3;
constexpr UINT WM_APP_TASK = WM_APP + 4;
constexpr UINT WM_APP_MENU = WM_APP + 5; // wParam = macro id (menu click)
constexpr UINT WM_APP_PAD = WM_APP + 6;  // controller state changed (Main page)

constexpr float kSideOpen = 256.f;
constexpr float kSideClosed = 76.f;
constexpr float kTopH = 68.f;
constexpr float kChrome = 40.f; // title strip above everything

void DrawLogo(Gfx &g, const Rect &r);
double NowSeconds();

std::wstring StatsDayKey(int daysAgo);
std::wstring StatsHourKey(int hoursAgo);

// fire counts, saved to stats.json
struct Stats
{
    std::map<std::wstring, int> days;   // "2026-10-27"
    std::map<std::wstring, int> hours;  // "2026-10-27 14"
    std::map<std::wstring, int> macros; // name -> count, all time
    int total = 0;
    bool dirty = false;
    void Add(const std::wstring &macro);
    void Load();
    void Save();
};

class App
{
  public:
    App(Gfx &g, UI &ui) : g(g), ui(ui)
    {
    }

    // preview: render only (for --shot), no hooks/threads/saving
    void Init(HWND hwnd, bool preview);
    void Shutdown();
    void Frame(float W, float H);

    void OnEngine();
    void OnTester();
    void OnMonitor()
    {
    }
    void OnTasks();
    void OnMenu(int id);
    // re-reads the connected devices in the background (startup, plug/unplug)
    void RefreshDevices();
    void OnActivate(bool active);
    int HitTest(float x, float y);
    std::wstring StartupWarning;

    // --shot helpers
    void Navigate(const std::wstring &page);
    void OpenEditor(int macroId);
    void PreviewSetup(const std::wstring &scenario);

  private:
    struct Issue
    {
        std::wstring text;
        bool error = false;
    };
    struct EditorState
    {
        int macroId = 0;
        bool isNew = false;
        std::wstring name, trigger, steps, release;
        int cat = 4;
        int hold = 20, delay = 35, repeatDelay = 30;
        bool repeat = false, toggle = false, block = true;
        double shakeAt = -10;
        std::wstring saveError;
        bool recording = false;
        std::wstring recInfo;
    };
    struct PromptState
    {
        std::wstring title, label, text, placeholder, ok;
        bool mono = false;
        std::function<bool(const std::wstring &)> onOk; // false keeps the dialog open
        std::wstring error;
    };
    struct ConfirmState
    {
        std::wstring title, text, ok;
        bool danger = false;
        std::function<void()> onOk;
    };
    struct MessageState
    {
        std::wstring title, text;
        bool error = false;
    };
    enum class ModalKind
    {
        Editor,
        Prompt,
        Confirm,
        Message
    };
    struct ModalEntry
    {
        ModalKind kind;
        bool closing = false;
        ID anim;
        float t = 0;
    };
    struct MenuItem
    {
        std::wstring label;
        const wchar_t *glyph;
        bool danger;
        std::function<void()> act;
    };
    struct MenuState
    {
        bool open = false;
        Rect anchor;
        std::vector<MenuItem> items;
        ID anim = 0;
        float width = 214;
    };
    struct Toast
    {
        std::wstring text;
        int kind; // 0 info, 1 success, 2 warning
        double born;
        ID id;
    };
    struct Capture
    {
        enum Kind
        {
            None,
            MacroKey,
            EditorKey,
            BindKey,
            ToggleKey,
            ResetKey
        } kind = None;
        int macroId = 0;
        std::wstring bind;
        int token = 0;
        ID chip = 0;
    };
    struct TesterRun
    {
        std::wstring name;
        long long qpc0 = 0;
        double at = 0;
        std::vector<TesterEvent> ev;
        ID id;
    };
    struct ProfileInfo
    {
        std::wstring name, path, sub;
    };
    struct TweakUi
    {
        int state = -1;
        bool busy = false;
    };
    struct Recent
    {
        std::wstring name, key;
        double at;
    };
    struct SearchHit
    {
        std::wstring title, sub, page;
        const wchar_t *glyph;
        int macroId;
    };
    // state changes
    void Changed();
    void SaveCfg();
    void RecomputeIssues();
    Macro *FindMacro(int id);
    void StartCapture(Capture::Kind k, ID chip, int macroId = 0, const std::wstring &bind = L"");
    void CancelCapture();
    void ApplyCapture(int vk);
    void SetMaster(bool on);
    void SetTheme(bool light);
    void ShowToast(const std::wstring &text, int kind = 0);
    void OpenModal(ModalKind k);
    void CloseModal(ModalKind k);
    bool ModalOpen(ModalKind k) const;
    void Prompt(const std::wstring &title, const std::wstring &label, const std::wstring &initial,
                std::function<bool(const std::wstring &)> onOk, const std::wstring &ok = L"Save", bool mono = false,
                const std::wstring &placeholder = L"");
    void Confirm(const std::wstring &title, const std::wstring &text, const std::wstring &ok, bool danger,
                 std::function<void()> onOk);
    void Message(const std::wstring &title, const std::wstring &text, bool error = false);
    void OpenMenu(const Rect &anchorScreen, std::vector<MenuItem> items, float width = 214);
    void RunAsync(std::function<std::function<void()>()> work);
    void HandleKeys();

    // actions
    void ImportMacro();
    void ExportBackup();
    void DuplicateMacro(int id);
    void DeleteMacro(int id);
    void SaveEditor();
    void CloseEditor();
    void ToggleRecord();
    void FinishRecord();
    void LoadTweaks();
    void SetTweak(size_t i, bool on);
    void RunAction(size_t i);
    void RevertAllTweaks();
    void RefreshProfiles();
    void SaveProfile(const std::wstring &name, bool overwriteOk);
    void LoadProfile(const ProfileInfo &p);
    std::vector<SearchHit> SearchResults();

    // drawing
    void DrawBase();
    void DrawSplash(float W, float H);
    void DrawTutorial(float W, float H);
    void DrawLicenseGate(float W, float H);
    void Activate(const std::wstring &key, bool remember);
    void SignOut(const std::wstring &why);
    void DrawSidebar(float sideW);
    void DrawSideFooter(float sideW, float open);
    void DrawTopBar(const Rect &bar);
    void DrawSearchResults();
    void DrawHeader(const Rect &r);
    void DrawWindowButtons();
    void PageDashboard(const Rect &area);
    void PageCategory(const Rect &area);
    void PageTweaks(const Rect &area);
    void PageTester(const Rect &area);
    void PageProfiles(const Rect &area);
    void PageSettings(const Rect &area);
    // Main page (mainpage.cpp)
    using BindMap = std::map<int, std::vector<const Macro *>>;
    using BindList = std::vector<std::pair<int, const Macro *>>;
    struct Anchor // where a callout line starts on the picture
    {
        int vk;
        float x, y;
        bool left;
    };
    void PageMain(const Rect &area);
    void MainTabs(const Rect &r, const Device *const devs[3], const int counts[3], const bool online[3]);
    void Stage(const Rect &r);
    void DrawKeyboard(const Rect &r, const BindMap &binds);
    void DrawMouse(const Rect &r, const BindMap &binds, std::vector<Anchor> &anchors);
    void DrawPad(const Rect &r, PadStyle style, const Pad::State &st, bool live, const BindMap &binds,
                 std::vector<Anchor> &anchors);
    void Callouts(const std::vector<Anchor> &anchors, const BindMap &binds, float leftX, float rightX, float top,
                  float bottom, PadStyle style);
    float BindRows(float x, float y, float w, const BindList &items, PadStyle style);
    float KeyFlash(const std::vector<const Macro *> &ms);
    float MainAppear(float delay, float dur = 0.4f);
    float MacroList(const std::wstring &category, float x, float y, float w, int firstRow);
    void MacroRow(Macro &m, const Rect &r);
    bool KeyCap(ID id, const Rect &r, const std::wstring &label, bool empty, bool capturing, float fire, bool lit,
                F font = F::Semi);
    void StatCard(const Rect &r, const wchar_t *glyph, Color c, const std::wstring &label, const std::wstring &value,
                  const std::wstring &delta, Color deltaC, int i);
    void Card(const Rect &r);
    void CardTitle(const Rect &r, const std::wstring &title, const std::wstring &legend = L"", Color legendC = {});
    float EmptyState(const Rect &area, const wchar_t *glyph, const std::wstring &title, const std::wstring &text);
    void Caption(const std::wstring &text, float x, float y, Color c);
    float RowAppear(int i);
    void DrawModal(ModalEntry &m, bool input);
    void DrawEditor(const Rect &box, bool input);
    void DrawPrompt(const Rect &box);
    void DrawConfirm(const Rect &box);
    void DrawMessage(const Rect &box);
    Rect ModalBox(ModalKind k, float t);
    void DrawMenu();
    void DrawToasts();

    bool IsMacroPage() const;
    std::wstring TimingLabel() const;
    const wchar_t *PageGlyph(const std::wstring &page) const;

    Gfx &g;
    UI &ui;
    HWND hwnd_ = nullptr;
    bool preview_ = false;
    bool active_ = true;
    float W_ = 0, H_ = 0;
    float sideW_ = kSideOpen;
    std::vector<Rect> holes_; // clickable bits inside the drag area

    Config cfg_;
    Engine engine_;
    Stats stats_;
    double statsSavedAt_ = 0;
    std::unordered_map<int, Issue> issues_;
    std::set<int> running_;
    std::unordered_map<int, double> flash_;
    std::vector<Device> devices_;
    bool devicesBusy_ = false, devicesAgain_ = false;
    bool padPreview_ = false; // --shot: fake controller instead of xinput
    Pad::State padShot_;
    int mainDev_ = 0, mainDir_ = 1; // shown device: 0 keyboard, 1 mouse, 2 controller
    double mainSwitchAt_ = -10;
    int mainHot_ = 0, mainHotNext_ = 0; // key hovered in a list / callout / picture
    std::deque<Recent> recent_;
    std::wstring lastFired_;
    double lastFiredAt_ = -1;
    double startedAt_ = 0;

    std::wstring page_ = L"Dashboard";
    double pageStart_ = 0;
    bool macrosOpen_ = true;
    int rangeDays_ = 7;
    std::wstring search_;
    int searchSel_ = 0;
    std::vector<ModalEntry> modals_;
    EditorState ed_;
    PromptState pr_;
    ConfirmState cf_;
    MessageState msg_;
    MenuState menu_;
    std::vector<Toast> toasts_;
    Capture cap_;
    int capToken_ = 0;
    int toastSeq_ = 0;

    std::unordered_map<std::wstring, int> badges_;
    std::deque<TesterRun> runs_;
    int runSeq_ = 0;
    std::vector<ProfileInfo> profiles_;
    std::vector<TweakUi> tweakUi_;
    std::vector<bool> actionBusy_;
    bool tweaksLoading_ = false;
    bool tweaksLoaded_ = false;

    double splashAt_ = -1;  // splash start
    int tutorialStep_ = -1; // first run tutorial, -1 = off
    bool licensed_ = false;
    bool licBusy_ = false;
    std::wstring licKey_, licError_;
    LicenseResult lic_;

    std::mutex taskMu_;
    std::vector<std::function<void()>> tasks_;
};
