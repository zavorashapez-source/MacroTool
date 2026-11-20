#include "pad.h"
#include "model.h"
#include <Xinput.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

namespace Pad
{
namespace
{

std::thread th;
std::atomic<bool> run{false};
std::mutex mu;
State cur;
std::atomic<double> watchUntil{0};
DWORD hookTid = 0;
UINT hookMsg = 0;
HWND uiWnd = nullptr;
UINT uiMsg = 0;

double Now()
{
    return QpcToMs(QpcNow());
}

float Axis(SHORT v, SHORT dead)
{
    if (std::abs((int)v) < dead)
        return 0;
    return std::clamp(v / 32767.f, -1.f, 1.f);
}

void ToUi(bool force)
{
    if (uiWnd && (force || Now() < watchUntil.load()))
        PostMessageW(uiWnd, uiMsg, 0, 0);
}

void Battery(int slot, State &st)
{
    XINPUT_BATTERY_INFORMATION bi{};
    if (XInputGetBatteryInformation(slot, BATTERY_DEVTYPE_GAMEPAD, &bi) != ERROR_SUCCESS)
        return;
    st.wired = bi.BatteryType == BATTERY_TYPE_WIRED;
    st.battery = st.wired || bi.BatteryType == BATTERY_TYPE_DISCONNECTED || bi.BatteryType == BATTERY_TYPE_UNKNOWN
                     ? -1
                     : (int)bi.BatteryLevel;
}

void Loop()
{
    int slot = -1;
    double nextScan = 0, nextBattery = 0, lastPost = 0;
    uint16_t prev = 0;
    State st;
    while (run)
    {
        double now = Now();
        if (slot < 0)
        {
            // asking an empty slot is slow, so only look for a new pad once a second
            if (now >= nextScan)
            {
                nextScan = now + 1000;
                for (int i = 0; i < XUSER_MAX_COUNT && slot < 0; i++)
                {
                    XINPUT_STATE s{};
                    if (XInputGetState(i, &s) == ERROR_SUCCESS)
                        slot = i;
                }
                if (slot >= 0)
                {
                    st = State{};
                    st.connected = true;
                    Battery(slot, st);
                    nextBattery = now + 10000;
                    {
                        std::lock_guard lk(mu);
                        cur = st;
                    }
                    ToUi(true);
                }
            }
            Sleep(50);
            continue;
        }
        XINPUT_STATE s{};
        if (XInputGetState(slot, &s) != ERROR_SUCCESS)
        {
            // unplugged: let go of anything that was held
            for (int i = 0; i < 16; i++)
                if (prev & (1 << i))
                    PostThreadMessageW(hookTid, hookMsg, kPadFirst + i, 0);
            prev = 0;
            slot = -1;
            {
                std::lock_guard lk(mu);
                cur = State{};
            }
            ToUi(true);
            continue;
        }
        const XINPUT_GAMEPAD &gp = s.Gamepad;
        uint16_t bits = ButtonBits(gp.wButtons, gp.bLeftTrigger, gp.bRightTrigger);
        uint16_t diff = bits ^ prev;
        for (int i = 0; i < 16; i++)
            if (diff & (1 << i))
                PostThreadMessageW(hookTid, hookMsg, kPadFirst + i, (bits >> i) & 1);
        prev = bits;

        State next = st;
        next.buttons = bits;
        next.lt = gp.bLeftTrigger / 255.f;
        next.rt = gp.bRightTrigger / 255.f;
        next.lx = Axis(gp.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        next.ly = Axis(gp.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        next.rx = Axis(gp.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        next.ry = Axis(gp.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        if (now >= nextBattery)
        {
            Battery(slot, next);
            nextBattery = now + 10000;
        }
        bool moved = diff || std::fabs(next.lx - st.lx) + std::fabs(next.ly - st.ly) > 0.02f ||
                     std::fabs(next.rx - st.rx) + std::fabs(next.ry - st.ry) > 0.02f ||
                     std::fabs(next.lt - st.lt) + std::fabs(next.rt - st.rt) > 0.02f || next.battery != st.battery;
        st = next;
        if (moved)
        {
            {
                std::lock_guard lk(mu);
                cur = st;
            }
            // ~60 repaints a second is plenty for the picture
            if (now - lastPost > 15)
            {
                ToUi(false);
                lastPost = now;
            }
        }
        Sleep(2);
    }
}

} // namespace

uint16_t ButtonBits(uint16_t b, uint8_t lt, uint8_t rt)
{
    // xinput flag for each pad vk, triggers handled below
    static const uint16_t flags[16] = {XINPUT_GAMEPAD_A,
                                       XINPUT_GAMEPAD_B,
                                       XINPUT_GAMEPAD_X,
                                       XINPUT_GAMEPAD_Y,
                                       XINPUT_GAMEPAD_RIGHT_SHOULDER,
                                       XINPUT_GAMEPAD_LEFT_SHOULDER,
                                       0,
                                       0,
                                       XINPUT_GAMEPAD_DPAD_UP,
                                       XINPUT_GAMEPAD_DPAD_DOWN,
                                       XINPUT_GAMEPAD_DPAD_LEFT,
                                       XINPUT_GAMEPAD_DPAD_RIGHT,
                                       XINPUT_GAMEPAD_START,
                                       XINPUT_GAMEPAD_BACK,
                                       XINPUT_GAMEPAD_LEFT_THUMB,
                                       XINPUT_GAMEPAD_RIGHT_THUMB};
    uint16_t bits = 0;
    for (int i = 0; i < 16; i++)
        if (flags[i] && (b & flags[i]))
            bits |= 1 << i;
    if (lt > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
        bits |= 1 << 6;
    if (rt > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
        bits |= 1 << 7;
    return bits;
}

void Start(DWORD hookThread, UINT hMsg, HWND wnd, UINT wMsg)
{
    if (run)
        return;
    hookTid = hookThread;
    hookMsg = hMsg;
    uiWnd = wnd;
    uiMsg = wMsg;
    run = true;
    th = std::thread([] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        Loop();
    });
}

void Stop()
{
    run = false;
    if (th.joinable())
        th.join();
}

State Get()
{
    std::lock_guard lk(mu);
    return cur;
}

void Watch()
{
    watchUntil = Now() + 600;
}

} // namespace Pad
