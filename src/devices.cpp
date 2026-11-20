#include "devices.h"
#include <hidsdi.h>
#include <algorithm>
#include <map>

namespace
{

// hex digits right after `tag`; bluetooth paths pad the vid with a bus prefix, so keep the last 4
bool HexAfter(const std::wstring &s, const wchar_t *tag, uint16_t &out)
{
    size_t p = s.find(tag);
    if (p == std::wstring::npos)
        return false;
    p += wcslen(tag);
    size_t e = p;
    while (e < s.size() && iswxdigit(s[e]))
        e++;
    if (e - p < 4)
        return false;
    out = (uint16_t)wcstoul(s.substr(e - 4, 4).c_str(), nullptr, 16);
    return true;
}

bool Has(const std::wstring &lower, std::initializer_list<const wchar_t *> words)
{
    for (auto *w : words)
        if (lower.find(w) != std::wstring::npos)
            return true;
    return false;
}

std::wstring HidString(HANDLE h, BOOLEAN(__stdcall *get)(HANDLE, PVOID, ULONG))
{
    wchar_t buf[128] = L"";
    if (!get(h, buf, sizeof buf))
        return L"";
    return Trim(buf);
}

struct Group
{
    uint16_t vid = 0, pid = 0;
    bool kb = false, mouse = false, pad = false;
    bool bt = false, builtin = false, touch = false;
    std::wstring product, maker;
};

} // namespace

bool ParseVidPid(const std::wstring &path, uint16_t &vid, uint16_t &pid)
{
    std::wstring l = Lower(path);
    return (HexAfter(l, L"vid_", vid) || HexAfter(l, L"vid&", vid)) &&
           (HexAfter(l, L"pid_", pid) || HexAfter(l, L"pid&", pid));
}

std::wstring BrandFor(uint16_t vid)
{
    static const std::map<uint16_t, const wchar_t *> brands = {
        {0x045E, L"Microsoft"},     {0x046D, L"Logitech"}, {0x1532, L"Razer"},        {0x1B1C, L"Corsair"},
        {0x1038, L"SteelSeries"},   {0x0951, L"HyperX"},   {0x31E3, L"Wooting"},      {0x054C, L"Sony"},
        {0x057E, L"Nintendo"},      {0x0B05, L"ASUS"},     {0x3434, L"Keychron"},     {0x2DC8, L"8BitDo"},
        {0x0738, L"Mad Catz"},      {0x0E6F, L"PDP"},      {0x24C6, L"PowerA"},       {0x20D6, L"PowerA"},
        {0x0F0D, L"Hori"},          {0x146B, L"Nacon"},    {0x3367, L"Endgame Gear"}, {0x1E7D, L"ROCCAT"},
        {0x2516, L"Cooler Master"}, {0x1462, L"MSI"},      {0x05AC, L"Apple"},        {0x413C, L"Dell"},
        {0x17EF, L"Lenovo"},        {0x04F2, L"Chicony"},
    };
    auto it = brands.find(vid);
    return it == brands.end() ? L"" : it->second;
}

std::wstring PadName(uint16_t vid, uint16_t pid)
{
    if (vid == 0x054C)
    {
        if (pid == 0x0CE6)
            return L"DualSense";
        if (pid == 0x0DF2)
            return L"DualSense Edge";
        if (pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0)
            return L"DualShock 4";
    }
    if (vid == 0x057E && pid == 0x2009)
        return L"Switch Pro Controller";
    if (vid == 0x045E)
    {
        if (pid == 0x0B12 || pid == 0x0B13)
            return L"Xbox Series X|S Controller";
        if (pid == 0x02EA || pid == 0x02FD || pid == 0x02E0 || pid == 0x02DD || pid == 0x0B20)
            return L"Xbox One Controller";
        if (pid == 0x02E3 || pid == 0x0B00 || pid == 0x0B05 || pid == 0x0B22)
            return L"Xbox Elite Controller";
        if (pid == 0x028E || pid == 0x0719)
            return L"Xbox 360 Controller";
    }
    return L"";
}

PadStyle PadStyleFor(const Device &d)
{
    std::wstring l = Lower(d.model);
    if (d.vid == 0x054C || Has(l, {L"dualsense", L"dualshock", L"playstation"}))
        return PadStyle::PlayStation;
    if (d.vid == 0x057E || Has(l, {L"switch", L"joy-con"}))
        return PadStyle::Nintendo;
    return PadStyle::Xbox;
}

std::vector<Device> ListDevices()
{
    UINT n = 0;
    if (GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST)) != 0 || !n)
        return {};
    std::vector<RAWINPUTDEVICELIST> list(n);
    n = GetRawInputDeviceList(list.data(), &n, sizeof(RAWINPUTDEVICELIST));
    if (n == (UINT)-1)
        return {};
    list.resize(n);

    // one physical device shows up as several interfaces, so group them by vid/pid
    std::map<uint64_t, Group> groups;
    for (auto &rd : list)
    {
        UINT len = 0;
        GetRawInputDeviceInfoW(rd.hDevice, RIDI_DEVICENAME, nullptr, &len);
        if (!len)
            continue;
        std::wstring path(len, L'\0');
        if (GetRawInputDeviceInfoW(rd.hDevice, RIDI_DEVICENAME, path.data(), &len) == (UINT)-1)
            continue;
        path.resize(wcslen(path.c_str()));
        std::wstring lp = Lower(path);
        if (Has(lp, {L"root#", L"rdp_"}))
            continue; // remote desktop / virtual devices

        RID_DEVICE_INFO info{};
        info.cbSize = sizeof info;
        UINT isz = sizeof info;
        if (GetRawInputDeviceInfoW(rd.hDevice, RIDI_DEVICEINFO, &info, &isz) == (UINT)-1)
            continue;
        bool kb = info.dwType == RIM_TYPEKEYBOARD;
        bool mouse = info.dwType == RIM_TYPEMOUSE;
        bool pad =
            info.dwType == RIM_TYPEHID && info.hid.usUsagePage == 1 && (info.hid.usUsage == 4 || info.hid.usUsage == 5);
        if (!kb && !mouse && !pad)
            continue;

        uint16_t vid = 0, pid = 0;
        bool known = ParseVidPid(path, vid, pid);
        bool builtin = !known && Has(lp, {L"acpi#", L"i2c", L"ven_"});
        if (!known && !builtin)
            continue;
        // built-in keyboard and touchpad have no usb ids, keep them apart by kind
        uint64_t key = known ? ((uint64_t)vid << 16 | pid) : (0xFFFF0000ull | (kb ? 1 : mouse ? 2 : 3));
        Group &gr = groups[key];
        gr.vid = vid;
        gr.pid = pid;
        gr.kb |= kb;
        gr.mouse |= mouse;
        gr.pad |= pad;
        gr.builtin |= builtin;
        gr.bt |= Has(lp, {L"{00001124-", L"{00001812-", L"bth"});
        gr.touch |= builtin && mouse;
        if (gr.product.empty() || gr.maker.empty())
        {
            // no read access needed just for the name strings
            HANDLE h =
                CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                if (gr.product.empty())
                    gr.product = HidString(h, HidD_GetProductString);
                if (gr.maker.empty())
                    gr.maker = HidString(h, HidD_GetManufacturerString);
                CloseHandle(h);
            }
        }
    }

    std::vector<Device> out;
    for (auto &[key, gr] : groups)
    {
        Device d;
        d.vid = gr.vid;
        d.pid = gr.pid;
        std::wstring l = Lower(gr.product);
        // mice often have a keyboard interface (for their macro keys) and keyboards a mouse one,
        // so the name wins, then whatever interface it has alone. receivers end up as mice.
        if (gr.pad || Has(l, {L"controller", L"gamepad", L"joystick"}))
            d.kind = DevKind::Controller;
        else if (Has(l, {L"keyboard", L"kbd", L"keypad"}))
            d.kind = DevKind::Keyboard;
        else if (Has(l, {L"mouse", L"touchpad", L"trackpad"}))
            d.kind = DevKind::Mouse;
        else
            d.kind = gr.kb && !gr.mouse ? DevKind::Keyboard : DevKind::Mouse;

        d.brand = BrandFor(gr.vid);
        if (d.brand.empty() && !gr.maker.empty() && !Has(Lower(gr.maker), {L"microsoft"}))
            d.brand = gr.maker;
        d.model = gr.product;
        if (d.kind == DevKind::Controller)
            if (std::wstring nice = PadName(gr.vid, gr.pid); !nice.empty())
                d.model = nice;
        // "Logitech G915" -> brand Logitech, model G915
        if (!d.brand.empty() && d.model.size() > d.brand.size() && IEquals(d.model.substr(0, d.brand.size()), d.brand))
            d.model = Trim(d.model.substr(d.brand.size()));
        if (gr.builtin)
        {
            d.link = L"Built-in";
            if (d.model.empty())
                d.model = gr.touch ? L"Touchpad" : L"Built-in keyboard";
        }
        else if (gr.bt)
            d.link = L"Bluetooth";
        else if (Has(l, {L"wireless", L"receiver", L"lightspeed", L"hyperspeed", L"dongle", L"2.4"}))
            d.link = L"Wireless";
        else
            d.link = L"USB";
        if (d.model.empty())
            d.model = d.kind == DevKind::Keyboard ? L"Keyboard" : d.kind == DevKind::Mouse ? L"Mouse" : L"Controller";
        if (d.brand.empty())
            d.brand = gr.builtin ? L"Laptop" : L"Unknown brand";
        out.push_back(d);
    }
    std::stable_sort(out.begin(), out.end(), [](const Device &a, const Device &b) {
        return (a.link != L"Built-in") > (b.link != L"Built-in");
    });
    return out;
}
