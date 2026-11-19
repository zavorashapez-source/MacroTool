#pragma once
#include "util.h"
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <cstdint>
#include <unordered_map>

template <class T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};
constexpr Color Rgb(uint32_t hex, float a = 1.f) {
    return {((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a};
}
Color Mix(Color a, Color b, float t);
inline Color WithA(Color c, float a) {
    c.a *= a;
    return c;
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float R() const { return x + w; }
    float B() const { return y + h; }
    float Cx() const { return x + w * 0.5f; }
    float Cy() const { return y + h * 0.5f; }
    bool Has(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect Inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect Inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect Move(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    D2D1_RECT_F D() const { return {x, y, x + w, y + h}; }
};

enum class F {
    Body, Semi, Small, SmallSemi, Caption, Title, H2, H3, Mono, MonoSmall, Icon, IconSm, IconLg, IconXl, Logo,
    Hero, Value, Tiny, Count
};

enum { kLeft = 0, kCenter = 1, kRight = 2 };

struct Span {
    UINT32 start, len;
    Color c;
};

class Gfx {
public:
    ID2D1Factory1* factory = nullptr;
    IDWriteFactory1* dw = nullptr;
    IWICImagingFactory* wic = nullptr;
    ID2D1RenderTarget* rt = nullptr;   // where drawing goes right now
    ID2D1DeviceContext* dc = nullptr;  // same target with effects, when available
    float opacity = 1.f;
    float shadowScale = 1.f;  // light themes want softer shadows

    bool Init();
    bool BindHwnd(HWND h);
    bool BindWic(IWICBitmap* bmp, float dpi);
    void ReleaseTarget();
    void Resize(UINT w, UINT h);
    void SetDpi(float dpi);
    float Dpi() const { return dpi_; }
    bool HasTarget() const { return base_ != nullptr; }
    bool Begin();
    bool End();  // false = target lost, caller re-binds
    void Frame();

    void Clear(Color c);
    void Fill(const Rect& r, Color c);
    void Round(const Rect& r, float rad, Color c);
    void RoundStroke(const Rect& r, float rad, Color c, float w = 1.f, bool dashed = false);
    void RoundGrad(const Rect& r, float rad, Color a, Color b, bool vertical = false);
    void RadialGlow(float cx, float cy, float rx, float ry, Color c);
    void Circle(float cx, float cy, float rad, Color c);
    void CircleStroke(float cx, float cy, float rad, Color c, float w = 1.f);
    void Arc(float cx, float cy, float rad, float a0, float sweep, Color c, float w);
    void Line(float x1, float y1, float x2, float y2, Color c, float w = 1.f);
    void Polygon(const D2D1_POINT_2F* pts, int n, Color c);
    void PolygonGrad(const D2D1_POINT_2F* pts, int n, const Rect& box, Color a, Color b);
    void Shadow(const Rect& r, float rad, float size, float alpha, float dy = 0);
    // Charts: a smooth curve through the points, and the area under it down to baseY.
    void SmoothLine(const std::vector<D2D1_POINT_2F>& p, Color c, float w);
    void SmoothArea(const std::vector<D2D1_POINT_2F>& p, float baseY, Color top, Color bottom);

    void Text(std::wstring_view s, const Rect& r, Color c, F f, int align = kLeft, bool vcenter = true,
              bool wrap = false, const Span* spans = nullptr, size_t nspans = 0);
    float TextW(std::wstring_view s, F f);
    float TextH(std::wstring_view s, F f, float maxW);
    IDWriteTextLayout* Layout(std::wstring_view s, F f, float maxW, float maxH, int align, bool vcenter, bool wrap,
                              bool spans = false);
    // Font used for a given role; text boxes need it for their own layouts.
    IDWriteTextFormat* Format(F f) { return fmt_[(int)f]; }
    void DrawLayout(IDWriteTextLayout* l, float x, float y, Color c);
    void ApplySpans(IDWriteTextLayout* l, const Span* spans, size_t n);

    void PushClip(const Rect& r);
    void PopClip();
    void SetTransform(const D2D1_MATRIX_3X2_F& m);

    // Renders into an offscreen copy so the scene behind a dialog can be blurred.
    bool BeginLayer();
    ID2D1Bitmap* EndLayer();
    void DrawBlurred(ID2D1Bitmap* bmp, float amount);

    bool SavePng(IWICBitmap* bmp, const std::wstring& path);
    bool EncodePng(IWICBitmap* bmp, std::vector<uint8_t>& out);

private:
    void SetBrush(Color c);
    ID2D1SolidColorBrush* SpanBrush(Color c);
    ID2D1GradientStopCollection* Stops(Color a, Color b);
    ID2D1PathGeometry* CurvePath(const std::vector<D2D1_POINT_2F>& p, bool closeTo, float baseY);

    ID2D1RenderTarget* base_ = nullptr;
    ID2D1HwndRenderTarget* hwndRt_ = nullptr;
    ID2D1DeviceContext* baseDc_ = nullptr;
    ID2D1BitmapRenderTarget* layer_ = nullptr;
    ID2D1DeviceContext* layerDc_ = nullptr;
    ID2D1Effect* blur_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    ID2D1StrokeStyle* dash_ = nullptr;
    ID2D1StrokeStyle* roundCap_ = nullptr;
    IDWriteTextFormat* fmt_[(int)F::Count] = {};
    IDWriteInlineObject* ellipsis_[(int)F::Count] = {};
    float tracking_[(int)F::Count] = {};
    float dpi_ = 96.f;
    uint64_t frame_ = 0;

    struct CachedLayout {
        IDWriteTextLayout* layout;
        uint64_t used;
    };
    std::unordered_map<std::wstring, CachedLayout> cache_;
    std::unordered_map<uint32_t, ID2D1SolidColorBrush*> spanBrushes_;
    std::unordered_map<uint64_t, ID2D1GradientStopCollection*> stops_;
};
