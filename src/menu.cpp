#include "menu.h"
#include "gfx.h"
#include <algorithm>
#include <cmath>

namespace Menu {
namespace {

// 96 dpi units
constexpr float kPad = 12, kColW = 176, kGap = 10, kBarH = 40, kHeadH = 36, kRowH = 30;
const Color kPanel = Rgb(0x1B1E26, 0.97f), kAccent = Rgb(0x3DDC97), kText = Rgb(0xF2F4F8), kDim = Rgb(0x8A93A6);

Gfx* g = nullptr;
IWICBitmap* wic = nullptr;
HWND wnd = nullptr, notify = nullptr, game = nullptr;
UINT notifyMsg = 0;
HDC memDc = nullptr;
HBITMAP dib = nullptr;
void* dibBits = nullptr;
int pxW = 0, pxH = 0, dpi = 96;
bool ready = false, open = false, tracking = false;

std::vector<Column> cols;
bool master = true;
std::vector<std::pair<Rect, int>> hits;  // rect -> id, 0 = master
int hover = -1;

float LogW() {
    float n = (float)std::max<size_t>(cols.size(), 1);
    return kPad * 2 + n * kColW + (n - 1) * kGap;
}
float LogH() {
    size_t rows = 1;
    for (auto& c : cols) rows = std::max(rows, c.items.size());
    return kPad * 2 + kBarH + kGap + kHeadH + rows * kRowH + 6;
}

void Draw() {
    hits.clear();
    float W = LogW(), H = LogH();
    g->Clear({0, 0, 0, 0});
    // backdrop so clicks between panels don't go to the game
    g->Round({0, 0, W, H}, 16, Rgb(0x0B0C10, 0.55f));

    Rect bar{kPad, kPad, W - 2 * kPad, kBarH};
    g->Round(bar, 10, kPanel);
    g->Text(L"MacroTool", {bar.x + 14, bar.y, 160, bar.h}, kText, F::H3, kLeft);
    g->Text(L"Insert / Esc to close", {bar.x, bar.y, bar.w - 120, bar.h}, kDim, F::Small, kRight);
    Rect sw{bar.R() - 106, bar.y + 7, 98, bar.h - 14};
    Color mc = master ? kAccent : kDim;
    g->Round(sw, sw.h / 2, WithA(mc, hover == 0 ? 0.32f : 0.18f));
    g->RoundStroke(sw, sw.h / 2, WithA(mc, 0.6f), 1.f);
    g->Circle(sw.x + 16, sw.Cy(), 4.f, mc);
    g->Text(master ? L"Macros ON" : L"Macros OFF", {sw.x + 24, sw.y, sw.w - 28, sw.h}, kText, F::SmallSemi, kCenter);
    hits.push_back({sw, 0});

    float x = kPad, y = bar.B() + kGap;
    if (cols.empty()) {
        Rect p{x, y, kColW, kHeadH + kRowH + 6};
        g->Round(p, 10, kPanel);
        g->Text(L"No macros yet", p, kDim, F::Small, kCenter);
    }
    for (auto& c : cols) {
        Rect p{x, y, kColW, kHeadH + c.items.size() * kRowH + 6};
        g->Round(p, 10, kPanel);
        g->Text(c.glyph, {p.x + 8, p.y, 24, kHeadH}, kAccent, F::IconSm, kCenter);
        g->Text(c.title, {p.x + 36, p.y, p.w - 46, kHeadH}, kText, F::Semi, kLeft);
        g->Fill({p.x + 10, p.y + kHeadH - 1, p.w - 20, 1}, Rgb(0xFFFFFF, 0.07f));
        float ry = p.y + kHeadH + 3;
        for (auto& it : c.items) {
            Rect r{p.x + 4, ry, p.w - 8, kRowH - 2};
            bool hv = hover == it.id;
            if (it.on) g->Round(r, 6, WithA(kAccent, hv ? 0.26f : 0.15f));
            else if (hv) g->Round(r, 6, Rgb(0xFFFFFF, 0.06f));
            if (it.on) g->Round({r.x + 1, r.y + 7, 3, r.h - 14}, 1.5f, kAccent);
            float kw = it.key.empty() ? 0 : std::min(56.f, g->TextW(it.key, F::Tiny) + 4);
            g->Text(it.name, {r.x + 12, r.y, r.w - 22 - kw, r.h}, it.on ? kText : kDim, F::Small, kLeft);
            if (kw) g->Text(it.key, {r.R() - 8 - kw, r.y, kw, r.h}, WithA(it.on ? kAccent : kDim, 0.8f), F::Tiny, kRight);
            hits.push_back({r, it.id});
            ry += kRowH;
        }
        x += kColW + kGap;
    }
}

void EnsureSized() {
    int w = (int)std::ceil(LogW() * dpi / 96), h = (int)std::ceil(LogH() * dpi / 96);
    if (w == pxW && h == pxH && dib) return;
    pxW = w;
    pxH = h;
    if (dib) DeleteObject(dib);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = pxW;
    bi.bmiHeader.biHeight = -pxH;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(memDc, &bi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    if (dib) SelectObject(memDc, dib);
    SafeRelease(wic);
    if (g && g->wic) g->wic->CreateBitmap(pxW, pxH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &wic);
}

// draw to wic, copy into the dib (same pbgra layout)
bool Render() {
    EnsureSized();
    if (!g || !wic || !g->BindWic(wic, (float)dpi)) return false;
    g->Begin();
    Draw();
    bool ok = g->End();
    g->ReleaseTarget();
    if (!ok || !dibBits) return false;
    UINT stride = pxW * 4;
    return SUCCEEDED(wic->CopyPixels(nullptr, stride, stride * pxH, (BYTE*)dibBits));
}

void Present() {
    if (!wnd || !Render()) return;
    HMONITOR mon = MonitorFromWindow(game ? game : wnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    POINT dst{(mi.rcMonitor.left + mi.rcMonitor.right - pxW) / 2,
              mi.rcMonitor.top + (mi.rcMonitor.bottom - mi.rcMonitor.top) * 8 / 100};
    SIZE sz{pxW, pxH};
    POINT src{0, 0};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(wnd, screen, &dst, &sz, memDc, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

// only the foreground app may give away focus -> attach to the game's input queue.
// if that still fails, the old alt tap trick.
void TakeFocus() {
    HWND fg = GetForegroundWindow();
    DWORD me = GetCurrentThreadId(), other = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    bool attached = other && other != me && AttachThreadInput(other, me, TRUE);
    SetForegroundWindow(wnd);
    SetFocus(wnd);
    if (attached) AttachThreadInput(other, me, FALSE);
    if (GetForegroundWindow() != wnd) {
        INPUT in[2]{};
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = VK_MENU;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        SetForegroundWindow(wnd);
    }
    ClipCursor(nullptr);  // game might still have the cursor clipped
}

int HitAt(LPARAM l) {
    float s = dpi / 96.f, x = (short)LOWORD(l) / s, y = (short)HIWORD(l) / s;
    for (auto& [r, id] : hits)
        if (r.Has(x, y)) return id;
    return -1;
}

void SetHover(int h) {
    if (h == hover) return;
    hover = h;
    Present();
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_MOUSEMOVE:
            if (!tracking) {
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
                tracking = TrackMouseEvent(&t);
            }
            SetHover(HitAt(l));
            return 0;
        case WM_MOUSELEAVE:
            tracking = false;
            SetHover(-1);
            return 0;
        case WM_LBUTTONDOWN:
            if (int id = HitAt(l); id >= 0 && notify) PostMessageW(notify, notifyMsg, (WPARAM)id, 0);
            return 0;
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) Hide();
            return 0;
        case WM_ACTIVATE:
            // alt-tab / clicked away
            if (LOWORD(w) == WA_INACTIVE && open) {
                open = false;
                game = nullptr;
                ShowWindow(h, SW_HIDE);
            }
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int DpiOf(HWND h) {
    UINT d = h ? GetDpiForWindow(h) : 0;
    if (d < 72 || d > 384) d = GetDpiForSystem();
    return (int)d;
}

bool InitGfx() {
    g = new Gfx();
    memDc = CreateCompatibleDC(nullptr);
    return g->Init() && g->wic && memDc;
}

}  // namespace

void Init(HWND notifyWnd, UINT msg) {
    if (ready) return;
    notify = notifyWnd;
    notifyMsg = msg;
    if (!InitGfx()) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MacroToolMenu";
    RegisterClassW(&wc);
    wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"MacroTool menu",
                          WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    ready = wnd != nullptr;
}

void Shutdown() {
    open = false;
    if (wnd) DestroyWindow(wnd);
    wnd = nullptr;
    if (dib) DeleteObject(dib);
    dib = nullptr;
    if (memDc) DeleteDC(memDc);
    memDc = nullptr;
    SafeRelease(wic);
    if (g) {
        g->ReleaseTarget();
        delete g;
        g = nullptr;
    }
    ready = false;
}

bool IsOpen() { return open; }

void Show(const std::vector<Column>& c, bool on) {
    if (!ready || open) return;
    cols = c;
    master = on;
    hover = -1;
    game = GetForegroundWindow();
    dpi = DpiOf(game);
    Present();
    ShowWindow(wnd, SW_SHOW);
    TakeFocus();
    open = true;
}

void Update(const std::vector<Column>& c, bool on) {
    cols = c;
    master = on;
    if (open) Present();
}

void Hide() {
    if (!open) return;
    open = false;  // before, so WM_ACTIVATE doesn't re-enter
    // we're foreground so this is allowed
    if (game && IsWindow(game)) SetForegroundWindow(game);
    game = nullptr;
    ShowWindow(wnd, SW_HIDE);
}

bool RenderPng(const std::vector<Column>& c, bool on, const std::wstring& path) {
    if (!ready && !InitGfx()) return false;
    cols = c;
    master = on;
    dpi = 96;
    return Render() && g->SavePng(wic, path);
}

}  // namespace Menu
