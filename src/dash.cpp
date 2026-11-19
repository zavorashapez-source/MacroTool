// dashboard + stats
#include "app.h"
#include "json.h"
#include "monitor.h"
#include <algorithm>
#include <cmath>
#include <cwctype>

// ---------------------------------------------------------------- stats

static SYSTEMTIME LocalAgo(long long seconds)
{
    FILETIME ft, local;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{ft.dwLowDateTime, ft.dwHighDateTime};
    u.QuadPart -= (ULONGLONG)seconds * 10000000ull;
    ft = {u.LowPart, u.HighPart};
    FileTimeToLocalFileTime(&ft, &local);
    SYSTEMTIME st;
    FileTimeToSystemTime(&local, &st);
    return st;
}

std::wstring StatsDayKey(int daysAgo)
{
    SYSTEMTIME st = LocalAgo(daysAgo * 86400ll);
    return Fmt(L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
}

std::wstring StatsHourKey(int hoursAgo)
{
    SYSTEMTIME st = LocalAgo(hoursAgo * 3600ll);
    return Fmt(L"%04d-%02d-%02d %02d", st.wYear, st.wMonth, st.wDay, st.wHour);
}

static std::wstring StatsPath()
{
    return PathJoin(ExeDir(), L"stats.json");
}

void Stats::Add(const std::wstring &macro)
{
    days[StatsDayKey(0)]++;
    hours[StatsHourKey(0)]++;
    macros[macro]++;
    total++;
    dirty = true;
    while (days.size() > 120)
        days.erase(days.begin());
    while (hours.size() > 96)
        hours.erase(hours.begin());
}

void Stats::Load()
{
    std::string bytes;
    json::Value v;
    if (!ReadFileBytes(StatsPath(), bytes) || !json::Parse(bytes, v) || v.t != json::Value::Obj)
        return;
    auto map = [&](const char *key, std::map<std::wstring, int> &out) {
        if (auto *o = v.Get(key); o && o->t == json::Value::Obj)
            for (auto &m : o->o)
                if (m.v.t == json::Value::Num)
                    out[Widen(m.k)] = (int)m.v.n;
    };
    map("Days", days);
    map("Hours", hours);
    map("Macros", macros);
    if (auto *t = v.Get("Total"); t && t->t == json::Value::Num)
        total = (int)t->n;
}

void Stats::Save()
{
    json::Value v = json::Value::Object();
    v.Set("Total", json::Value::Number(total));
    auto map = [&](const char *key, const std::map<std::wstring, int> &in) {
        json::Value o = json::Value::Object();
        for (auto &[k, n] : in)
            o.Set(Narrow(k), json::Value::Number(n));
        v.Set(key, o);
    };
    map("Days", days);
    map("Hours", hours);
    map("Macros", macros);
    WriteFileAtomic(StatsPath(), json::Write(v, true));
    dirty = false;
}

static std::wstring Compact(double n)
{
    if (n >= 10000)
        return Fmt(L"%.1fk", n / 1000.0);
    if (n >= 1000)
        return Fmt(L"%d,%03d", (int)n / 1000, (int)n % 1000);
    return Fmt(L"%d", (int)n);
}

// ---------------------------------------------------------------- shared helpers

// stat tile, first and last are highlighted
void App::StatCard(const Rect &r, const wchar_t *glyph, Color c, const std::wstring &label, const std::wstring &value,
                   const std::wstring &delta, Color deltaC, int i)
{
    bool hi = i == 0 || i == 3;
    float t = RowAppear(i);
    ui.PushAlpha(t);
    ui.PushOffset(0, 14 * (1 - t));
    ID id = HashId(L"stat-" + label);
    Interact it = ui.Press(id, r);
    float sp = ui.Spring(HashId(id, 0xB7), it.held ? 1.f : 0.f, 460.f, 26.f);
    ui.PushScale(1.f - 0.02f * sp, r.Cx(), r.Cy());
    Color fg = hi ? T.onPrimary : T.text;
    Color sub = hi ? WithA(T.onPrimary, 0.6f) : T.dim;
    g.Shadow(r, 14, 22, hi ? 0.30f : 0.18f, 7);
    g.Round(r, 14, hi ? T.primary : Mix(T.panel, T.panel2, 0.5f * it.hoverT));
    if (!hi)
        g.RoundStroke(r, 14, Mix(T.border, T.border2, it.hoverT));
    g.Fill({r.x + 14, r.y + 1, r.w - 28, 1}, WithA(hi ? T.onPrimary : T.text, hi ? 0.12f : 0.05f));
    // corner glyph
    if (glyph)
    {
        ui.PushClip(r);
        g.Text(glyph, {r.R() - 74, r.y + 8, 72, 72}, WithA(hi ? T.onPrimary : c, hi ? 0.10f : 0.09f), F::IconXl);
        ui.PopClip();
    }
    std::wstring nice = label;
    for (size_t k = 1; k < nice.size(); k++)
        nice[k] = (wchar_t)towlower(nice[k]);
    g.Text(nice, {r.x + 22, r.y + 22, r.w - 44, 20}, hi ? fg : Mix(T.dim, T.text, 0.35f), F::Semi);
    F vf = g.TextW(value, F::Value) > r.w - 44 ? F::H2 : F::Value;
    g.Text(value, {r.x + 22, r.y + 48, r.w - 44, 36}, fg, vf);
    g.Text(delta, {r.x + 22, r.y + 88, r.w - 44, 18}, hi ? sub : Mix(sub, deltaC, 0.6f), F::Small);
    ui.PopXf(); // scale
    ui.PopXf(); // offset
    ui.PopAlpha();
}

// ---------------------------------------------------------------- dashboard

void App::PageDashboard(const Rect &area)
{
    ID sid = HashId(L"scroll-Dashboard");
    ui.BeginScroll(sid, {area.x, area.y, area.w + 18, area.h});
    float y = area.y;
    double now = ui.time;

    // values for the selected range, oldest first
    std::vector<int> vals, prev;
    std::vector<std::wstring> labels;
    int n = rangeDays_ == 1 ? 24 : rangeDays_;
    for (int i = n - 1; i >= 0; i--)
    {
        if (rangeDays_ == 1)
        {
            auto it = stats_.hours.find(StatsHourKey(i));
            vals.push_back(it == stats_.hours.end() ? 0 : it->second);
            auto pv = stats_.hours.find(StatsHourKey(i + n));
            prev.push_back(pv == stats_.hours.end() ? 0 : pv->second);
            SYSTEMTIME st = LocalAgo(i * 3600ll);
            labels.push_back(i % 4 == 0 ? Fmt(L"%02d:00", st.wHour) : L"");
        }
        else
        {
            auto it = stats_.days.find(StatsDayKey(i));
            vals.push_back(it == stats_.days.end() ? 0 : it->second);
            auto pv = stats_.days.find(StatsDayKey(i + n));
            prev.push_back(pv == stats_.days.end() ? 0 : pv->second);
            SYSTEMTIME st = LocalAgo(i * 86400ll);
            wchar_t buf[32] = L"";
            if (n <= 7)
                GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"ddd", buf, 32, nullptr);
            else if (i % 5 == 0)
                GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"MMM d", buf, 32, nullptr);
            labels.push_back(buf);
        }
    }
    long long sum = 0, prevSum = 0;
    for (int v : vals)
        sum += v;
    for (int v : prev)
        prevSum += v;

    // ---- stat cards
    int enabled = 0;
    for (auto &m : cfg_.macros)
        enabled += m.enabled;
    std::wstring top = L"—";
    int topN = 0, allN = 0;
    for (auto &[name, c] : stats_.macros)
    {
        allN += c;
        if (c > topN)
        {
            topN = c;
            top = name;
        }
    }
    bool wide = area.w >= 760;
    int cols = wide ? 4 : 2;
    float gap = 20, cw = (area.w - gap * (cols - 1)) / cols, ch = 156;
    std::wstring delta;
    Color dc = T.green;
    if (prevSum > 0)
    {
        double pct = (sum - prevSum) * 100.0 / prevSum;
        delta = Fmt(L"%+.0f%%", pct);
        dc = pct >= 0 ? T.green : T.red;
    }
    else
    {
        delta = sum > 0 ? L"new" : L"";
    }
    int step = Monitor::frameMs.load();
    struct S
    {
        const wchar_t *glyph;
        Color c;
        std::wstring label, value, delta;
        Color dc;
    } cards[4] = {
        {L"", T.accentHi, L"MACROS FIRED", Compact((double)sum), delta, dc},
        {L"", T.green, L"ACTIVE MACROS", Fmt(L"%d / %d", enabled, (int)cfg_.macros.size()),
         issues_.empty() ? L"all good" : Fmt(L"%d issue%s", (int)issues_.size(), issues_.size() == 1 ? L"" : L"s"),
         issues_.empty() ? T.green : T.red},
        {L"", T.amber, L"STEP TIMING", Fmt(L"%d ms", step),
         cfg_.timing == L"FrameHuman" ? L"frame better" : L"frame legacy", T.accent2},
        {L"", T.pink, L"TOP MACRO", top, allN ? Fmt(L"%d%%", topN * 100 / allN) : L"", T.pink},
    };
    for (int i = 0; i < 4; i++)
    {
        Rect r{area.x + (i % cols) * (cw + gap), y + (i / cols) * (ch + gap), cw, ch};
        StatCard(r, cards[i].glyph, cards[i].c, cards[i].label, cards[i].value, cards[i].delta, cards[i].dc, i);
    }
    y += (4 / cols) * (ch + gap) + 6;

    // ---- activity chart + top macros
    float chartW = wide ? area.w * 0.64f - gap / 2 : area.w;
    float rowH = 440;
    {
        float t = RowAppear(4);
        ui.PushAlpha(t);
        ui.PushOffset(0, 16 * (1 - t));
        Rect card{area.x, y, chartW, rowH};
        Card(card);
        CardTitle(card, L"Activity", L"MACROS FIRED", T.accent);
        Rect plot{card.x + 64, card.y + 96, card.w - 96, card.h - 150};
        int maxV = 1;
        for (int v : vals)
            maxV = std::max(maxV, v);
        double rough = maxV * 1.15, mag = std::pow(10.0, std::floor(std::log10(std::max(1.0, rough))));
        double stepV = mag;
        for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
            if (rough / (m * mag) <= 5)
            {
                stepV = m * mag;
                break;
            }
        int lines = std::max(2, (int)std::ceil(rough / stepV));
        double top = stepV * lines;
        for (int i = 0; i <= lines; i++)
        {
            float gy = plot.B() - plot.h * i / (float)lines;
            g.Fill({plot.x, gy, plot.w, 1}, WithA(T.border, i == 0 ? 1.f : 0.6f));
            g.Text(Compact(stepV * i), {card.x + 14, gy - 9, 42, 18}, T.faint, F::Tiny, kRight);
        }
        float grow = ui.reduceMotion ? 1.f : EaseOut((float)(now - pageStart_ - 0.15) / 0.9f);
        if (grow < 1)
            ui.Keep();
        std::vector<D2D1_POINT_2F> pts;
        for (size_t i = 0; i < vals.size(); i++)
        {
            float x = plot.x + (vals.size() > 1 ? plot.w * i / (vals.size() - 1) : plot.w / 2);
            float v = (float)(vals[i] / top) * grow;
            pts.push_back({x, plot.B() - plot.h * v});
            if (!labels[i].empty())
                g.Text(labels[i], {x - 40, plot.B() + 12, 80, 18}, T.faint, F::Tiny, kCenter);
        }
        ui.PushClip({plot.x - 4, plot.y - 8, (plot.w + 8) * std::max(0.02f, grow), plot.h + 10});
        g.SmoothArea(pts, plot.B(), WithA(T.accent, 0.26f), WithA(T.accent, 0.0f));
        g.SmoothLine(pts, WithA(T.accent, 0.22f), 7.f); // glow
        g.SmoothLine(pts, T.accentHi, 2.4f);
        ui.PopClip();
        // hover read-out
        if (ui.Hover({plot.x - 20, plot.y, plot.w + 40, plot.h}))
        {
            float lx = ui.Local().x;
            size_t best = 0;
            for (size_t i = 0; i < pts.size(); i++)
                if (std::fabs(pts[i].x - lx) < std::fabs(pts[best].x - lx))
                    best = i;
            float hx = ui.Spring(HashId(L"chart-x"), pts[best].x, 600.f, 45.f);
            float hy = ui.Spring(HashId(L"chart-y"), pts[best].y, 600.f, 45.f);
            g.Fill({hx - 0.5f, plot.y, 1, plot.h}, WithA(T.accentHi, 0.4f));
            g.Circle(hx, hy, 8, WithA(T.accent, 0.25f));
            g.Circle(hx, hy, 4.5f, T.primary);
            int daysAgo = (int)(vals.size() - 1 - best);
            std::wstring when = rangeDays_ == 1 ? (daysAgo == 0 ? L"This hour" : Fmt(L"%dh ago", daysAgo))
                                : daysAgo == 0  ? L"Today"
                                : daysAgo == 1  ? L"Yesterday"
                                                : Fmt(L"%d days ago", daysAgo);
            std::wstring txt = when + L"  ·  " + Compact(vals[best]) + L" fires";
            float tw = g.TextW(txt, F::SmallSemi) + 24;
            Rect tip{std::clamp(hx - tw / 2, plot.x - 40, plot.R() + 20 - tw), std::max(plot.y - 34, hy - 44), tw, 28};
            g.Shadow(tip, 8, 10, 0.5f, 2);
            g.Round(tip, 8, T.panel3);
            g.Text(txt, tip, T.text, F::SmallSemi, kCenter);
        }
        ui.PopXf();
        ui.PopAlpha();
    }
    if (wide)
    {
        float t = RowAppear(5);
        ui.PushAlpha(t);
        ui.PushOffset(0, 16 * (1 - t));
        Rect card{area.x + chartW + gap, y, area.w - chartW - gap, rowH};
        Card(card);
        CardTitle(card, L"Top Macros");
        std::vector<std::pair<std::wstring, int>> top4(stats_.macros.begin(), stats_.macros.end());
        std::sort(top4.begin(), top4.end(), [](auto &a, auto &b) { return a.second > b.second; });
        int other = 0;
        for (size_t i = 4; i < top4.size(); i++)
            other += top4[i].second;
        if (top4.size() > 4)
            top4.resize(4);
        if (other)
            top4.push_back({L"Other", other});
        const Color shades[5] = {T.accentHi, T.accent, Mix(T.accent, T.panel3, 0.45f), Mix(T.accent, T.panel3, 0.7f),
                                 T.border2};
        float cx = card.Cx(), cy = card.y + 66 + 104, rad = std::min(card.w * 0.28f, 72.f);
        float grow = ui.reduceMotion ? 1.f : EaseOut((float)(now - pageStart_ - 0.25) / 1.0f);
        if (grow < 1)
            ui.Keep();
        if (allN == 0)
        {
            g.CircleStroke(cx, cy, rad, T.border2, 22);
            g.Text(L"No data yet", {cx - 60, cy - 10, 120, 20}, T.dim, F::Small, kCenter);
        }
        else
        {
            float a = -1.5708f, gapA = top4.size() > 1 ? 0.16f : 0.f;
            int hoverI = -1;
            D2D1_POINT_2F m = ui.Local();
            float dist = std::hypot(m.x - cx, m.y - cy);
            float ang = std::atan2(m.y - cy, m.x - cx);
            for (size_t i = 0; i < top4.size(); i++)
            {
                float sweep = 6.2832f * top4[i].second / allN * grow;
                float s = std::max(0.f, sweep - gapA);
                float rel = std::fmod(ang - a + 12.566f, 6.2832f);
                if (ui.inputOn && dist > rad - 20 && dist < rad + 20 && rel <= sweep)
                    hoverI = (int)i;
                float hw = ui.Anim(HashId(L"donut", i), hoverI == (int)i ? 1.f : 0.f, 16.f);
                if (s > 0.01f)
                    g.Arc(cx, cy, rad, a + gapA / 2, s, shades[i], 16.f + 5.f * hw);
                a += sweep;
            }
            std::wstring centre = Compact(allN);
            g.Text(centre, {cx - 70, cy - 20, 140, 30}, T.text, F::Value, kCenter);
            g.Text(L"fires, all time", {cx - 70, cy + 10, 140, 18}, T.dim, F::Small, kCenter);
        }
        float ly = cy + rad + 30;
        for (size_t i = 0; i < top4.size() && ly + 30 < card.B(); i++)
        {
            Rect row{card.x + 24, ly, card.w - 48, 30};
            g.Circle(row.x + 14, row.Cy(), 14, WithA(shades[i], 0.18f));
            g.Circle(row.x + 14, row.Cy(), 5, shades[i]);
            std::wstring name = top4[i].first;
            for (auto &c : name)
                c = (wchar_t)towupper(c);
            g.Text(name, {row.x + 38, row.y, row.w - 100, row.h}, T.dim, F::Caption, kLeft, true);
            g.Text(Fmt(L"%d%%", (int)std::lround(top4[i].second * 100.0 / allN)), {row.R() - 60, row.y, 60, row.h},
                   T.text, F::Semi, kRight);
            ly += 32;
        }
        ui.PopXf();
        ui.PopAlpha();
    }
    y += rowH + gap;

    // ---- recent + health
    {
        float t = RowAppear(6);
        ui.PushAlpha(t);
        ui.PushOffset(0, 16 * (1 - t));
        float h = 72 + std::max(1, std::min(6, (int)recent_.size())) * 50.f + 16;
        Rect card{area.x, y, chartW, h};
        Card(card);
        CardTitle(card, L"Recent activity", L"THIS SESSION", T.green);
        float ry = card.y + 80;
        if (recent_.empty())
            g.Text(L"Nothing has fired yet. Turn a macro on and press its key.", {card.x + 26, ry, card.w - 52, 30},
                   T.dim, F::Body);
        for (size_t i = 0; i < recent_.size() && i < 6; i++)
        {
            auto &r = recent_[i];
            Rect row{card.x + 18, ry, card.w - 36, 44};
            g.Round({row.x, row.y + 6, 32, 32}, 10, WithA(T.accent, 0.14f));
            g.Text(L"", {row.x, row.y + 6, 32, 32}, T.accentHi, F::IconSm, kCenter);
            g.Text(r.name, {row.x + 46, row.y + 4, row.w - 260, 20}, T.text, F::Semi);
            g.Text(L"fired with " + r.key, {row.x + 46, row.y + 23, row.w - 260, 18}, T.dim, F::Small);
            double ago = now - r.at;
            std::wstring a = ago < 5    ? L"just now"
                             : ago < 60 ? Fmt(L"%ds ago", (int)ago)
                                        : Fmt(L"%dm ago", (int)(ago / 60));
            g.Text(a, {row.R() - 120, row.y, 120, row.h}, T.faint, F::Small, kRight);
            ry += 50;
        }
        if (!recent_.empty() && active_)
            ui.WakeIn(1.0);
        if (wide)
        {
            Rect hc{area.x + chartW + gap, y, area.w - chartW - gap, h};
            Card(hc);
            CardTitle(hc, L"Health");
            float hy = hc.y + 84;
            auto line = [&](const wchar_t *glyph, Color c, const std::wstring &text) {
                g.Circle(hc.x + 40, hy + 12, 14, WithA(c, 0.15f));
                g.Text(glyph, {hc.x + 26, hy - 2, 28, 28}, c, F::IconSm, kCenter);
                g.Text(text, {hc.x + 64, hy, hc.w - 84, 24}, T.text, F::Body);
                hy += 42;
            };
            line(cfg_.enabled ? L"" : L"", cfg_.enabled ? T.green : T.amber,
                 cfg_.enabled ? L"Macros are on" : L"Macros are paused");
            line(issues_.empty() ? L"" : L"", issues_.empty() ? T.green : T.red,
                 issues_.empty() ? L"No problems found" : Fmt(L"%d macro(s) need attention", (int)issues_.size()));
            if (hy + 30 < hc.B())
                line(L"", T.amber, TimingLabel());
        }
        y = card.B() + 24;
        ui.PopXf();
        ui.PopAlpha();
    }
    ui.EndScroll(sid, y - area.y);
}
