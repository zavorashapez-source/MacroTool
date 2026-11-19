#pragma once
#include "util.h"
#include <vector>

// in-game menu (Insert). own topmost window like the osd, doesn't touch the game.
// takes focus while open so the cursor is free, gives it back to the game on close.
namespace Menu
{
struct Item
{
    int id = 0;
    std::wstring name, key;
    bool on = false;
};
struct Column
{
    std::wstring title, glyph;
    std::vector<Item> items;
};
// clicks post msg to notify, wParam = macro id (0 = master)
void Init(HWND notify, UINT msg); // UI thread, after main window
void Shutdown();
bool IsOpen();
void Show(const std::vector<Column> &cols, bool master);
void Update(const std::vector<Column> &cols, bool master); // redraws if open
void Hide();
// for --menu-png
bool RenderPng(const std::vector<Column> &cols, bool master, const std::wstring &path);
} // namespace Menu
