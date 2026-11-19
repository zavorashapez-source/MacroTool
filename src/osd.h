#pragma once
#include "util.h"

// A tiny on-screen popup shown when macros toggle. It is a separate, always-on-top
// window that this app owns and draws itself — it never injects into or hooks the
// game, so anti-cheat sees only another desktop window. It shows over the game only
// when the game runs borderless/windowed (exclusive fullscreen covers it).
namespace Osd {
void Init();  // call on the UI thread, after the main window exists
void Shutdown();
void SetEnabled(bool on);
void Flash(bool macrosOn);
// Renders the popup content to a PNG without showing a window (for verification).
bool RenderPng(bool macrosOn, const std::wstring& path);
}  // namespace Osd
