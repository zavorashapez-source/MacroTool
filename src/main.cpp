#include "app.h"
#include "license.h"
#include "menu.h"
#include "osd.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <timeapi.h>
#include <algorithm>

int RunSelfTest(const std::wstring& report);

namespace {

const wchar_t kClass[] = L"MacroToolNativeWindow";
constexpr float kMinW = 940, kMinH = 680;

Gfx* gG = nullptr;
UI* gUi = nullptr;
App* gApp = nullptr;
HWND gHwnd = nullptr;
bool gNeedFrame = true;
bool gTracking = false;
UINT gDpi = 96;
UINT_PTR gWakeTimer = 0;

float Scale() { return gDpi / 96.f; }

void Render() {
    if (!gHwnd || IsIconic(gHwnd)) {
        gNeedFrame = false;
        return;
    }
    if (!gG->HasTarget() && !gG->BindHwnd(gHwnd)) return;
    RECT rc;
    GetClientRect(gHwnd, &rc);
    float W = rc.right / Scale(), H = rc.bottom / Scale();
    gG->Begin();
    gUi->Begin(NowSeconds());
    gApp->Frame(W, H);
    gUi->End();
    gG->Frame();
    if (!gG->End()) {
        gNeedFrame = true;  // device lost: rebuild and draw again
        return;
    }
    gNeedFrame = gUi->needFrame;
    if (gWakeTimer) {
        KillTimer(gHwnd, 1);
        gWakeTimer = 0;
    }
    if (!gNeedFrame && gUi->wakeAt > 0) {
        double ms = (gUi->wakeAt - gUi->time) * 1000.0;
        gWakeTimer = SetTimer(gHwnd, 1, (UINT)std::clamp(ms, 10.0, 60000.0), nullptr);
    }
    POINT pt;
    GetCursorPos(&pt);
    if (WindowFromPoint(pt) == gHwnd && SendMessageW(gHwnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y)) == HTCLIENT)
        SetCursor(LoadCursorW(nullptr, gUi->cursor));
}

void Mouse(LPARAM l) {
    gUi->in.mx = GET_X_LPARAM(l) / Scale();
    gUi->in.my = GET_Y_LPARAM(l) / Scale();
}

void Mods() {
    gUi->in.ctrl = GetKeyState(VK_CONTROL) < 0;
    gUi->in.shift = GetKeyState(VK_SHIFT) < 0;
}

void ApplyFrameStyle(HWND h) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    int corner = 2 /*DWMWCP_ROUND*/;
    DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof corner);
    COLORREF border = RGB(0x2A, 0x31, 0x42);
    DwmSetWindowAttribute(h, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof border);
    MARGINS m{0, 0, 0, 1};  // keep dwm shadow
    DwmExtendFrameIntoClientArea(h, &m);
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_CREATE:
            ApplyFrameStyle(h);
            SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
            return 0;
        case WM_NCCALCSIZE:
            if (w) {
                auto* p = (NCCALCSIZE_PARAMS*)l;
                if (IsZoomed(h)) {
                    int f = GetSystemMetricsForDpi(SM_CXFRAME, gDpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, gDpi);
                    p->rgrc[0].left += f;
                    p->rgrc[0].top += f;
                    p->rgrc[0].right -= f;
                    p->rgrc[0].bottom -= f;
                }
                return 0;
            }
            break;
        case WM_NCACTIVATE: return DefWindowProcW(h, msg, w, -1);
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &pt);
            RECT rc;
            GetClientRect(h, &rc);
            int b = (int)(6 * Scale());
            if (!IsZoomed(h)) {
                bool L = pt.x < b, R = pt.x >= rc.right - b, T_ = pt.y < b, B = pt.y >= rc.bottom - b;
                if (T_ && L) return HTTOPLEFT;
                if (T_ && R) return HTTOPRIGHT;
                if (B && L) return HTBOTTOMLEFT;
                if (B && R) return HTBOTTOMRIGHT;
                if (L) return HTLEFT;
                if (R) return HTRIGHT;
                if (T_) return HTTOP;
                if (B) return HTBOTTOM;
            }
            return gApp ? gApp->HitTest(pt.x / Scale(), pt.y / Scale()) : HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)l;
            mm->ptMinTrackSize = {(LONG)(kMinW * Scale()), (LONG)(kMinH * Scale())};
            return 0;
        }
        case WM_DPICHANGED: {
            gDpi = HIWORD(w);
            gG->SetDpi((float)gDpi);
            auto* r = (RECT*)l;
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            gNeedFrame = true;
            return 0;
        }
        case WM_SIZE:
            if (w != SIZE_MINIMIZED) {
                gG->Resize(LOWORD(l), HIWORD(l));
                Render();
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            EndPaint(h, &ps);
            Render();
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, gUi->cursor));
                return TRUE;
            }
            break;
        case WM_ACTIVATE:
            gApp->OnActivate(LOWORD(w) != WA_INACTIVE);
            gNeedFrame = true;
            break;
        case WM_MOUSEMOVE:
            Mouse(l);
            if (!gTracking) {
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                gTracking = true;
            }
            gNeedFrame = true;
            return 0;
        case WM_MOUSELEAVE:
            gTracking = false;
            if (!gUi->in.down) gUi->in.mx = gUi->in.my = -10000;
            gNeedFrame = true;
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            Mouse(l);
            Mods();
            SetCapture(h);
            gUi->in.down = true;
            gUi->in.pressed = true;
            gUi->in.dbl = msg == WM_LBUTTONDBLCLK;
            gNeedFrame = true;
            return 0;
        case WM_LBUTTONUP:
            Mouse(l);
            ReleaseCapture();
            gUi->in.down = false;
            gUi->in.released = true;
            gNeedFrame = true;
            return 0;
        case WM_RBUTTONDOWN:
            gUi->in.rpressed = true;
            gNeedFrame = true;
            return 0;
        case WM_MOUSEWHEEL:
            gUi->in.wheel += GET_WHEEL_DELTA_WPARAM(w) / (float)WHEEL_DELTA;
            gNeedFrame = true;
            return 0;
        case WM_KEYDOWN:
            Mods();
            gUi->in.keys.push_back({(UINT)w, gUi->in.ctrl, gUi->in.shift});
            gNeedFrame = true;
            return 0;
        case WM_CHAR:
            gUi->in.chars += (wchar_t)w;
            gNeedFrame = true;
            return 0;
        case WM_TIMER:
            if (w == 1) {
                KillTimer(h, 1);
                gWakeTimer = 0;
            }
            gNeedFrame = true;
            if (w == 2) Render();
            return 0;
        case WM_ENTERSIZEMOVE: SetTimer(h, 2, 16, nullptr); return 0;
        case WM_EXITSIZEMOVE: KillTimer(h, 2); return 0;
        case WM_APP_ENGINE: gApp->OnEngine(); gNeedFrame = true; return 0;
        case WM_APP_TESTER: gApp->OnTester(); gNeedFrame = true; return 0;
        case WM_APP_MONITOR: gNeedFrame = true; return 0;
        case WM_APP_TASK: gApp->OnTasks(); gNeedFrame = true; return 0;
        case WM_APP_MENU: gApp->OnMenu((int)w); gNeedFrame = true; return 0;
        case WM_SETTINGCHANGE: {
            BOOL anim = TRUE;
            SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
            gUi->reduceMotion = !anim;
            gNeedFrame = true;
            break;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY:
            Osd::Shutdown();
            Menu::Shutdown();
            gApp->Shutdown();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// --shot: render a scene to png, no hooks/config
int RunShot(int argc, wchar_t** argv) {
    std::wstring scenario = argv[2], out = argv[3];
    float W = 1280, H = 820, scale = 1.f, at = 2.5f, mx = -10000, my = -10000;
    for (int i = 4; i + 1 < argc; i += 2) {
        std::wstring k = argv[i];
        float v = (float)_wtof(argv[i + 1]);
        if (k == L"--w") W = v;
        else if (k == L"--h") H = v;
        else if (k == L"--scale") scale = v;
        else if (k == L"--at") at = v;
        else if (k == L"--mx") mx = v;
        else if (k == L"--my") my = v;
    }
    Gfx g;
    if (!g.Init() || !g.wic) return 2;
    UI ui(g);
    App* app = new App(g, ui);
    IWICBitmap* bmp = nullptr;
    if (FAILED(g.wic->CreateBitmap((UINT)(W * scale), (UINT)(H * scale), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapCacheOnLoad, &bmp)))
        return 3;
    if (!g.BindWic(bmp, 96.f * scale)) return 4;
    app->Init(nullptr, true);
    double t0 = NowSeconds();
    ui.Begin(t0);
    ui.End();
    app->PreviewSetup(scenario);
    int frames = std::max(1, (int)(at * 60));
    for (int f = 0; f <= frames; f++) {
        ui.in.mx = mx;
        ui.in.my = my;
        g.Begin();
        ui.Begin(t0 + f / 60.0);
        app->Frame(W, H);
        ui.End();
        g.Frame();
        g.End();
    }
    bool ok = g.SavePng(bmp, out);
    bmp->Release();
    return ok ? 0 : 5;
}

int WriteIcon(const std::wstring& path) {
    Gfx g;
    if (!g.Init() || !g.wic) return 2;
    const UINT sizes[] = {256, 64, 48, 40, 32, 24, 20, 16};
    std::vector<std::vector<uint8_t>> pngs;
    for (UINT s : sizes) {
        IWICBitmap* bmp = nullptr;
        g.wic->CreateBitmap(s, s, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
        if (!bmp || !g.BindWic(bmp, 96.f)) return 3;
        g.Begin();
        g.Clear({0, 0, 0, 0});
        float pad = s >= 48 ? s * 0.04f : 0.f;
        DrawLogo(g, {pad, pad, s - 2 * pad, s - 2 * pad});
        g.End();
        std::vector<uint8_t> png;
        g.EncodePng(bmp, png);
        pngs.push_back(png);
        g.ReleaseTarget();
        bmp->Release();
    }
    std::string ico;
    auto u16 = [&](uint16_t v) { ico.append((const char*)&v, 2); };
    auto u32 = [&](uint32_t v) { ico.append((const char*)&v, 4); };
    u16(0);
    u16(1);
    u16((uint16_t)pngs.size());
    uint32_t offset = 6 + 16 * (uint32_t)pngs.size();
    for (size_t i = 0; i < pngs.size(); i++) {
        ico += (char)(sizes[i] >= 256 ? 0 : sizes[i]);
        ico += (char)(sizes[i] >= 256 ? 0 : sizes[i]);
        ico += (char)0;
        ico += (char)0;
        u16(1);
        u16(32);
        u32((uint32_t)pngs[i].size());
        u32(offset);
        offset += (uint32_t)pngs[i].size();
    }
    for (auto& p : pngs) ico.append(p.begin(), p.end());
    return WriteFileAtomic(path, ico) ? 0 : 4;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int show) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc >= 2 && std::wstring(argv[1]) == L"--selftest") return RunSelfTest(argc >= 3 ? argv[2] : L"");
    if (argc >= 3 && std::wstring(argv[1]) == L"--write-icon") return WriteIcon(argv[2]);
    if (argc >= 4 && std::wstring(argv[1]) == L"--shot") return RunShot(argc, argv);
    // --licensetest <key> <out>
    if (argc >= 4 && std::wstring(argv[1]) == L"--licensetest") {
        LicenseResult r = License::Activate(argv[2]);
        WriteFileAtomic(argv[3], Narrow(r.ok ? L"OK " + r.plan + L" " + std::to_wstring(r.expiry) : L"FAIL " + r.error));
        return r.ok ? 0 : 1;
    }
    // --osd-png <on|off> <path>
    if (argc >= 4 && std::wstring(argv[1]) == L"--osd-png")
        return Osd::RenderPng(std::wstring(argv[2]) == L"on", argv[3]) ? 0 : 1;
    // --menu-png <path> (dummy macros)
    if (argc >= 3 && std::wstring(argv[1]) == L"--menu-png") {
        std::vector<Menu::Column> cols = {
            {kCategories[0].name, kCategories[0].glyph, {{1, L"Fast Edit", L"Mouse 5", true}, {2, L"Reset Edit", L"F", false}}},
            {kCategories[1].name, kCategories[1].glyph,
             {{3, L"Wall + Ramp", L"Q", true}, {4, L"Cone Spam", L"E", true}, {5, L"Floor Ramp Wall", L"", false}}},
            {kCategories[2].name, kCategories[2].glyph, {{6, L"Crouch Spam", L"C", false}}},
            {kCategories[3].name, kCategories[3].glyph, {{7, L"Ping", L"Mouse 4", true}}}};
        return Menu::RenderPng(cols, true, argv[2]) ? 0 : 1;
    }
    // --checksig <timestamp> <sig hex> <body file>
    if (argc >= 5 && std::wstring(argv[1]) == L"--checksig") {
        std::string body;
        ReadFileBytes(argv[4], body);
        std::wstring why;
        bool ok = License::VerifySigned(Narrow(argv[2]), body, Narrow(argv[3]), _wtoi64(argv[2]) / (wcslen(argv[2]) == 13 ? 1000 : 1), &why);
        return ok ? 0 : 1;
    }

    // one instance only, otherwise every macro fires twice
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\MacroTool.Native.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kClass, nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }
    (void)single;

    timeBeginPeriod(1);
    Gfx* g = new Gfx;
    if (!g->Init()) {
        MessageBoxW(nullptr, L"Direct2D isn't available on this PC.", L"Macro Tool", MB_ICONERROR);
        return 1;
    }
    UI* ui = new UI(*g);
    App* app = new App(*g, *ui);  // leaked on purpose, workers may still run on exit
    gG = g;
    gUi = ui;
    gApp = app;
    BOOL anim = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    ui->reduceMotion = !anim;

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x0A, 0x0B, 0x10));
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    gDpi = GetDpiForSystem();
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int ww = std::min((int)(1280 * Scale()), (int)(work.right - work.left) - 40);
    int wh = std::min((int)(820 * Scale()), (int)(work.bottom - work.top) - 40);
    int x = work.left + ((work.right - work.left) - ww) / 2, y = work.top + ((work.bottom - work.top) - wh) / 2;
    HWND h = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"Macro Tool", WS_OVERLAPPEDWINDOW, x, y, ww, wh, nullptr, nullptr, hi,
                             nullptr);
    if (!h) return 1;
    gHwnd = h;
    gDpi = GetDpiForWindow(h);
    g->SetDpi((float)gDpi);
    Osd::Init();
    Menu::Init(h, WM_APP_MENU);
    app->Init(h, false);

    // cloak until the first frame so it doesn't flash
    BOOL cloak = TRUE;
    DwmSetWindowAttribute(h, DWMWA_CLOAK, &cloak, sizeof cloak);
    ShowWindow(h, show == SW_SHOWMINIMIZED ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
    Render();
    cloak = FALSE;
    DwmSetWindowAttribute(h, DWMWA_CLOAK, &cloak, sizeof cloak);

    MSG m;
    for (;;) {
        if (gNeedFrame) {
            bool quit = false;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) {
                    quit = true;
                    break;
                }
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            if (quit) break;
            Render();
        } else {
            if (GetMessageW(&m, nullptr, 0, 0) <= 0) break;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    timeEndPeriod(1);
    return 0;
}
