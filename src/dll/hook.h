#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <map>

// ---- menu mirror ----
struct MirroredItem {
    std::wstring text;
    UINT id = 0;
    UINT type = 0;
    UINT state = 0;
    HMENU sub = NULL;
    HBITMAP hbmp = NULL;
    bool hasText = false;
    bool hasBmp = false;
    bool separator = false;
};

struct MirroredMenu {
    std::vector<MirroredItem> items;
};

// Result of parsing an HMENU into our own model (source-agnostic).
struct MenuItemData {
    std::wstring text;          // cleaned display text (no &, no shortcuts)
    std::wstring rawText;
    std::wstring shortcut;
    UINT id = 0;
    UINT type = 0;
    UINT state = 0;
    HMENU sub = NULL;
    HBITMAP hbmp = NULL;
    bool separator = false;
    bool popup = false;
    bool checked = false;
    bool disabled = false;
    bool defaultItem = false;
    bool radio = false;
    bool thirdParty = false;
    int accelIndex = -1;        // index in text of accelerator char
};

struct MenuData {
    std::vector<MenuItemData> items;
    HMENU hmenu = NULL;
};

// Keeps a mirror of menus as they are built by Explorer / shell extensions.
void MirrorInstall();
void MirrorReset();
bool MirrorLookup(HMENU hmenu, MirroredMenu& out);

// Parse a menu (using mirror data when available, GetMenuItemInfo otherwise).
bool BuildMenuData(HMENU hmenu, MenuData& out, bool verbose);

void InstallHooks();
void UninstallHooks();
DWORD WINAPI InitHookThreadProc(LPVOID param);
bool HookIsInstalled();

// Implemented in menu_ui.cpp
struct CustomMenuResult {
    bool shown = false;
    bool canceled = true;
    UINT id = 0;
    std::wstring pluginCommand;
    bool pluginAdmin = false;
    bool pluginConsole = false;
};
CustomMenuResult ShowCustomMenu(HMENU hmenu, const MenuData& data, int x, int y, HWND owner, UINT flags);
