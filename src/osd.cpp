#include "osd.h"
#include "gfx.h"
#include <algorithm>

namespace Osd
{
namespace
{

constexpr int kLogW = 230, kLogH = 56; // logical size at 96 dpi
constexpr UINT_PTR kTimerId = 1;

Gfx *g = nullptr;
IWICBitmap *wic = nullptr;
HWND wnd = nullptr;
HDC memDc = nullptr;
HBITMAP dib = nullptr;
void *dibBits = nullptr;
int pxW = 0, pxH = 0;
bool enabled = true;
bool ready = false;
double flashStart = 0;

double Now()
{
    return QpcToMs(QpcNow());
}

// draw the pill, copy into the dib
bool RenderInto(bool on)
{
    if (!g || !wic)
        return false;
    if (!g->BindWic(wic, (float)pxW / kLogW * 96.f))
        return false;
    g->Begin();
    g->Clear({0, 0, 0, 0});
    Rect r{6, 4, (float)kLogW - 12, (float)kLogH - 8};
    Color accent = on ? Rgb(0x3DDC97) : Rgb(0x8A93A6);
    g->Round(r, 14, Rgb(0x14161C, 0.94f));
    g->RoundStroke(r, 14, WithA(accent, 0.55f), 1.2f);
    float cx = r.x + 22, cy = r.Cy();
    g->Circle(cx, cy, 5.5f, accent);
    if (on)
        g->Circle(cx, cy, 9.f, WithA(accent, 0.25f));
    g->Text(on ? L"Macros ON" : L"Macros OFF", {r.x + 40, r.y, r.w - 52, r.h}, Rgb(0xF2F4F8), F::H3, kLeft);
    g->Text(L"", {r.R() - 34, r.y, 24, r.h}, WithA(accent, 0.8f), F::IconSm, kCenter);
    bool ok = g->End();
    g->ReleaseTarget();
    if (!ok || !dibBits)
        return false;
    // same byte order, straight copy
    UINT stride = pxW * 4;
    wic->CopyPixels(nullptr, stride, stride * pxH, (BYTE *)dibBits);
    return true;
}

void Blit(int alpha)
{
    if (!wnd || !memDc)
        return;
    HDC screen = GetDC(nullptr);
    SIZE sz{pxW, pxH};
    POINT src{0, 0};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)std::clamp(alpha, 0, 255), AC_SRC_ALPHA};
    UpdateLayeredWindow(wnd, screen, nullptr, &sz, memDc, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

void Position()
{
    HMONITOR mon = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    int cx = (mi.rcMonitor.left + mi.rcMonitor.right) / 2;
    int y = mi.rcMonitor.bottom - pxH - (mi.rcMonitor.bottom - mi.rcMonitor.top) * 9 / 100;
    SetWindowPos(wnd, HWND_TOPMOST, cx - pxW / 2, y, pxW, pxH, SWP_NOACTIVATE);
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TIMER && w == kTimerId)
    {
        double e = Now() - flashStart;
        const double hold = 1200, fade = 450;
        if (e >= hold + fade)
        {
            KillTimer(h, kTimerId);
            ShowWindow(h, SW_HIDE);
        }
        else
        {
            int a = e < hold ? 255 : (int)(255 * (1 - (e - hold) / fade));
            Blit(a);
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int PxFromDpi()
{
    UINT dpi = 96;
    HWND fg = GetForegroundWindow();
    if (fg)
        dpi = GetDpiForWindow(fg);
    if (dpi < 72 || dpi > 384)
        dpi = GetDpiForSystem();
    return (int)dpi;
}

void EnsureSized(int dpi)
{
    int w = kLogW * dpi / 96, h = kLogH * dpi / 96;
    if (w == pxW && h == pxH && dib)
        return;
    pxW = w;
    pxH = h;
    if (dib)
        DeleteObject(dib);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = pxW;
    bi.bmiHeader.biHeight = -pxH; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(memDc, &bi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    if (dib)
        SelectObject(memDc, dib);
    SafeRelease(wic);
    if (g && g->wic)
        g->wic->CreateBitmap(pxW, pxH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &wic);
}

} // namespace

void Init()
{
    if (ready)
        return;
    g = new Gfx();
    if (!g->Init() || !g->wic)
        return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MacroToolOSD";
    RegisterClassW(&wc);
    wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                          wc.lpszClassName, L"", WS_POPUP, 0, 0, kLogW, kLogH, nullptr, nullptr, wc.hInstance, nullptr);
    memDc = CreateCompatibleDC(nullptr);
    EnsureSized(PxFromDpi());
    ready = wnd && memDc && dib;
}

void Shutdown()
{
    if (wnd)
    {
        KillTimer(wnd, kTimerId);
        DestroyWindow(wnd);
        wnd = nullptr;
    }
    if (dib)
        DeleteObject(dib);
    dib = nullptr;
    if (memDc)
        DeleteDC(memDc);
    memDc = nullptr;
    SafeRelease(wic);
    if (g)
    {
        g->ReleaseTarget();
        delete g;
        g = nullptr;
    }
    ready = false;
}

void SetEnabled(bool on)
{
    enabled = on;
    if (!on && wnd)
    {
        KillTimer(wnd, kTimerId);
        ShowWindow(wnd, SW_HIDE);
    }
}

void Flash(bool macrosOn)
{
    if (!ready || !enabled)
        return;
    EnsureSized(PxFromDpi());
    if (!RenderInto(macrosOn))
        return;
    Position();
    flashStart = Now();
    Blit(255);
    ShowWindow(wnd, SW_SHOWNOACTIVATE);
    SetTimer(wnd, kTimerId, 16, nullptr);
}

bool RenderPng(bool macrosOn, const std::wstring &path)
{
    bool temp = !ready;
    if (temp)
    {
        g = new Gfx();
        if (!g->Init() || !g->wic)
            return false;
        memDc = CreateCompatibleDC(nullptr);
        EnsureSized(96);
    }
    if (!RenderInto(macrosOn))
        return false;
    bool ok = g->SavePng(wic, path);
    return ok;
}

} // namespace Osd
