#pragma once
#include <string>
#include <vector>
#include "hook.h"

// One third-party entry that was actually seen in a real context menu.
struct ObservedItem {
    std::wstring text;      // raw label, accelerators and file names included
    std::wstring norm;      // normalised label used as the stable key
    bool popup = false;     // label opens a submenu (7-Zip, WPS ...)
};

// A group of entries that come from the same shell extension, identified by
// the icon bitmap the extension puts next to its items.  Entries without an
// icon end up in their own single-item group.
struct ObservedProvider {
    std::wstring id;        // "bmp:0123456789abcdef" or "txt:<hash>"
    std::wstring name;      // best-effort label, editable in the settings UI
    std::vector<ObservedItem> items;
    long long lastSeen = 0;
};

struct ObservedDb {
    std::vector<ObservedProvider> providers;
};

// Strip accelerators, shortcuts, quoted file names and trailing "(T)" from a
// menu label so the same entry can be looked up across different selections.
std::wstring NormalizeMenuText(const std::wstring& raw);

// Icon fingerprint of a menu item (average hash over a 16x16 downsample of
// hbmpItem).  Returns 0 when the item has no real bitmap.
unsigned long long HashMenuItemBitmap(HBITMAP bmp);

// Identity of the shell extension that contributed this item.
std::wstring ProviderIdOf(const MenuItemData& it);

// Fold the items of one popup into the observation database, merging by
// provider and remembering the most recent labels.
void ObserveMenuItems(const std::vector<MenuItemData>& items);

bool LoadObservedDb(ObservedDb& db);
std::wstring ObservedDbPath();
