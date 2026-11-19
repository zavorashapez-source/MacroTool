#pragma once
#include "util.h"
#include <vector>

// The in-game click menu (Insert): one column per category, a row per macro. Like the OSD
// it is our own topmost window and never touches the game. While open it takes focus so the
// cursor is free; closing hands focus back to the game window it came from.
namespace Menu {
struct Item {
    int id = 0;
    std::wstring name, key;
    bool on = false;
};
struct Column {
    std::wstring title, glyph;
    std::vector<Item> items;
};
// A click posts `msg` to `notify` with wParam = macro id, or 0 for the master switch.
void Init(HWND notify, UINT msg);  // UI thread, after the main window exists
void Shutdown();
bool IsOpen();
void Show(const std::vector<Column>& cols, bool master);
void Update(const std::vector<Column>& cols, bool master);  // redraws if open
void Hide();
// Renders the menu to a PNG without showing a window (for verification).
bool RenderPng(const std::vector<Column>& cols, bool master, const std::wstring& path);
}  // namespace Menu
