#include "gfx.h"
#include <algorithm>
#include <cmath>

Color Mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

static std::wstring PickFamily(IDWriteFontCollection* sys, std::initializer_list<const wchar_t*> names) {
    for (auto n : names) {
        UINT32 idx;
        BOOL exists = FALSE;
        if (sys && SUCCEEDED(sys->FindFamilyName(n, &idx, &exists)) && exists) return n;
    }
    return *(names.end() - 1);
}

bool Gfx::Init() {
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts, (void**)&factory)))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1), (IUnknown**)&dw))) return false;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));

    float dashes[] = {2.5f, 2.5f};
    factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                           D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND, 10.f,
                                                           D2D1_DASH_STYLE_CUSTOM, 0.f),
                               dashes, 2, &dash_);
    factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                           D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                               nullptr, 0, &roundCap_);

    IDWriteFontCollection* sys = nullptr;
    dw->GetSystemFontCollection(&sys);
    std::wstring text = PickFamily(sys, {L"Segoe UI Variable Text", L"Segoe UI"});
    std::wstring display = PickFamily(sys, {L"Segoe UI Variable Display", L"Segoe UI"});
    std::wstring smallFam = PickFamily(sys, {L"Segoe UI Variable Small", L"Segoe UI Variable Text", L"Segoe UI"});
    std::wstring mono = PickFamily(sys, {L"Cascadia Mono", L"Consolas"});
    std::wstring icons = PickFamily(sys, {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"});
    SafeRelease(sys);

    auto make = [&](F f, const std::wstring& fam, float size, DWRITE_FONT_WEIGHT w, float tracking = 0) {
        IDWriteTextFormat*& fmt = fmt_[(int)f];
        dw->CreateTextFormat(fam.c_str(), nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                             L"en-us", &fmt);
        if (!fmt) return;
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        dw->CreateEllipsisTrimmingSign(fmt, &ellipsis_[(int)f]);
        tracking_[(int)f] = tracking;
    };
    make(F::Body, text, 13.f, DWRITE_FONT_WEIGHT_NORMAL);
    make(F::Semi, text, 13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    make(F::Small, smallFam, 12.f, DWRITE_FONT_WEIGHT_NORMAL);
    make(F::SmallSemi, smallFam, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    make(F::Caption, smallFam, 10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, 1.1f);
    make(F::Title, display, 26.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, -0.3f);
    make(F::H2, display, 18.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    make(F::H3, text, 14.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    make(F::Mono, mono, 12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    make(F::MonoSmall, mono, 11.5f, DWRITE_FONT_WEIGHT_NORMAL);
    make(F::Icon, icons, 15.f, DWRITE_FONT_WEIGHT_NORMAL);
    make(F::IconSm, icons, 12.f, DWRITE_FONT_WEIGHT_NORMAL);
    make(F::IconLg, icons, 19.f, DWRITE_FONT_WEIGHT_NORMAL);
    make(F::IconXl, icons, 30.f, DWRITE_FONT_WEIGHT_NORMAL);
    make(F::Logo, display, 15.f, DWRITE_FONT_WEIGHT_BOLD);
    make(F::Hero, display, 22.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, -0.2f);
    make(F::Value, display, 26.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, -0.2f);
    make(F::Tiny, smallFam, 10.5f, DWRITE_FONT_WEIGHT_NORMAL);
    return true;
}

static void AfterBind(ID2D1RenderTarget* rt) {
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

bool Gfx::BindHwnd(HWND h) {
    ReleaseTarget();
    RECT rc;
    GetClientRect(h, &rc);
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                                              dpi_, dpi_);
    auto hp = D2D1::HwndRenderTargetProperties(
        h, D2D1::SizeU((UINT)std::max<LONG>(1, rc.right), (UINT)std::max<LONG>(1, rc.bottom)), D2D1_PRESENT_OPTIONS_NONE);
    if (FAILED(factory->CreateHwndRenderTarget(props, hp, &hwndRt_))) return false;
    base_ = hwndRt_;
    base_->AddRef();
    base_->QueryInterface(&baseDc_);
    base_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_);
    AfterBind(base_);
    rt = base_;
    dc = baseDc_;
    return true;
}

bool Gfx::BindWic(IWICBitmap* bmp, float dpi) {
    ReleaseTarget();
    dpi_ = dpi;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                              dpi, dpi);
    if (FAILED(factory->CreateWicBitmapRenderTarget(bmp, props, &base_))) return false;
    base_->QueryInterface(&baseDc_);
    base_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_);
    AfterBind(base_);
    rt = base_;
    dc = baseDc_;
    return true;
}

void Gfx::ReleaseTarget() {
    for (auto& [k, b] : spanBrushes_) b->Release();
    spanBrushes_.clear();
    for (auto& [k, s] : stops_) s->Release();
    stops_.clear();
    SafeRelease(blur_);
    SafeRelease(layerDc_);
    SafeRelease(layer_);
    SafeRelease(brush_);
    SafeRelease(baseDc_);
    SafeRelease(base_);
    SafeRelease(hwndRt_);
    rt = nullptr;
    dc = nullptr;
}

void Gfx::Resize(UINT w, UINT h) {
    if (hwndRt_) hwndRt_->Resize(D2D1::SizeU(std::max(1u, w), std::max(1u, h)));
    SafeRelease(layerDc_);
    SafeRelease(layer_);
}

void Gfx::SetDpi(float dpi) {
    dpi_ = dpi;
    if (base_) base_->SetDpi(dpi, dpi);
    SafeRelease(layerDc_);
    SafeRelease(layer_);
}

bool Gfx::Begin() {
    if (!base_) return false;
    rt = base_;
    dc = baseDc_;
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    opacity = 1.f;
    return true;
}

bool Gfx::End() {
    HRESULT hr = base_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseTarget();
        return false;
    }
    return true;
}

void Gfx::Frame() {
    frame_++;
    if (frame_ % 120 == 0) {
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->second.used + 240 < frame_) {
                it->second.layout->Release();
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void Gfx::SetBrush(Color c) { brush_->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a * opacity)); }

ID2D1SolidColorBrush* Gfx::SpanBrush(Color c) {
    uint32_t key = ((uint32_t)(c.r * 255) << 16) | ((uint32_t)(c.g * 255) << 8) | (uint32_t)(c.b * 255);
    auto it = spanBrushes_.find(key);
    ID2D1SolidColorBrush* b = nullptr;
    if (it == spanBrushes_.end()) {
        rt->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, 1.f), &b);
        spanBrushes_[key] = b;
    } else {
        b = it->second;
    }
    if (b) b->SetOpacity(c.a * opacity);
    return b;
}

ID2D1GradientStopCollection* Gfx::Stops(Color a, Color b) {
    auto pack = [](Color c) {
        return ((uint64_t)(c.r * 255) << 24) | ((uint64_t)(c.g * 255) << 16) | ((uint64_t)(c.b * 255) << 8) |
               (uint64_t)(c.a * 255);
    };
    uint64_t key = (pack(a) << 32) | pack(b);
    auto it = stops_.find(key);
    if (it != stops_.end()) return it->second;
    D2D1_GRADIENT_STOP st[2] = {{0.f, D2D1::ColorF(a.r, a.g, a.b, a.a)}, {1.f, D2D1::ColorF(b.r, b.g, b.b, b.a)}};
    ID2D1GradientStopCollection* s = nullptr;
    rt->CreateGradientStopCollection(st, 2, &s);
    if (s) stops_[key] = s;
    return s;
}

void Gfx::Clear(Color c) { rt->Clear(D2D1::ColorF(c.r, c.g, c.b, c.a)); }

void Gfx::Fill(const Rect& r, Color c) {
    if (c.a * opacity <= 0.002f) return;
    SetBrush(c);
    rt->FillRectangle(r.D(), brush_);
}

void Gfx::Round(const Rect& r, float rad, Color c) {
    if (c.a * opacity <= 0.002f || r.w <= 0 || r.h <= 0) return;
    SetBrush(c);
    rad = std::min(rad, std::min(r.w, r.h) * 0.5f);
    rt->FillRoundedRectangle(D2D1::RoundedRect(r.D(), rad, rad), brush_);
}

void Gfx::RoundStroke(const Rect& r, float rad, Color c, float w, bool dashed) {
    if (c.a * opacity <= 0.002f) return;
    SetBrush(c);
    Rect s = r.Inset(w * 0.5f);
    rad = std::min(rad, std::min(s.w, s.h) * 0.5f);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(s.D(), rad, rad), brush_, w, dashed ? dash_ : nullptr);
}

void Gfx::RoundGrad(const Rect& r, float rad, Color a, Color b, bool vertical) {
    auto* stops = Stops(a, b);
    if (!stops || r.w <= 0 || r.h <= 0) return;
    ID2D1LinearGradientBrush* gb = nullptr;
    D2D1_POINT_2F p0 = {r.x, r.y}, p1 = vertical ? D2D1_POINT_2F{r.x, r.B()} : D2D1_POINT_2F{r.R(), r.B()};
    rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(p0, p1), stops, &gb);
    if (!gb) return;
    gb->SetOpacity(opacity);
    rad = std::min(rad, std::min(r.w, r.h) * 0.5f);
    rt->FillRoundedRectangle(D2D1::RoundedRect(r.D(), rad, rad), gb);
    gb->Release();
}

void Gfx::RadialGlow(float cx, float cy, float rx, float ry, Color c) {
    Color clear = c;
    clear.a = 0;
    auto* stops = Stops(c, clear);
    if (!stops) return;
    ID2D1RadialGradientBrush* gb = nullptr;
    rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({cx, cy}, {0, 0}, rx, ry), stops, &gb);
    if (!gb) return;
    gb->SetOpacity(opacity);
    rt->FillEllipse(D2D1::Ellipse({cx, cy}, rx, ry), gb);
    gb->Release();
}

void Gfx::Circle(float cx, float cy, float rad, Color c) {
    if (c.a * opacity <= 0.002f) return;
    SetBrush(c);
    rt->FillEllipse(D2D1::Ellipse({cx, cy}, rad, rad), brush_);
}

void Gfx::CircleStroke(float cx, float cy, float rad, Color c, float w) {
    if (c.a * opacity <= 0.002f) return;
    SetBrush(c);
    rt->DrawEllipse(D2D1::Ellipse({cx, cy}, rad, rad), brush_, w);
}

void Gfx::Arc(float cx, float cy, float rad, float a0, float sweep, Color c, float w) {
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geo))) return;
    ID2D1GeometrySink* sink = nullptr;
    geo->Open(&sink);
    D2D1_POINT_2F p0 = {cx + rad * std::cos(a0), cy + rad * std::sin(a0)};
    D2D1_POINT_2F p1 = {cx + rad * std::cos(a0 + sweep), cy + rad * std::sin(a0 + sweep)};
    sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(p1, {rad, rad}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                  sweep > 3.14159f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();
    SetBrush(c);
    rt->DrawGeometry(geo, brush_, w, roundCap_);
    geo->Release();
}

void Gfx::Line(float x1, float y1, float x2, float y2, Color c, float w) {
    if (c.a * opacity <= 0.002f) return;
    SetBrush(c);
    rt->DrawLine({x1, y1}, {x2, y2}, brush_, w, roundCap_);
}

void Gfx::Polygon(const D2D1_POINT_2F* pts, int n, Color c) {
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geo))) return;
    ID2D1GeometrySink* sink = nullptr;
    geo->Open(&sink);
    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(pts + 1, n - 1);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    SetBrush(c);
    rt->FillGeometry(geo, brush_);
    geo->Release();
}

void Gfx::PolygonGrad(const D2D1_POINT_2F* pts, int n, const Rect& box, Color a, Color b) {
    auto* stops = Stops(a, b);
    ID2D1PathGeometry* geo = nullptr;
    if (!stops || FAILED(factory->CreatePathGeometry(&geo))) return;
    ID2D1GeometrySink* sink = nullptr;
    geo->Open(&sink);
    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(pts + 1, n - 1);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    ID2D1LinearGradientBrush* gb = nullptr;
    rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({box.x, box.y}, {box.R(), box.B()}), stops, &gb);
    if (gb) {
        gb->SetOpacity(opacity);
        rt->FillGeometry(geo, gb);
        gb->Release();
    }
    geo->Release();
}

void Gfx::Shadow(const Rect& r, float rad, float size, float alpha, float dy) {
    const int n = 9;
    for (int i = n; i >= 1; i--) {
        float t = (float)i / n;
        float e = size * t;
        float a = alpha * shadowScale * 2.4f / n * (1 - t) * (1 - t);
        Round({r.x - e, r.y - e + dy, r.w + 2 * e, r.h + 2 * e}, rad + e, {0, 0, 0, a});
    }
}

IDWriteTextLayout* Gfx::Layout(std::wstring_view s, F f, float maxW, float maxH, int align, bool vcenter, bool wrap,
                               bool spans) {
    std::wstring key;
    key.reserve(s.size() + 40);
    key.append(s);
    key += Fmt(L"\x1%d|%d|%d|%d%d%d%d", (int)f, (int)(maxW * 4), (int)(maxH * 4), align, vcenter, wrap, spans);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.used = frame_;
        return it->second.layout;
    }
    IDWriteTextLayout* l = nullptr;
    if (!fmt_[(int)f] || FAILED(dw->CreateTextLayout(s.data(), (UINT32)s.size(), fmt_[(int)f], std::max(0.f, maxW),
                                                     std::max(0.f, maxH), &l)))
        return nullptr;
    l->SetTextAlignment(align == kCenter  ? DWRITE_TEXT_ALIGNMENT_CENTER
                        : align == kRight ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                          : DWRITE_TEXT_ALIGNMENT_LEADING);
    l->SetParagraphAlignment(vcenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    l->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!wrap) {
        DWRITE_TRIMMING tr{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        l->SetTrimming(&tr, ellipsis_[(int)f]);
    }
    if (tracking_[(int)f] != 0) {
        IDWriteTextLayout1* l1 = nullptr;
        if (SUCCEEDED(l->QueryInterface(&l1))) {
            l1->SetCharacterSpacing(0, tracking_[(int)f], 0, {0, (UINT32)s.size()});
            l1->Release();
        }
    }
    cache_[key] = {l, frame_};
    return l;
}

void Gfx::Text(std::wstring_view s, const Rect& r, Color c, F f, int align, bool vcenter, bool wrap, const Span* spans,
               size_t nspans) {
    if (s.empty() || r.w <= 1 || c.a * opacity <= 0.002f) return;
    IDWriteTextLayout* l = Layout(s, f, r.w, r.h, align, vcenter, wrap, spans != nullptr);
    if (!l) return;
    for (size_t i = 0; i < nspans; i++) l->SetDrawingEffect(SpanBrush(spans[i].c), {spans[i].start, spans[i].len});
    SetBrush(c);
    rt->DrawTextLayout({r.x, r.y}, l, brush_, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Gfx::ApplySpans(IDWriteTextLayout* l, const Span* spans, size_t n) {
    for (size_t i = 0; i < n; i++) l->SetDrawingEffect(SpanBrush(spans[i].c), {spans[i].start, spans[i].len});
}

void Gfx::DrawLayout(IDWriteTextLayout* l, float x, float y, Color c) {
    SetBrush(c);
    rt->DrawTextLayout({x, y}, l, brush_, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

float Gfx::TextW(std::wstring_view s, F f) {
    if (s.empty()) return 0;
    IDWriteTextLayout* l = Layout(s, f, 100000.f, 100.f, kLeft, false, false);
    DWRITE_TEXT_METRICS m{};
    if (l) l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float Gfx::TextH(std::wstring_view s, F f, float maxW) {
    if (s.empty()) return 0;
    IDWriteTextLayout* l = Layout(s, f, maxW, 100000.f, kLeft, false, true);
    DWRITE_TEXT_METRICS m{};
    if (l) l->GetMetrics(&m);
    return m.height;
}

void Gfx::PushClip(const Rect& r) { rt->PushAxisAlignedClip(r.D(), D2D1_ANTIALIAS_MODE_ALIASED); }
void Gfx::PopClip() { rt->PopAxisAlignedClip(); }
void Gfx::SetTransform(const D2D1_MATRIX_3X2_F& m) { rt->SetTransform(m); }

bool Gfx::BeginLayer() {
    if (!base_) return false;
    D2D1_SIZE_F sz = base_->GetSize();
    if (layer_) {
        D2D1_SIZE_F ls = layer_->GetSize();
        if (std::fabs(ls.width - sz.width) > 0.5f || std::fabs(ls.height - sz.height) > 0.5f) {
            SafeRelease(layerDc_);
            SafeRelease(layer_);
        }
    }
    if (!layer_) {
        if (FAILED(base_->CreateCompatibleRenderTarget(sz, &layer_))) return false;
        layer_->QueryInterface(&layerDc_);
        AfterBind(layer_);
    }
    rt = layer_;
    dc = layerDc_;
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

ID2D1Bitmap* Gfx::EndLayer() {
    rt->EndDraw();
    rt = base_;
    dc = baseDc_;
    ID2D1Bitmap* b = nullptr;
    layer_->GetBitmap(&b);
    return b;
}

void Gfx::DrawBlurred(ID2D1Bitmap* bmp, float amount) {
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    if (dc && amount > 0.25f) {
        if (!blur_) dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur_);
        if (blur_) {
            blur_->SetInput(0, bmp);
            blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, amount);
            blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
            blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
            dc->DrawImage(blur_);
            return;
        }
    }
    rt->DrawBitmap(bmp);
}

bool Gfx::EncodePng(IWICBitmap* bmp, std::vector<uint8_t>& out) {
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return false;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    bool ok = SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr)) &&
              SUCCEEDED(fr->WriteSource(bmp, nullptr)) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
    if (ok) {
        STATSTG st{};
        stream->Stat(&st, STATFLAG_NONAME);
        out.resize((size_t)st.cbSize.QuadPart);
        LARGE_INTEGER zero{};
        stream->Seek(zero, STREAM_SEEK_SET, nullptr);
        ULONG got = 0;
        stream->Read(out.data(), (ULONG)out.size(), &got);
    }
    SafeRelease(fr);
    SafeRelease(enc);
    stream->Release();
    return ok;
}

bool Gfx::SavePng(IWICBitmap* bmp, const std::wstring& path) {
    std::vector<uint8_t> png;
    if (!EncodePng(bmp, png)) return false;
    return WriteFileAtomic(path, std::string(png.begin(), png.end()));
}

ID2D1PathGeometry* Gfx::CurvePath(const std::vector<D2D1_POINT_2F>& p, bool closeTo, float baseY) {
    ID2D1PathGeometry* geo = nullptr;
    if (p.size() < 2 || FAILED(factory->CreatePathGeometry(&geo))) return nullptr;
    ID2D1GeometrySink* sink = nullptr;
    geo->Open(&sink);
    sink->BeginFigure(p[0], closeTo ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    size_t n = p.size();
    auto tangent = [&](size_t i) {
        D2D1_POINT_2F a = p[i == 0 ? 0 : i - 1], b = p[i + 1 >= n ? n - 1 : i + 1];
        float span = (i == 0 || i + 1 >= n) ? 1.f : 2.f;
        return D2D1_POINT_2F{(b.x - a.x) / span, (b.y - a.y) / span};
    };
    for (size_t i = 0; i + 1 < n; i++) {
        D2D1_POINT_2F t0 = tangent(i), t1 = tangent(i + 1);
        D2D1_POINT_2F c1{p[i].x + t0.x / 3, p[i].y + t0.y / 3}, c2{p[i + 1].x - t1.x / 3, p[i + 1].y - t1.y / 3};
        // Keep control points between the two values so the curve never overshoots.
        float lo = std::min(p[i].y, p[i + 1].y), hi = std::max(p[i].y, p[i + 1].y);
        c1.y = std::clamp(c1.y, lo, hi);
        c2.y = std::clamp(c2.y, lo, hi);
        sink->AddBezier(D2D1::BezierSegment(c1, c2, p[i + 1]));
    }
    if (closeTo) {
        sink->AddLine({p.back().x, baseY});
        sink->AddLine({p.front().x, baseY});
    }
    sink->EndFigure(closeTo ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();
    return geo;
}

void Gfx::SmoothLine(const std::vector<D2D1_POINT_2F>& p, Color c, float w) {
    ID2D1PathGeometry* geo = CurvePath(p, false, 0);
    if (!geo) return;
    SetBrush(c);
    rt->DrawGeometry(geo, brush_, w, roundCap_);
    geo->Release();
}

void Gfx::SmoothArea(const std::vector<D2D1_POINT_2F>& p, float baseY, Color top, Color bottom) {
    ID2D1PathGeometry* geo = CurvePath(p, true, baseY);
    auto* stops = Stops(top, bottom);
    if (!geo || !stops) {
        if (geo) geo->Release();
        return;
    }
    float minY = baseY;
    for (auto& q : p) minY = std::min(minY, q.y);
    ID2D1LinearGradientBrush* gb = nullptr;
    rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, minY}, {0, baseY}), stops, &gb);
    if (gb) {
        gb->SetOpacity(opacity);
        rt->FillGeometry(geo, gb);
        gb->Release();
    }
    geo->Release();
}
