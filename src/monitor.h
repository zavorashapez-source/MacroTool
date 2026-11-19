#pragma once
#include "util.h"
#include <atomic>

// Watches the game's FPS cap / server ping for the Frame and Ping timing modes.
namespace Monitor {
extern std::atomic<int> pingMs;   // -1 = unknown
extern std::atomic<int> frameMs;  // one frame at the game's FPS cap

void SetNotify(HWND wnd, UINT msg);
// Starts the background thread on first use and re-checks right away.
void Configure(const std::wstring& timing, const std::wstring& pingHost);
void Shutdown();

std::wstring Describe(const std::wstring& timing);
std::wstring PingStatus();

int DelayFor(int ping);
int FrameMsFor(double fpsCap, int refreshHz);
double ReadFpsCap(std::string_view ini);
std::string ServerFromLog(std::string_view text);
}  // namespace Monitor
