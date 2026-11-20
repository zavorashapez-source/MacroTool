#pragma once
#include "util.h"
#include <cstdint>

// xinput controller polling. button changes go to the engine's hook thread like keys,
// the Main page reads a snapshot for the live picture.
namespace Pad
{
struct State
{
    bool connected = false;
    uint16_t buttons = 0;                 // bit i = vk kPadFirst + i
    float lt = 0, rt = 0;                 // 0..1
    float lx = 0, ly = 0, rx = 0, ry = 0; // -1..1, y up
    int battery = -1;                     // 0 empty .. 3 full, -1 unknown
    bool wired = false;
};

// xinput buttons + triggers -> bits in the kPadFirst.. vk order
uint16_t ButtonBits(uint16_t xinputButtons, uint8_t lt, uint8_t rt);

// hookMsg goes to hookThread as (vk, down), uiMsg to uiWnd when something changed
void Start(DWORD hookThread, UINT hookMsg, HWND uiWnd, UINT uiMsg);
void Stop();
State Get();
// the UI shows live input right now, so post repaints on changes for a bit
void Watch();
} // namespace Pad
