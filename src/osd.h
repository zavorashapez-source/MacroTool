#pragma once
#include "util.h"

// small on/off popup. separate topmost window we draw ourselves, no hooking/injecting
// into the game. only visible in borderless/windowed.
namespace Osd
{
void Init(); // UI thread, after main window
void Shutdown();
void SetEnabled(bool on);
void Flash(bool macrosOn);
// for --osd-png
bool RenderPng(bool macrosOn, const std::wstring &path);
} // namespace Osd
