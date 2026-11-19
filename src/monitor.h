#pragma once
#include "util.h"
#include <atomic>

// fps cap + server ping for timing
namespace Monitor
{
extern std::atomic<int> pingMs;  // -1 = unknown
extern std::atomic<int> frameMs; // one frame at the game's FPS cap

void SetNotify(HWND wnd, UINT msg);
// starts the thread on first call
void Configure(const std::wstring &timing, const std::wstring &pingHost);
void Shutdown();

std::wstring Describe(const std::wstring &timing);
std::wstring PingStatus();

int DelayFor(int ping);
int FrameMsFor(double fpsCap, int refreshHz);
double ReadFpsCap(std::string_view ini);
std::string ServerFromLog(std::string_view text);
} // namespace Monitor
