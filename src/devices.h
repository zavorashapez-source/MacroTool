#pragma once
#include "util.h"
#include <cstdint>
#include <vector>

// what's plugged in, for the Main page. raw input list + the HID name strings.
enum class DevKind
{
    Keyboard,
    Mouse,
    Controller
};

enum class PadStyle
{
    Xbox,
    PlayStation,
    Nintendo
};

struct Device
{
    DevKind kind = DevKind::Keyboard;
    std::wstring brand, model;
    std::wstring link; // USB, Wireless, Bluetooth, Built-in
    uint16_t vid = 0, pid = 0;
};

// blocking, a few ms. external devices first, built-in ones last.
std::vector<Device> ListDevices();

std::wstring BrandFor(uint16_t vid);
PadStyle PadStyleFor(const Device &d);
// friendly controller name from vid/pid, empty if we don't know it
std::wstring PadName(uint16_t vid, uint16_t pid);
// "...VID_046D&PID_C539..." (usb) or "...VID&0002054c_PID&0ce6..." (bluetooth)
bool ParseVidPid(const std::wstring &path, uint16_t &vid, uint16_t &pid);
