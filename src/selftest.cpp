#include "deflate.h"
#include "devices.h"
#include "json.h"
#include "license.h"
#include "model.h"
#include "monitor.h"
#include "pad.h"
#include "tweaks.h"
#include <Xinput.h>
#include <algorithm>
#include <set>

// ported from the .NET --selftest + new stuff
int RunSelfTest(const std::wstring &reportPath)
{
    std::wstring rep;
    int fails = 0;
    auto check = [&](bool ok, const wchar_t *name) {
        rep += (ok ? L"PASS  " : L"FAIL  ") + std::wstring(name) + L"\r\n";
        if (!ok)
            fails++;
    };
    auto throws = [](auto fn) {
        try
        {
            fn();
        }
        catch (const FormatError &)
        {
            return true;
        }
        return false;
    };

    Config c = Config::Defaults();
    const Binds &b = c.binds;

    std::vector<Step> exp = {{'G', L't', 0}, {VK_LBUTTON, L't', 0}, {0, L'w', 20},
                             {'1', L't', 0}, {VK_LSHIFT, L'd', 0},  {VK_LSHIFT, L'u', 0}};
    bool parsed = false;
    try
    {
        parsed = ParseSteps(L"{Edit}, LButton, wait 20, 1, LShiftKey down, lshiftkey up", b) == exp;
    }
    catch (...)
    {
    }
    check(parsed, L"step parser reads binds, waits, digits, down/up");
    for (auto bad : {L"Nope", L"{Missing}", L"V sideways", L"wait", L"wait x", L"42", L"wait 999999", L"V down now"})
        check(throws([&] { ParseSteps(bad, b); }), (L"rejects '" + std::wstring(bad) + L"'").c_str());
    check(ParseSteps(L" A ,\r\n B ,, ", b).size() == 2, L"blank entries and CRLF are ignored");
    {
        auto adapt = [&](const wchar_t *p, const wchar_t *r = L"") {
            auto ps = ParseSteps(p, b), rs = ParseSteps(r, b);
            AdaptForEditOnRelease(ps, rs, 'G');
            return std::make_pair(ps, rs);
        };
        check(adapt(L"{Edit}, LButton down, wait 5, LButton up, {Edit}").first ==
                  ParseSteps(L"{Edit}, LButton down, wait 5, LButton up", b),
              L"edit on release drops the confirm tap");
        check(adapt(L"{Edit}, RButton down, wait 5, RButton up, {Edit}").first ==
                  ParseSteps(L"{Edit}, RButton down, wait 5, RButton up", b),
              L"reset on release drops the confirm tap");
        check(adapt(L"{Edit}, LButton down", L"LButton up, {Edit}").second == ParseSteps(L"LButton up", b),
              L"edit on release carries edit mode into release steps");
        check(adapt(L"{Wall}, LButton, {Edit}, LButton down, wait 5, LButton up, {Edit}").first ==
                  ParseSteps(L"{Wall}, LButton, {Edit}, LButton down, wait 5, LButton up", b),
              L"edit on release keeps the tap that opens edit mode");
    }
    {
        int vk = 0;
        check(KeyParse(L"PadRB", vk) && vk == kPadFirst + 4 && KeyName(vk) == L"PadRB" && Pretty(L"PadRB") == L"Pad RB",
              L"controller buttons have key names");
        check(KeyParse(L"PadRS", vk) && vk == kPadLast && IsPadKey(vk) && !IsPadKey(VK_LBUTTON),
              L"controller key range");
        check(throws([&] { ParseSteps(L"PadA", b); }), L"macros can't press controller buttons");
        check(Pad::ButtonBits(XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_RIGHT_THUMB, 200, 10) ==
                  (1 << 0 | 1 << 4 | 1 << 6 | 1 << 15),
              L"xinput buttons and triggers map to pad keys");
        uint16_t v = 0, p = 0;
        check(ParseVidPid(L"\\\\?\\HID#VID_046D&PID_C539&MI_01&Col01#8&1d0f#{884b96c3}", v, p) && v == 0x046D &&
                  p == 0xC539,
              L"vid/pid from a usb path");
        check(ParseVidPid(L"\\\\?\\HID#{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&0ce6#9&2", v, p) &&
                  v == 0x054C && p == 0x0CE6,
              L"vid/pid from a bluetooth path");
        check(!ParseVidPid(L"\\\\?\\ACPI#PNP0303#4&2f94427b&0#{884b96c3}", v, p), L"built-in keyboard has no vid/pid");
        Device ds{DevKind::Controller, L"Sony", L"DualSense", L"USB", 0x054C, 0x0CE6};
        Device sw{DevKind::Controller, L"", L"Pro Controller", L"USB", 0x057E, 0x2009};
        Device xb{DevKind::Controller, L"", L"Controller (Xbox One For Windows)", L"USB", 0x045E, 0x02FF};
        check(BrandFor(0x1532) == L"Razer" && BrandFor(0x1234).empty() && PadName(0x054C, 0x0CE6) == L"DualSense" &&
                  PadStyleFor(ds) == PadStyle::PlayStation && PadStyleFor(sw) == PadStyle::Nintendo &&
                  PadStyleFor(xb) == PadStyle::Xbox,
              L"device brands and controller styles");
    }

    bool defaultsOk = true;
    for (auto &m : c.macros)
    {
        try
        {
            ParseSteps(m.steps, b);
            ParseSteps(m.releaseSteps, b);
        }
        catch (...)
        {
            defaultsOk = false;
        }
        if (!IsCategory(m.category))
            defaultsOk = false;
    }
    check(defaultsOk, L"all built-in macros parse and have a category");
    std::set<std::wstring> names;
    for (auto &m : c.macros)
        names.insert(m.name);
    check(names.size() == c.macros.size(), L"built-in macro names are unique");

    check(Pretty(L"XButton2") == L"Mouse 5" && Pretty(L"D3") == L"3" && Pretty(L"") == L"None", L"pretty key names");
    int k = 0;
    check(KeyParse(KeyName(kWheelUp), k) && k == kWheelUp, L"wheel up round-trips");
    check(ParseSteps(L"WheelDown", b)[0].key == kWheelDown, L"wheel down parses");
    check(!KeyParse(L"14", k), L"numbers are not key names");
    check(KeyParse(L"Enter", k) && k == VK_RETURN && KeyName(VK_RETURN) == L"Return", L".NET key aliases");
    bool allKeys = true;
    for (int vk = 1; vk < 255; vk++)
    {
        std::wstring n = KeyName(vk);
        int back = 0;
        if (n != std::to_wstring(vk) && (!KeyParse(n, back) || back != vk))
            allKeys = false;
    }
    check(allKeys, L"every named key round-trips");

    std::set<std::wstring> tweakNames;
    bool groupsOk = true;
    auto inGroups = [](const wchar_t *g) {
        for (auto &x : kTweakGroups)
            if (std::wstring(x.name) == g)
                return true;
        return false;
    };
    for (auto &t : TweakToggles())
    {
        tweakNames.insert(t.name);
        groupsOk &= inGroups(t.group);
    }
    for (auto &a : TweakActions())
    {
        tweakNames.insert(a.name);
        groupsOk &= inGroups(a.group);
    }
    check(tweakNames.size() == TweakToggles().size() + TweakActions().size() && groupsOk,
          L"tweaks are unique and grouped");

    auto ev = RecorderClean({{'G', true, 0},
                             {'G', true, 30},
                             {'G', false, 40},
                             {VK_LBUTTON, true, 100},
                             {'Q', true, 150},
                             {VK_LBUTTON, false, 160},
                             {VK_LBUTTON, true, 900}});
    check(RecorderToSteps(ev, b) ==
              L"{Edit}, wait 60, LButton down, wait 50, {Wall} down, wait 10, LButton up, {Wall} up",
          L"recorder output");

    Macro src;
    src.name = L"Drag";
    src.steps = L"{Edit}, LButton down";
    src.trigger = L"E";
    src.enabled = true;
    src.holdMs = 99999;
    bool share = false;
    try
    {
        Macro m = ShareImport(ShareExport(src));
        share = m.name == L"Drag" && m.steps == L"{Edit}, LButton down" && m.trigger.empty() && !m.enabled &&
                m.category == L"Custom" && m.holdMs == 5000;
    }
    catch (...)
    {
    }
    check(share, L"share code round-trip and clamping");
    for (std::wstring bad :
         {std::wstring(L"hello"), std::wstring(L"MT1:!!!"), std::wstring(L"MT1:") + std::wstring(9000, L'A'),
          std::wstring(L"MT1:AAAA"), std::wstring(L"MT1:")})
        check(throws([&] { ShareImport(bad); }), L"rejects a bad share code");

    // Produced by .NET DeflateStream (dynamic Huffman block) from a Macro JSON.
    const wchar_t *dotnet = L"MT1:PY5BDoJADEWvQrqehdG4YTlK4kJdqBeo8p0QK0OGkmgId7cEcPf7-"
                            L"n7ans78BuXk4ycrykrJ0Y4VIaav0ZFUdTBY1HwXlJRr6uDolqoQkEyx3VXRtBb7"
                            L"UR9cdvSdaqxdNgEzLhBwi0U0cohSniyvV1N_D2G7uNmOcgNWyp8sLZZx2ZvuJT5e_wfmf2IIgrkz_AA";
    bool fromDotnet = false;
    try
    {
        Macro m = ShareImport(dotnet);
        fromDotnet = m.name == L"Box Edit" && m.steps == L"{Edit}, LButton, {Edit}";
    }
    catch (...)
    {
    }
    check(fromDotnet, L"imports a share code made by the .NET version");

    std::string big(20000, 'x');
    for (size_t i = 0; i < big.size(); i++)
        big[i] = "abcabcabd {Edit}, LButton"[i % 25];
    auto packed = Deflate((const uint8_t *)big.data(), big.size());
    std::vector<uint8_t> unpacked;
    check(Inflate(packed.data(), packed.size(), unpacked, 1 << 20) &&
              std::string(unpacked.begin(), unpacked.end()) == big && packed.size() < big.size() / 4,
          L"deflate compresses and inflates back");

    check(Monitor::FrameMsFor(120, 60) == 9 && Monitor::FrameMsFor(0, 144) == 7 && Monitor::FrameMsFor(0, 0) == 34 &&
              Monitor::ReadFpsCap("FrontendFrameRateLimit=120\nFrameRateLimit=240.000000\n") == 240 &&
              Monitor::ReadFpsCap("FrameRateLimit=0.000000") == 0,
          L"frame timing from the game's FPS cap");
    check(Monitor::DelayFor(10) == 0 && Monitor::DelayFor(100) == 20 && Monitor::DelayFor(900) == 40, L"ping delay");
    check(Monitor::ServerFromLog("LogNet: Browse: 10.0.0.1:7777//Game\n[x]LogNet: Close [UNetConnection] RemoteAddr: "
                                 "89.39.105.80:9016, Name") == "89.39.105.80" &&
              Monitor::ServerFromLog("RemoteAddr: 127.0.0.1:80").empty(),
          L"game server from the log");

    bool mouseOk = false;
    try
    {
        auto ms = ParseSteps(L"move 12 -7", b);
        mouseOk = ms.size() == 1 && ms[0].kind == L'm' && ms[0].dx == 12 && ms[0].dy == -7;
    }
    catch (...)
    {
    }
    check(mouseOk, L"move becomes a raw mouse step");
    for (auto bad : {L"move 1", L"turn 90", L"look 30"})
        check(throws([&] { ParseSteps(bad, b); }), (L"rejects '" + std::wstring(bad) + L"'").c_str());
    InputEvent mv{kMouseMove, false, 17};
    mv.dx = 300;
    InputEvent mv2{kMouseMove, false, 400};
    mv2.dx = -50;
    auto rec = RecorderClean({{VK_LMENU, true, 0},
                              {VK_TAB, true, 5},
                              {VK_TAB, false, 9},
                              {VK_LMENU, false, 12},
                              {'G', true, 15},
                              mv,
                              {'G', false, 30},
                              mv2});
    check(RecorderToSteps(rec, b) == L"{Edit} down, move 300 0, wait 13, {Edit} up",
          L"recording drops alt-tab and trailing mouse moves");
    bool noAuto = !IsCategory(L"Auto Build") && !IsCategory(L"Double Edit");
    for (auto &m : c.macros)
        noAuto &= m.steps.find(L"turn") == std::wstring::npos;
    check(noAuto, L"no auto-build or double edit left");

    // Ed25519 against RFC 8032 section 7.1 test vectors
    auto unhex = [](const char *h, std::vector<uint8_t> &out) {
        out.clear();
        for (size_t i = 0; h[i] && h[i + 1]; i += 2)
        {
            auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
            out.push_back((uint8_t)(v(h[i]) * 16 + v(h[i + 1])));
        }
    };
    std::vector<uint8_t> pk, sig, msg;
    unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pk);
    unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595b"
          "be24655141438e7a100b",
          sig);
    check(Ed25519Verify(sig.data(), nullptr, 0, pk.data()), L"ed25519 RFC 8032 test 1");
    unhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", pk);
    unhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb430"
          "2aeeb00d291612bb0c00",
          sig);
    unhex("72", msg);
    check(Ed25519Verify(sig.data(), msg.data(), 1, pk.data()), L"ed25519 RFC 8032 test 2");
    msg[0] ^= 1;
    check(!Ed25519Verify(sig.data(), msg.data(), 1, pk.data()), L"ed25519 rejects a changed message");
    msg[0] ^= 1;
    sig[5] ^= 0x40;
    check(!Ed25519Verify(sig.data(), msg.data(), 1, pk.data()), L"ed25519 rejects a changed signature");
    check(!License::VerifySigned("", "{}", "", 0, nullptr), L"unsigned license answers are rejected");
    check(License::Hwid().size() == 40 && License::Hwid() == License::Hwid(), L"hwid is stable");

    // config JSON round-trip, including non-ASCII text
    Config r = Config::Defaults();
    r.macros[0].name = L"Žlutý “test” \U0001F600";
    r.macros[0].enabled = true;
    r.binds.push_back({L"Extra", L"F13"});
    r.theme = L"Light";
    std::wstring tmp = PathJoin(ExeDir(), L"selftest-config.json");
    bool cfgOk = r.SaveTo(tmp);
    Config back;
    cfgOk = cfgOk && Config::LoadFile(tmp, back, nullptr) && back.macros.size() == r.macros.size() &&
            back.macros[0].name == r.macros[0].name && back.macros[0].enabled && back.binds.back().second == L"F13" &&
            back.theme == L"Light";
    DeleteFileW(tmp.c_str());
    check(cfgOk, L"config saves and loads back");
    json::Value jv;
    check(json::Parse("{\"Name\":\"\\u017d\\ud83d\\ude00\",\"HoldMs\":5}", jv) && jv.Get("name") &&
              Widen(jv.Get("name")->s) == L"Ž\U0001F600",
          L"json escapes and case-insensitive keys");

    rep += fails ? Fmt(L"\r\n%d FAILED\r\n", fails) : L"\r\nALL PASSED\r\n";
    // For checking that .NET's DeflateStream (the old app) can read codes we make.
    Macro sample = Config::Defaults().macros[1];
    rep += L"SAMPLE " + ShareExport(sample) + L"\r\n";
    if (!reportPath.empty())
        WriteFileAtomic(reportPath, Narrow(rep));
    return fails ? 1 : 0;
}
