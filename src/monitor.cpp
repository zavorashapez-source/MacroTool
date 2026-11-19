#include "monitor.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>
#include <thread>

namespace Monitor {

std::atomic<int> pingMs{-1};
std::atomic<int> frameMs{7};

static std::mutex mu;
static std::wstring status = L"Waiting for game…";
static std::wstring fpsStatus;
static std::wstring cfgTiming, cfgHost;
static HWND notifyWnd = nullptr;
static UINT notifyMsg = 0;
static HANDLE wake = nullptr;
static std::atomic<bool> quit{false};
static std::thread worker;

static const wchar_t kGameExe[] = L"FortniteClient-Win64-Shipping.exe";

int DelayFor(int ping) { return ping >= 20 ? std::min(40, ping / 5) : 0; }

int FrameMsFor(double fpsCap, int refreshHz) {
    double fps = fpsCap > 0 ? fpsCap : (double)std::max(30, refreshHz);
    return (int)std::ceil(1000.0 / fps);
}

double ReadFpsCap(std::string_view ini) {
    size_t pos = 0;
    while (pos <= ini.size()) {
        size_t end = ini.find('\n', pos);
        if (end == std::string_view::npos) end = ini.size();
        std::string_view line = ini.substr(pos, end - pos);
        constexpr std::string_view key = "FrameRateLimit=";
        if (line.substr(0, key.size()) == key) {
            std::string num;
            for (char c : line.substr(key.size())) {
                if ((c >= '0' && c <= '9') || c == '.') num += c;
                else break;
            }
            if (num.empty()) return 0;
            char* e = nullptr;
            double v = strtod(num.c_str(), &e);
            return (e && *e == 0) ? v : 0;
        }
        pos = end + 1;
    }
    return 0;
}

std::string ServerFromLog(std::string_view text) {
    std::string last;
    size_t i = 0;
    while (i < text.size()) {
        size_t a = text.find("Browse: ", i), b = text.find("RemoteAddr: ", i);
        size_t at = std::min(a, b);
        if (at == std::string_view::npos) break;
        size_t p = at + (at == a ? 8 : 12);
        // dotted quad followed by :port
        std::string ip;
        int groups = 0;
        bool ok = true;
        while (groups < 4) {
            size_t s = p;
            while (p < text.size() && p - s < 3 && text[p] >= '0' && text[p] <= '9') p++;
            if (p == s) {
                ok = false;
                break;
            }
            ip.append(text.substr(s, p - s));
            groups++;
            if (groups < 4) {
                if (p >= text.size() || text[p] != '.') {
                    ok = false;
                    break;
                }
                ip += '.';
                p++;
            }
        }
        if (ok && p + 1 < text.size() && text[p] == ':' && text[p + 1] >= '0' && text[p + 1] <= '9' &&
            ip.rfind("127.", 0) != 0)
            last = ip;
        i = at + 1;
    }
    return last;
}

static std::set<DWORD> GamePids() {
    std::set<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (IEquals(pe.szExeFile, kGameExe)) pids.insert(pe.th32ProcessID);
    CloseHandle(snap);
    return pids;
}

static std::wstring LocalAppData() { return KnownFolder(FOLDERID_LocalAppData); }

static std::string ReadLogTail() {
    std::wstring path = LocalAppData() + L"\\FortniteGame\\Saved\\Logs\\FortniteGame.log";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    LARGE_INTEGER off;
    off.QuadPart = std::max(0ll, size.QuadPart - 524288);
    SetFilePointerEx(h, off, nullptr, FILE_BEGIN);
    std::string out((size_t)(size.QuadPart - off.QuadPart), '\0');
    DWORD got = 0;
    if (!out.empty()) ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    out.resize(got);
    return out;
}

static std::vector<std::string> GameServers() {
    auto pids = GamePids();
    if (pids.empty()) return {};
    std::string fromLog = ServerFromLog(ReadLogTail());
    if (!fromLog.empty()) return {fromLog};
    ULONG size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    std::vector<char> buf(size + 1024);
    size = (ULONG)buf.size();
    if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) return {};
    auto* t = (MIB_TCPTABLE_OWNER_PID*)buf.data();
    std::vector<std::string> out;
    for (DWORD i = 0; i < t->dwNumEntries; i++) {
        auto& r = t->table[i];
        if (r.dwState != MIB_TCP_STATE_ESTAB || !pids.count(r.dwOwningPid)) continue;
        in_addr a;
        a.S_un.S_addr = r.dwRemoteAddr;
        char s[32];
        inet_ntop(AF_INET, &a, s, sizeof s);
        if (strncmp(s, "127.", 4) == 0) continue;
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    }
    return out;
}

static int PingOnce(const std::string& host) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return -1;
    IPAddr ip = ((sockaddr_in*)res->ai_addr)->sin_addr.S_un.S_addr;
    freeaddrinfo(res);
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) return -1;
    char data[32] = "MacroTool";
    char reply[sizeof(ICMP_ECHO_REPLY) + sizeof data + 64];
    int rtt = -1;
    if (IcmpSendEcho(h, ip, data, sizeof data, nullptr, reply, sizeof reply, 1000) > 0) {
        auto* r = (ICMP_ECHO_REPLY*)reply;
        if (r->Status == IP_SUCCESS) rtt = (int)r->RoundTripTime;
    }
    IcmpCloseHandle(h);
    return rtt;
}

static void UpdateFps() {
    double cap = 0;
    std::string ini;
    if (ReadFileBytes(LocalAppData() + L"\\FortniteGame\\Saved\\Config\\WindowsClient\\GameUserSettings.ini", ini)) {
        if (ini.size() >= 2 && (unsigned char)ini[0] == 0xFF && (unsigned char)ini[1] == 0xFE)
            ini = Narrow(std::wstring_view((const wchar_t*)(ini.data() + 2), (ini.size() - 2) / 2));
        cap = ReadFpsCap(ini);
    }
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    int hz = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) ? (int)dm.dmDisplayFrequency : 60;
    frameMs = FrameMsFor(cap, hz);
    std::lock_guard lk(mu);
    fpsStatus = cap > 0 ? Fmt(L"Game capped at %.0f fps", cap) : Fmt(L"Uncapped, using monitor %d Hz", hz);
}

static void UpdatePing(const std::wstring& host) {
    std::vector<std::string> hosts = host.empty() ? GameServers() : std::vector<std::string>{Narrow(host)};
    std::wstring st;
    int best = -1;
    if (hosts.empty()) {
        st = L"Game not running";
    } else {
        std::string bestHost;
        for (size_t i = 0; i < hosts.size() && i < 5; i++) {
            int ms = PingOnce(hosts[i]);
            if (ms >= 0 && (best < 0 || ms < best)) {
                best = ms;
                bestHost = hosts[i];
            }
        }
        st = best < 0 ? L"Server doesn't answer ping" : Fmt(L"%d ms to %S", best, bestHost.c_str());
    }
    pingMs = best;
    std::lock_guard lk(mu);
    status = st;
}

static void Loop() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    while (!quit) {
        std::wstring timing, host;
        {
            std::lock_guard lk(mu);
            timing = cfgTiming;
            host = cfgHost;
        }
        if (timing == L"Frame" || timing == L"FrameHuman") UpdateFps();
        if (timing == L"Ping") UpdatePing(host);
        if (notifyWnd) PostMessageW(notifyWnd, notifyMsg, 0, 0);
        WaitForSingleObject(wake, 5000);
    }
    WSACleanup();
}

void SetNotify(HWND wnd, UINT msg) {
    notifyWnd = wnd;
    notifyMsg = msg;
}

void Configure(const std::wstring& timing, const std::wstring& pingHost) {
    {
        std::lock_guard lk(mu);
        cfgTiming = timing;
        cfgHost = pingHost;
    }
    if (timing != L"Frame" && timing != L"FrameHuman" && timing != L"Ping") return;
    if (!worker.joinable()) {
        wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        worker = std::thread(Loop);
    } else {
        SetEvent(wake);
    }
}

void Shutdown() {
    if (!worker.joinable()) return;
    quit = true;
    SetEvent(wake);
    // don't wait for a ping in flight on exit
    worker.detach();
}

std::wstring PingStatus() {
    std::lock_guard lk(mu);
    return status;
}

std::wstring Describe(const std::wstring& timing) {
    std::lock_guard lk(mu);
    if (timing == L"Frame" || timing == L"FrameHuman") {
        std::wstring human = timing == L"FrameHuman" ? L" + human jitter" : L"";
        return fpsStatus.empty() ? L"Reading game FPS…"
                                 : fpsStatus + Fmt(L"  →  %d ms per step%s", frameMs.load(), human.c_str());
    }
    if (timing == L"Ping") {
        int p = pingMs.load();
        return p < 0 ? status + L"  →  zero delay until ping is known"
                     : L"Ping " + status + Fmt(L"  →  %d ms per step", DelayFor(p));
    }
    if (timing == L"Zero") return L"Every step fires instantly.";
    return L"Uses each macro's own Hold and Delay sliders.";
}

}  // namespace Monitor
