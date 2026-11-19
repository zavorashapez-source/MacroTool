#include "tweaks.h"
#include <tlhelp32.h>
#include <shellapi.h>
#include <algorithm>
#include <set>
#pragma comment(lib, "shell32.lib")

const TweakGroup kTweakGroups[5] = {
    {L"FPS", L""}, {L"Input", L""}, {L"Ping", L""}, {L"Cleanup", L""}, {L"Safety", L""},
};

namespace {

const wchar_t kExe[] = L"FortniteClient-Win64-Shipping.exe";
const wchar_t kGameBar[] = L"Software\\Microsoft\\GameBar";
const wchar_t kDvr[] = L"System\\GameConfigStore";
const wchar_t kCapture[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR";
const wchar_t kIfeo[] =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\FortniteClient-Win64-Shipping.exe\\PerfOptions";
const wchar_t kGpu[] = L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers";
const wchar_t kMm[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
const wchar_t kLayers[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
const wchar_t kIfaces[] = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces";
const wchar_t kTcpip[]  = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters";
const wchar_t kPsched[] = L"SOFTWARE\\Policies\\Microsoft\\Windows\\Psched";
const wchar_t kDwm[]    = L"SOFTWARE\\Microsoft\\Windows\\DWM";
const wchar_t kDxShader[] = L"SOFTWARE\\Microsoft\\Direct3D";
const wchar_t kPriorityControl[] = L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl";
const wchar_t kMemMgmt[] = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";
const wchar_t kPrefetch[] = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management\\PrefetchParameters";
const wchar_t kVisualFx[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects";
const wchar_t kDesktop[] = L"Control Panel\\Desktop";
const wchar_t kMouse[]   = L"Control Panel\\Mouse";
const wchar_t kKeyboard[] = L"Control Panel\\Keyboard";
const wchar_t kAccessibility[] = L"Control Panel\\Accessibility";
const wchar_t kGpuSched[] = L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\\Scheduler";
const wchar_t kNoFso[] = L"DISABLEDXMAXIMIZEDWINDOWEDMODE";
const wchar_t kUltimate[] = L"e9a42b02-d5df-448d-aa00-03f14749eb61";
const wchar_t kHighPerf[] = L"8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
const wchar_t kBalanced[] = L"381b4222-f694-41f0-9685-ff5bb260df2e";

std::wstring Denied(LSTATUS s) {
    if (s == ERROR_ACCESS_DENIED) return L"Windows denied access. Run Macro Tool as administrator.";
    return Fmt(L"Windows refused the change (error %ld).", (long)s);
}

bool RegIs(HKEY root, const wchar_t* path, const wchar_t* name, DWORD v) {
    DWORD d = 0, sz = sizeof d;
    return RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &d, &sz) == ERROR_SUCCESS && d == v;
}

void RegSet(HKEY root, const wchar_t* path, const wchar_t* name, DWORD v) {
    HKEY k;
    LSTATUS s = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr);
    if (s != ERROR_SUCCESS) throw TweakError{Denied(s)};
    s = RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof v);
    RegCloseKey(k);
    if (s != ERROR_SUCCESS) throw TweakError{Denied(s)};
}

void RegDel(HKEY root, const wchar_t* path, const wchar_t* name) {
    LSTATUS s = RegDeleteKeyValueW(root, path, name);
    if (s != ERROR_SUCCESS && s != ERROR_FILE_NOT_FOUND) throw TweakError{Denied(s)};
}

std::vector<std::wstring> RegValueNames(HKEY k) {
    std::vector<std::wstring> out;
    wchar_t name[16384];
    for (DWORD i = 0;; i++) {
        DWORD len = (DWORD)std::size(name);
        if (RegEnumValueW(k, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        out.emplace_back(name, len);
    }
    return out;
}

std::vector<std::wstring> RegSubKeys(HKEY root, const wchar_t* path) {
    std::vector<std::wstring> out;
    HKEY k;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return out;
    wchar_t name[256];
    for (DWORD i = 0;; i++) {
        DWORD len = (DWORD)std::size(name);
        if (RegEnumKeyExW(k, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        out.emplace_back(name, len);
    }
    RegCloseKey(k);
    return out;
}

std::wstring RegString(HKEY k, const std::wstring& name) {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(k, name.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ) return {};
    std::wstring s(size / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(k, name.c_str(), nullptr, nullptr, (BYTE*)s.data(), &size) != ERROR_SUCCESS) return {};
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

// Runs a console program hidden and returns what it printed on stdout (or stderr).
std::string Run(const std::wstring& exe, const std::wstring& args, bool wantStderr, DWORD* exitCode = nullptr) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return {};
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nullptr;
    si.hStdOutput = wantStderr ? nul : wr;
    si.hStdError = wantStderr ? wr : nul;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    std::string out;
    if (ok) {
        char buf[4096];
        DWORD got;
        while (ReadFile(rd, buf, sizeof buf, &got, nullptr) && got) out.append(buf, got);
        WaitForSingleObject(pi.hProcess, 60000);
        if (exitCode) GetExitCodeProcess(pi.hProcess, exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else if (exitCode) {
        *exitCode = (DWORD)-1;
    }
    CloseHandle(rd);
    return out;
}

std::wstring SysExe(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetSystemDirectoryW(dir, MAX_PATH);
    return PathJoin(dir, name);
}

std::string Power(const std::wstring& args) { return Run(SysExe(L"powercfg.exe"), args, false); }

std::wstring FirstGuid(const std::string& s) {
    auto hex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
    static const int groups[5] = {8, 4, 4, 4, 12};
    for (size_t i = 0; i + 36 <= s.size(); i++) {
        size_t p = i;
        bool ok = true;
        for (int g = 0; g < 5 && ok; g++) {
            for (int k = 0; k < groups[g]; k++, p++)
                if (!hex(s[p])) ok = false;
            if (ok && g < 4) ok = s[p++] == '-';
        }
        if (ok) return Lower(Widen(s.substr(i, 36)));
    }
    return {};
}

std::wstring PowerActive() { return FirstGuid(Power(L"/getactivescheme")); }

void PowerMax(PowerState& ps) {
    std::wstring active = PowerActive();
    if (!active.empty() && active != ps.max && active != kHighPerf) ps.prev = active;
    if (ps.max.empty() || !IContains(Widen(Power(L"/list")), ps.max)) {
        std::wstring g = FirstGuid(Power(std::wstring(L"-duplicatescheme ") + kUltimate));
        ps.max = g.empty() ? kHighPerf : g;
    }
    Power(L"/setactive " + ps.max);
    if (PowerActive() != ps.max) throw TweakError{L"Windows didn't switch the power plan."};
}

std::vector<std::wstring> GamePaths() {
    std::set<std::wstring> paths;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (!IEquals(pe.szExeFile, kExe)) continue;
        // Limited-information access works even for anti-cheat protected processes,
        // where reading the main module (what the .NET version did) is denied.
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD len = (DWORD)std::size(path);
        if (QueryFullProcessImageNameW(h, 0, path, &len)) paths.insert(std::wstring(path, len));
        CloseHandle(h);
    }
    CloseHandle(snap);
    return {paths.begin(), paths.end()};
}

bool FsoDisabled() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kLayers, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    bool on = false;
    for (auto& n : RegValueNames(k))
        if (IEndsWith(n, kExe) && Contains(RegString(k, n), kNoFso)) on = true;
    RegCloseKey(k);
    return on;
}

void FsoDisable() {
    auto paths = GamePaths();
    if (paths.empty()) throw TweakError{L"Start the game first, so the app can find where it's installed."};
    HKEY k;
    LSTATUS s = RegCreateKeyExW(HKEY_CURRENT_USER, kLayers, 0, nullptr, 0, KEY_READ | KEY_SET_VALUE, nullptr, &k, nullptr);
    if (s != ERROR_SUCCESS) throw TweakError{Denied(s)};
    for (auto& p : paths) {
        std::wstring cur = RegString(k, p);
        if (Contains(cur, kNoFso)) continue;
        std::wstring v = cur.empty() ? std::wstring(L"~ ") + kNoFso : cur + L" " + kNoFso;
        RegSetValueExW(k, p.c_str(), 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(k);
}

void FsoEnable() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kLayers, 0, KEY_READ | KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    for (auto& n : RegValueNames(k)) {
        if (!IEndsWith(n, kExe)) continue;
        std::wstring v = RegString(k, n);
        size_t at;
        while ((at = v.find(kNoFso)) != std::wstring::npos) v.erase(at, wcslen(kNoFso));
        v = Trim(v);
        if (v.empty() || v == L"~") RegDeleteValueW(k, n.c_str());
        else RegSetValueExW(k, n.c_str(), 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(k);
}

bool MouseRaw() {
    int v[3] = {};
    SystemParametersInfoW(SPI_GETMOUSE, 0, v, 0);
    return v[2] == 0;
}

void SetMouse(int a, int b, int c) {
    int v[3] = {a, b, c};
    if (!SystemParametersInfoW(SPI_SETMOUSE, 0, v, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        throw TweakError{L"Windows refused the mouse setting."};
}

bool NagleOff() {
    for (auto& n : RegSubKeys(HKEY_LOCAL_MACHINE, kIfaces))
        if (RegIs(HKEY_LOCAL_MACHINE, (std::wstring(kIfaces) + L"\\" + n).c_str(), L"TcpAckFrequency", 1)) return true;
    return false;
}

void Nagle(bool off) {
    for (auto& n : RegSubKeys(HKEY_LOCAL_MACHINE, kIfaces)) {
        std::wstring p = std::wstring(kIfaces) + L"\\" + n;
        if (off) {
            RegSet(HKEY_LOCAL_MACHINE, p.c_str(), L"TcpAckFrequency", 1);
            RegSet(HKEY_LOCAL_MACHINE, p.c_str(), L"TCPNoDelay", 1);
        } else {
            RegDel(HKEY_LOCAL_MACHINE, p.c_str(), L"TcpAckFrequency");
            RegDel(HKEY_LOCAL_MACHINE, p.c_str(), L"TCPNoDelay");
        }
    }
}

void CleanDir(const std::wstring& dir, long long& bytes, int& deleted, int& kept) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;  // never follow links out of Temp
        std::wstring full = dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CleanDir(full, bytes, deleted, kept);
            RemoveDirectoryW(full.c_str());
        } else {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(full.c_str(), fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (DeleteFileW(full.c_str())) {
                bytes += ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                deleted++;
            } else {
                kept++;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

std::wstring ClearTemp() {
    wchar_t tmp[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
    std::wstring dir(tmp, n);
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    if (dir.size() < 4) throw TweakError{L"Couldn't find the Temp folder."};
    long long bytes = 0;
    int deleted = 0, kept = 0;
    CleanDir(dir, bytes, deleted, kept);
    return Fmt(L"Deleted %d files (%.1f MB). %d were in use and kept.", deleted, bytes / 1048576.0, kept);
}

std::wstring RestorePoint() {
    DWORD code = 0;
    std::string err = Run(SysExe(L"WindowsPowerShell\\v1.0\\powershell.exe"),
                          L"-NoProfile -NonInteractive -Command \"Checkpoint-Computer -Description 'Macro Tool tweaks' "
                          L"-RestorePointType MODIFY_SETTINGS\"",
                          true, &code);
    std::wstring e = Trim(Widen(err));
    if (code == 0 && e.empty()) return L"Restore point created.";
    return L"Couldn't create it (System Restore may be off, or one was made in the last 24 hours):\n" + e;
}

std::wstring FlushDns() {
    using Fn = BOOL(WINAPI*)();
    HMODULE m = LoadLibraryW(L"dnsapi.dll");
    auto fn = m ? (Fn)GetProcAddress(m, "DnsFlushResolverCache") : nullptr;
    BOOL ok = fn && fn();
    if (m) FreeLibrary(m);
    return ok ? L"DNS cache cleared." : L"Windows refused to flush DNS.";
}

std::wstring ClearPrefetch() {
    wchar_t sysDir[MAX_PATH]; GetWindowsDirectoryW(sysDir, MAX_PATH);
    std::wstring dir = std::wstring(sysDir) + L"\\Prefetch";
    long long bytes = 0; int deleted = 0, kept = 0;
    CleanDir(dir, bytes, deleted, kept);
    return Fmt(L"Removed %d prefetch entries (%.1f MB). %d in use.", deleted, bytes / 1048576.0, kept);
}

std::wstring ClearShaderCache() {
    wchar_t local[MAX_PATH]; DWORD n = MAX_PATH;
    GetEnvironmentVariableW(L"LOCALAPPDATA", local, n);
    long long bytes = 0; int deleted = 0, kept = 0;
    // NVIDIA, AMD and D3D generic cache locations.
    const wchar_t* dirs[] = {
        L"\\NVIDIA\\DXCache",
        L"\\NVIDIA\\GLCache",
        L"\\AMD\\DxCache",
        L"\\AMD\\GLCache",
        L"\\D3DSCache",
    };
    for (auto* d : dirs) CleanDir(std::wstring(local) + d, bytes, deleted, kept);
    return Fmt(L"Removed %d shader cache files (%.1f MB). Game will rebuild them.", deleted, bytes / 1048576.0);
}

std::wstring EmptyRecycleBin() {
    DWORD r = SHEmptyRecycleBinW(nullptr, nullptr,
        SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    if (r == S_OK || r == 0x8000FFFE /* E_UNEXPECTED, bin already empty */) return L"Recycle bin emptied.";
    return Fmt(L"Windows refused (error 0x%08lX).", (long)r);
}

std::wstring EndBackgroundApps() {
    // Kill common FPS-eating background processes.
    DWORD killed = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e{ sizeof(e) };
    static const wchar_t* targets[] = {
        L"MicrosoftEdge.exe", L"msedge.exe",
        L"OneDrive.exe", L"Teams.exe", L"Spotify.exe",
        L"Discord.exe", L"DiscordPTB.exe", L"DiscordCanary.exe",
        L"GameBar.exe", L"GameBarPresenceWriter.exe",
        L"RuntimeBroker.exe", L"SearchApp.exe", L"YourPhone.exe",
    };
    if (Process32FirstW(snap, &e)) {
        do {
            for (auto* t : targets) {
                if (!_wcsicmp(e.szExeFile, t)) {
                    HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, e.th32ProcessID);
                    if (p) { if (TerminateProcess(p, 0)) killed++; CloseHandle(p); }
                    break;
                }
            }
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return Fmt(L"Closed %lu background processes.", killed);
}

}  // namespace

const std::vector<Tweak>& TweakToggles() {
    static const std::vector<Tweak> list = {
        {L"FPS", L"Max performance power plan", L"Keeps the CPU at full clock instead of slowing down between frames.",
         [](const PowerState& ps) {
             std::wstring a = PowerActive();
             return !a.empty() && (a == ps.max || a == kHighPerf);
         },
         [](PowerState& ps) { PowerMax(ps); },
         [](PowerState& ps) { Power(L"/setactive " + (ps.prev.empty() ? std::wstring(kBalanced) : ps.prev)); }},
        {L"FPS", L"Game Mode", L"Windows prioritises the game and holds back updates while you play.",
         [](const PowerState&) { return RegIs(HKEY_CURRENT_USER, kGameBar, L"AutoGameModeEnabled", 1); },
         [](PowerState&) { RegSet(HKEY_CURRENT_USER, kGameBar, L"AutoGameModeEnabled", 1); },
         [](PowerState&) { RegSet(HKEY_CURRENT_USER, kGameBar, L"AutoGameModeEnabled", 0); }},
        {L"FPS", L"Turn off Game DVR recording", L"Stops Xbox Game Bar recording in the background, a common cause of FPS drops.",
         [](const PowerState&) { return RegIs(HKEY_CURRENT_USER, kDvr, L"GameDVR_Enabled", 0); },
         [](PowerState&) {
             RegSet(HKEY_CURRENT_USER, kDvr, L"GameDVR_Enabled", 0);
             RegSet(HKEY_CURRENT_USER, kCapture, L"AppCaptureEnabled", 0);
         },
         [](PowerState&) {
             RegSet(HKEY_CURRENT_USER, kDvr, L"GameDVR_Enabled", 1);
             RegSet(HKEY_CURRENT_USER, kCapture, L"AppCaptureEnabled", 1);
         }},
        {L"FPS", L"Game always starts at high priority", L"Windows gives Fortnite CPU time before background apps.",
         [](const PowerState&) { return RegIs(HKEY_LOCAL_MACHINE, kIfeo, L"CpuPriorityClass", 3); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kIfeo, L"CpuPriorityClass", 3); },
         [](PowerState&) { RegDel(HKEY_LOCAL_MACHINE, kIfeo, L"CpuPriorityClass"); }},
        {L"FPS", L"Hardware-accelerated GPU scheduling", L"The GPU manages its own memory, which lowers latency on most newer cards.",
         [](const PowerState&) { return RegIs(HKEY_LOCAL_MACHINE, kGpu, L"HwSchMode", 2); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kGpu, L"HwSchMode", 2); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kGpu, L"HwSchMode", 1); }, true},
        {L"FPS", L"Disable fullscreen optimizations", L"True fullscreen for the game, so less input lag. Start the game once first.",
         [](const PowerState&) { return FsoDisabled(); }, [](PowerState&) { FsoDisable(); },
         [](PowerState&) { FsoEnable(); }},
        {L"Input", L"Raw mouse (no acceleration)", L"Turns off Enhance Pointer Precision so mouse movement is 1:1.",
         [](const PowerState&) { return MouseRaw(); }, [](PowerState&) { SetMouse(0, 0, 0); },
         [](PowerState&) { SetMouse(6, 10, 1); }},
        {L"Input", L"Prioritise games over background tasks", L"The multimedia scheduler reserves less CPU for background work.",
         [](const PowerState&) { return RegIs(HKEY_LOCAL_MACHINE, kMm, L"SystemResponsiveness", 10); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kMm, L"SystemResponsiveness", 10); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kMm, L"SystemResponsiveness", 20); }},
        {L"Ping", L"Turn off network throttling", L"Stops Windows limiting network packets while audio or video plays.",
         [](const PowerState&) { return RegIs(HKEY_LOCAL_MACHINE, kMm, L"NetworkThrottlingIndex", 0xFFFFFFFF); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kMm, L"NetworkThrottlingIndex", 0xFFFFFFFF); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kMm, L"NetworkThrottlingIndex", 10); }},
        {L"Ping", L"Turn off Nagle's algorithm", L"Small TCP packets go out at once. Helps lobby and party traffic; matches use UDP.",
         [](const PowerState&) { return NagleOff(); }, [](PowerState&) { Nagle(true); },
         [](PowerState&) { Nagle(false); }},

        // --- extended FPS pack ---
        {L"FPS", L"Visual effects: best performance",
         L"Turns off window animations, fade, shadows. Frees CPU/GPU when alt-tabbing and in menus.",
         [](const PowerState&) { return RegIs(HKEY_CURRENT_USER, kVisualFx, L"VisualFXSetting", 2); },
         [](PowerState&) {
             RegSet(HKEY_CURRENT_USER, kVisualFx, L"VisualFXSetting", 2);
             RegSet(HKEY_CURRENT_USER, kDesktop, L"UserPreferencesMask", 0x90);
             RegSet(HKEY_CURRENT_USER, kDesktop, L"DragFullWindows", 0);
             RegSet(HKEY_CURRENT_USER, kDesktop, L"MenuShowDelay", 0);
         },
         [](PowerState&) {
             RegSet(HKEY_CURRENT_USER, kVisualFx, L"VisualFXSetting", 0);
             RegSet(HKEY_CURRENT_USER, kDesktop, L"DragFullWindows", 1);
             RegSet(HKEY_CURRENT_USER, kDesktop, L"MenuShowDelay", 400);
         }},
        {L"FPS", L"Transparency effects off",
         L"Kills the glass/blur behind taskbar and start menu. Small GPU load off.",
         [](const PowerState&) { return RegIs(HKEY_CURRENT_USER,
             L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", 0); },
         [](PowerState&) { RegSet(HKEY_CURRENT_USER,
             L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", 0); },
         [](PowerState&) { RegSet(HKEY_CURRENT_USER,
             L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", 1); }},
        {L"FPS", L"Mouse pointer shadow + trails off",
         L"Removes the soft shadow and trails behind the cursor. Zero graphics work for the cursor.",
         [](const PowerState&) { return RegIs(HKEY_CURRENT_USER, kDesktop, L"CursorShadow", 0); },
         [](PowerState&) { RegSet(HKEY_CURRENT_USER, kDesktop, L"CursorShadow", 0);
                           RegSet(HKEY_CURRENT_USER, kMouse,   L"MouseTrails", 0); },
         [](PowerState&) { RegSet(HKEY_CURRENT_USER, kDesktop, L"CursorShadow", 1); }},
        {L"FPS", L"Disable Superfetch / SysMain for the game",
         L"Stops the service that pre-loads apps. Reduces random disk I/O spikes during matches.",
         [](const PowerState&) { return RegIs(HKEY_LOCAL_MACHINE, kPrefetch, L"EnableSuperfetch", 0); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kPrefetch, L"EnableSuperfetch", 0);
                           RegSet(HKEY_LOCAL_MACHINE, kPrefetch, L"EnablePrefetcher", 0); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kPrefetch, L"EnableSuperfetch", 3);
                           RegSet(HKEY_LOCAL_MACHINE, kPrefetch, L"EnablePrefetcher", 3); }},
        {L"FPS", L"Larger system cache for gaming",
         L"Priority for background working set. Smoother frametimes on 16 GB+ machines.",
         [](const PowerState&) { return RegIs(HKEY_LOCAL_MACHINE, kMemMgmt, L"LargeSystemCache", 1); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kMemMgmt, L"LargeSystemCache", 1); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kMemMgmt, L"LargeSystemCache", 0); }},
        {L"FPS", L"GPU priority: high",
         L"Tells Windows scheduler the game's GPU work is top priority. Smoother frames at high load.",
         [](const PowerState&) { return RegIs(HKEY_LOCAL_MACHINE, kMm, L"GPU Priority", 8)
                                    && RegIs(HKEY_LOCAL_MACHINE, kMm, L"Priority", 6); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kMm, L"GPU Priority", 8);
                           RegSet(HKEY_LOCAL_MACHINE, kMm, L"Priority", 6);
                           RegSet(HKEY_LOCAL_MACHINE, kMm, L"Scheduling Category", 0 /* High */); },
         [](PowerState&) { RegDel(HKEY_LOCAL_MACHINE, kMm, L"GPU Priority");
                           RegDel(HKEY_LOCAL_MACHINE, kMm, L"Priority");
                           RegDel(HKEY_LOCAL_MACHINE, kMm, L"Scheduling Category"); }},

        // --- extended Input pack ---
        {L"Input", L"Instant key repeat",
         L"Keyboard starts repeating keys immediately and fast. Edit/build inputs feel snappier.",
         [](const PowerState&) { return RegIs(HKEY_CURRENT_USER, kKeyboard, L"KeyboardDelay", 0); },
         [](PowerState&) { RegSet(HKEY_CURRENT_USER, kKeyboard, L"KeyboardDelay", 0);
                           RegSet(HKEY_CURRENT_USER, kKeyboard, L"KeyboardSpeed", 31); },
         [](PowerState&) { RegSet(HKEY_CURRENT_USER, kKeyboard, L"KeyboardDelay", 1);
                           RegSet(HKEY_CURRENT_USER, kKeyboard, L"KeyboardSpeed", 31); }},
        {L"Input", L"Turn off Sticky/Filter/Toggle keys",
         L"Disables the five-shift accessibility pop-ups that steal focus mid-match.",
         [](const PowerState&) { return RegIs(HKEY_CURRENT_USER,
             L"Control Panel\\Accessibility\\StickyKeys", L"Flags", 506); },
         [](PowerState&) {
             RegSet(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\StickyKeys",  L"Flags", 506);
             RegSet(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\Keyboard Response", L"Flags", 122);
             RegSet(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\ToggleKeys", L"Flags", 58);
         },
         [](PowerState&) {
             RegSet(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\StickyKeys",  L"Flags", 510);
             RegSet(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\Keyboard Response", L"Flags", 126);
             RegSet(HKEY_CURRENT_USER, L"Control Panel\\Accessibility\\ToggleKeys", L"Flags", 62);
         }},

        // --- extended Ping pack ---
        {L"Ping", L"Free the 20% reserved QoS bandwidth",
         L"By default Windows reserves 20% for QoS. Hands it back to the game for upload.",
         [](const PowerState&) { return RegIs(HKEY_LOCAL_MACHINE, kPsched, L"NonBestEffortLimit", 0); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kPsched, L"NonBestEffortLimit", 0); },
         [](PowerState&) { RegDel(HKEY_LOCAL_MACHINE, kPsched, L"NonBestEffortLimit"); }},
        {L"Ping", L"Disable auto-tuning scale",
         L"Stops Windows from dropping the TCP window during congestion. Steadier ping in long matches.",
         [](const PowerState&) { return RegIs(HKEY_LOCAL_MACHINE, kTcpip, L"Tcp1323Opts", 1); },
         [](PowerState&) { RegSet(HKEY_LOCAL_MACHINE, kTcpip, L"Tcp1323Opts", 1);
                           RegSet(HKEY_LOCAL_MACHINE, kTcpip, L"TcpMaxDataRetransmissions", 3); },
         [](PowerState&) { RegDel(HKEY_LOCAL_MACHINE, kTcpip, L"Tcp1323Opts");
                           RegDel(HKEY_LOCAL_MACHINE, kTcpip, L"TcpMaxDataRetransmissions"); }},
    };
    return list;
}

const std::vector<TweakAction>& TweakActions() {
    static const std::vector<TweakAction> list = {
        {L"Ping", L"Flush DNS cache", L"Clears old server addresses Windows remembered.", L"Flush", FlushDns},
        {L"Cleanup", L"Clear temp files", L"Deletes files in your Temp folder that nothing is using.", L"Clean", ClearTemp,
         L"Delete everything in your Temp folder that isn't in use?"},
        {L"Safety", L"Create a restore point", L"A Windows snapshot you can roll back to. Do this before tweaking.",
         L"Create", RestorePoint},
        {L"Cleanup", L"Clear Windows prefetch", L"Deletes old prefetch data so Windows re-learns what you use.",
         L"Clear", ClearPrefetch,
         L"Clear the Windows prefetch folder?"},
        {L"Cleanup", L"Clear GPU shader cache", L"Removes compiled shader caches (NVIDIA + AMD + D3D). First match after is slower.",
         L"Clear", ClearShaderCache,
         L"Clear GPU shader caches? First match or two will stutter while they rebuild."},
        {L"Cleanup", L"Empty the recycle bin", L"Deletes everything in the recycle bin.",
         L"Empty", EmptyRecycleBin, L"Empty the recycle bin?"},
        {L"Cleanup", L"Close background apps", L"Kills Edge, OneDrive, Discord, Teams, Spotify, Xbox Game Bar etc.",
         L"Close", EndBackgroundApps,
         L"Close Edge, OneDrive, Discord, Teams, Spotify, Xbox Game Bar and similar apps?"},
    };
    return list;
}
