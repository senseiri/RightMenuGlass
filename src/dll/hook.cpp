#include "hook.h"
#include "plugin.h"
#include "util.h"
#include "config.h"
#include <vector>
#include <psapi.h>

// ------------------------------------------------------------------
// IAT patching
// ------------------------------------------------------------------
typedef BOOL (WINAPI* PFN_TrackPopupMenuEx)(HMENU, UINT, int, int, HWND, LPTPMPARAMS);
typedef BOOL (WINAPI* PFN_TrackPopupMenu)(HMENU, UINT, int, int, int, HWND, const RECT*);
typedef BOOL (WINAPI* PFN_InsertMenuItemW)(HMENU, UINT, BOOL, LPCMENUITEMINFOW);
typedef BOOL (WINAPI* PFN_AppendMenuW)(HMENU, UINT, UINT_PTR, LPCWSTR);
typedef BOOL (WINAPI* PFN_InsertMenuW)(HMENU, UINT, UINT, UINT_PTR, LPCWSTR);
typedef BOOL (WINAPI* PFN_ModifyMenuW)(HMENU, UINT, UINT, UINT_PTR, LPCWSTR);
typedef BOOL (WINAPI* PFN_SetMenuItemInfoW)(HMENU, UINT, BOOL, LPCMENUITEMINFOW);
typedef BOOL (WINAPI* PFN_DeleteMenu)(HMENU, UINT, UINT);
typedef BOOL (WINAPI* PFN_RemoveMenu)(HMENU, UINT, UINT);
typedef BOOL (WINAPI* PFN_DestroyMenu)(HMENU);

static PFN_TrackPopupMenuEx  g_origTrackPopupMenuEx  = nullptr;
static PFN_TrackPopupMenu    g_origTrackPopupMenu    = nullptr;
static PFN_InsertMenuItemW   g_origInsertMenuItemW   = nullptr;
static PFN_AppendMenuW       g_origAppendMenuW       = nullptr;
static PFN_InsertMenuW       g_origInsertMenuW       = nullptr;
static PFN_ModifyMenuW       g_origModifyMenuW       = nullptr;
static PFN_SetMenuItemInfoW  g_origSetMenuItemInfoW  = nullptr;
static PFN_DeleteMenu        g_origDeleteMenu        = nullptr;
static PFN_RemoveMenu        g_origRemoveMenu        = nullptr;
static PFN_DestroyMenu       g_origDestroyMenu       = nullptr;

// ------------------------------------------------------------------
// Menu mirror (HMENU -> items as inserted)
// ------------------------------------------------------------------
static CRITICAL_SECTION g_mirrorCs;
static bool g_mirrorCsInit = false;
static std::map<HMENU, MirroredMenu> g_mirror;
static volatile LONG g_mirrorCount = 0;

static void MirrorInit() {
    if (!g_mirrorCsInit) {
        InitializeCriticalSection(&g_mirrorCs);
        g_mirrorCsInit = true;
    }
}

void MirrorInstall() { MirrorInit(); }

void MirrorReset() {
    if (!g_mirrorCsInit) return;
    EnterCriticalSection(&g_mirrorCs);
    g_mirror.clear();
    LeaveCriticalSection(&g_mirrorCs);
}

static bool MirrorFindLocked(HMENU hmenu, MirroredMenu** out) {
    auto it = g_mirror.find(hmenu);
    if (it == g_mirror.end()) return false;
    *out = &it->second;
    return true;
}

bool MirrorLookup(HMENU hmenu, MirroredMenu& out) {
    if (!g_mirrorCsInit) return false;
    EnterCriticalSection(&g_mirrorCs);
    MirroredMenu* m = nullptr;
    bool ok = MirrorFindLocked(hmenu, &m);
    if (ok) out = *m;
    LeaveCriticalSection(&g_mirrorCs);
    return ok;
}

static void MirrorEnsureLocked(HMENU hmenu, MirroredMenu** out) {
    auto it = g_mirror.find(hmenu);
    if (it == g_mirror.end()) {
        if (g_mirror.size() > 96) g_mirror.clear();
        it = g_mirror.emplace(hmenu, MirroredMenu()).first;
    }
    *out = &it->second;
}

static std::wstring MirrorTextFrom(const wchar_t* s) {
    if (!s) return L"";
    return s;
}

static void MirrorInsertItemW(LPCMENUITEMINFOW mii) {
    if (!mii) return;
    UINT mask = mii->fMask;
    UINT type = (mask & MIIM_FTYPE) ? mii->fType : 0;
    bool separator = (type & MFT_SEPARATOR) != 0;
    std::wstring text;
    bool hasText = false;
    if ((mask & (MIIM_STRING | MIIM_TYPE)) && mii->dwTypeData) {
        text = mii->dwTypeData;
        hasText = true;
    }
    HMENU sub = (mask & MIIM_SUBMENU) ? mii->hSubMenu : NULL;
    HBITMAP bmp = (mask & MIIM_BITMAP) ? mii->hbmpItem : NULL;
    UINT id = (mask & MIIM_ID) ? mii->wID : 0;
    UINT state = (mask & MIIM_STATE) ? mii->fState : 0;
    if (separator) { text.clear(); hasText = false; }

    // Every insertion needs the menu handle, which is not in MENUITEMINFO;
    // callers pass it to the hook, so MirrorInsertMenuItem handles it.
    (void)sub; (void)bmp; (void)id; (void)state; (void)hasText;
}

// ------------------------------------------------------------------
// Mirror updates
// ------------------------------------------------------------------
static void MirrorInsertMenuItem(HMENU hmenu, UINT item, BOOL byPos, LPCMENUITEMINFOW mii) {
    if (!g_mirrorCsInit || !hmenu || !mii) return;
    EnterCriticalSection(&g_mirrorCs);
    MirroredMenu* m = nullptr;
    MirrorEnsureLocked(hmenu, &m);
    MirroredItem mi;
    UINT mask = mii->fMask;
    UINT type = (mask & MIIM_FTYPE) ? mii->fType : 0;
    mi.separator = (type & MFT_SEPARATOR) != 0;
    if ((mask & (MIIM_STRING | MIIM_TYPE)) && mii->dwTypeData) {
        mi.text = mii->dwTypeData;
        mi.hasText = !mi.separator;
    }
    if (mask & MIIM_SUBMENU) mi.sub = mii->hSubMenu;
    if (mask & MIIM_BITMAP) { mi.hbmp = mii->hbmpItem; mi.hasBmp = true; }
    if (mask & MIIM_ID) mi.id = mii->wID;
    if (mask & MIIM_STATE) mi.state = mii->fState;
    mi.type = type;
    if (byPos) {
        if (item >= m->items.size()) m->items.push_back(mi);
        else m->items.insert(m->items.begin() + item, mi);
    } else {
        // id-based: replace matching id
        bool replaced = false;
        for (auto& it : m->items) if (!it.separator && it.id == mi.id && mi.id != 0) { it = mi; replaced = true; break; }
        if (!replaced) m->items.push_back(mi);
    }
    LeaveCriticalSection(&g_mirrorCs);
}

static void MirrorAppendMenu(HMENU hmenu, UINT flags, UINT_PTR id, LPCWSTR text) {
    if (!g_mirrorCsInit || !hmenu) return;
    EnterCriticalSection(&g_mirrorCs);
    MirroredMenu* m = nullptr;
    MirrorEnsureLocked(hmenu, &m);
    MirroredItem mi;
    mi.type = flags;
    mi.separator = (flags & MF_SEPARATOR) != 0;
    if (!mi.separator) {
        if ((flags & MF_POPUP) && id) {
            mi.sub = (HMENU)id;
        } else {
            mi.id = (UINT)id;
        }
        if (text) { mi.text = text; mi.hasText = true; }
    }
    m->items.push_back(mi);
    LeaveCriticalSection(&g_mirrorCs);
}

static void MirrorInsertMenu(HMENU hmenu, UINT pos, UINT flags, UINT_PTR id, LPCWSTR text, bool byPos) {
    if (!g_mirrorCsInit || !hmenu) return;
    EnterCriticalSection(&g_mirrorCs);
    MirroredMenu* m = nullptr;
    MirrorEnsureLocked(hmenu, &m);
    MirroredItem mi;
    mi.type = flags;
    mi.separator = (flags & MF_SEPARATOR) != 0;
    if (!mi.separator) {
        if ((flags & MF_POPUP) && id) mi.sub = (HMENU)id;
        else mi.id = (UINT)id;
        if (text) { mi.text = text; mi.hasText = true; }
    }
    if (byPos) {
        if (pos >= m->items.size()) m->items.push_back(mi);
        else m->items.insert(m->items.begin() + pos, mi);
    } else {
        bool replaced = false;
        for (auto& it : m->items) if (!it.separator && it.id == mi.id && mi.id != 0) { it = mi; replaced = true; break; }
        if (!replaced) m->items.push_back(mi);
    }
    LeaveCriticalSection(&g_mirrorCs);
}

static void MirrorSetItemInfo(HMENU hmenu, UINT item, BOOL byPos, LPCMENUITEMINFOW mii) {
    if (!g_mirrorCsInit || !hmenu || !mii) return;
    EnterCriticalSection(&g_mirrorCs);
    MirroredMenu* m = nullptr;
    if (!MirrorFindLocked(hmenu, &m)) { LeaveCriticalSection(&g_mirrorCs); return; }
    MirroredItem* dst = nullptr;
    if (byPos) {
        if (item < m->items.size()) dst = &m->items[item];
    } else {
        for (auto& it : m->items) if (!it.separator && it.id == item) { dst = &it; break; }
    }
    if (dst) {
        UINT mask = mii->fMask;
        if ((mask & (MIIM_STRING | MIIM_TYPE)) && mii->dwTypeData) { dst->text = mii->dwTypeData; dst->hasText = true; }
        if (mask & MIIM_SUBMENU) dst->sub = mii->hSubMenu;
        if (mask & MIIM_BITMAP) { dst->hbmp = mii->hbmpItem; dst->hasBmp = true; }
        if (mask & MIIM_FTYPE) dst->type = mii->fType;
        if (mask & MIIM_STATE) dst->state = mii->fState;
        if (mask & MIIM_ID) dst->id = mii->wID;
    }
    LeaveCriticalSection(&g_mirrorCs);
}

static void MirrorDeleteItem(HMENU hmenu, UINT item, bool byPos) {
    if (!g_mirrorCsInit || !hmenu) return;
    EnterCriticalSection(&g_mirrorCs);
    MirroredMenu* m = nullptr;
    if (MirrorFindLocked(hmenu, &m)) {
        if (byPos) {
            if (item < m->items.size()) m->items.erase(m->items.begin() + item);
        } else {
            for (size_t i = 0; i < m->items.size(); ++i) {
                if (!m->items[i].separator && m->items[i].id == item) { m->items.erase(m->items.begin() + i); break; }
            }
        }
    }
    LeaveCriticalSection(&g_mirrorCs);
}

static void MirrorDestroyMenu(HMENU hmenu) {
    if (!g_mirrorCsInit || !hmenu) return;
    EnterCriticalSection(&g_mirrorCs);
    g_mirror.erase(hmenu);
    LeaveCriticalSection(&g_mirrorCs);
}

// ------------------------------------------------------------------
// Menu parsing
// ------------------------------------------------------------------
static std::wstring CleanMenuText(const std::wstring& raw, int* accelIndex) {
    std::wstring out;
    int accel = -1;
    for (size_t i = 0; i < raw.size(); ++i) {
        wchar_t c = raw[i];
        if (c == L'&') {
            if (i + 1 < raw.size() && raw[i + 1] == L'&') { out += L'&'; ++i; }
            else { accel = (int)out.size(); }
        } else if (c == L'\t') {
            // tab: split shortcut later
            out += c;
        } else {
            out += c;
        }
    }
    if (accelIndex) *accelIndex = accel;
    return out;
}

static void SplitShortcut(const std::wstring& text, std::wstring& label, std::wstring& shortcut) {
    size_t tab = text.find(L'\t');
    if (tab == std::wstring::npos) { label = text; shortcut.clear(); return; }
    label = text.substr(0, tab);
    shortcut = text.substr(tab + 1);
}

bool BuildMenuData(HMENU hmenu, MenuData& out, bool verbose) {
    out.hmenu = hmenu;
    out.items.clear();
    if (!hmenu) return false;
    int count = GetMenuItemCount(hmenu);
    if (count < 0) return false;
    MirroredMenu mirror;
    bool hasMirror = MirrorLookup(hmenu, mirror);
    if (verbose) LogW(L"BuildMenuData hmenu=%p count=%d mirror=%d", hmenu, count, (int)hasMirror);

    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW mii;
        ZeroMemory(&mii, sizeof(mii));
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU | MIIM_STRING | MIIM_BITMAP;
        wchar_t buf[512] = {0};
        mii.dwTypeData = buf;
        mii.cch = 511;
        MenuItemData item;
        bool got = GetMenuItemInfoW(hmenu, (UINT)i, TRUE, &mii) != FALSE;
        std::wstring raw;
        if (got) {
            raw = buf;
            item.id = mii.wID;
            item.type = mii.fType;
            item.state = mii.fState;
            item.sub = mii.hSubMenu;
            item.hbmp = mii.hbmpItem;
        }
        if (raw.empty() && hasMirror && i < (int)mirror.items.size()) {
            raw = mirror.items[i].text;
            if (!got) {
                item.id = mirror.items[i].id;
                item.type = mirror.items[i].type;
                item.state = mirror.items[i].state;
                item.sub = mirror.items[i].sub;
                item.hbmp = mirror.items[i].hbmp;
            }
        }
        item.rawText = raw;
        int accel = -1;
        std::wstring cleaned = CleanMenuText(raw, &accel);
        std::wstring label, shortcut;
        SplitShortcut(cleaned, label, shortcut);
        item.text = label;
        item.shortcut = shortcut;
        item.accelIndex = accel;
        item.separator = (item.type & MFT_SEPARATOR) != 0 || (label.empty() && !item.sub && !item.hbmp && !(item.type & MFT_OWNERDRAW));
        item.popup = item.sub != NULL;
        item.checked = (item.state & MFS_CHECKED) != 0;
        item.disabled = (item.state & MFS_DISABLED) != 0 || (item.state & MFS_GRAYED) != 0;
        item.defaultItem = (item.state & MFS_DEFAULT) != 0;
        item.radio = (item.type & MFT_RADIOCHECK) != 0;
        if (verbose) {
            LogW(L"  item[%d] id=%u type=0x%08X state=0x%08X sub=%p bmp=%p text=[%s]", i, item.id, item.type, item.state, item.sub, item.hbmp, raw.c_str());
        }
        out.items.push_back(item);
    }
    return !out.items.empty();
}

// ------------------------------------------------------------------
// Hooked APIs
// ------------------------------------------------------------------
static volatile LONG g_inCustomMenu = 0;
static volatile LONG g_hooksReady = 0;
static Config g_cfg;
static bool g_cfgLoaded = false;

static void EnsureConfig() {
    if (!g_cfgLoaded) { g_cfg = LoadConfig(); g_cfgLoaded = true; }
}

static bool ShouldHandleMenu(HMENU hmenu, UINT flags) {
    if (!hmenu) return false;
    if (InterlockedCompareExchange(&g_inCustomMenu, 1, 0) != 0) return false; // re-entrancy
    if (!g_cfg.enabled) { InterlockedExchange(&g_inCustomMenu, 0); return false; }
    if (GetMenuItemCount(hmenu) <= 0) { InterlockedExchange(&g_inCustomMenu, 0); return false; }
    (void)flags;
    return true;
}

static BOOL HandleMenu(HMENU hmenu, UINT flags, int x, int y, HWND owner) {
    EnsureConfig();
    MenuData data;
    if (!BuildMenuData(hmenu, data, g_cfg.logVerbose)) {
        InterlockedExchange(&g_inCustomMenu, 0);
        return (BOOL)-1; // signal: not handled
    }
    CustomMenuResult res = ShowCustomMenu(hmenu, data, x, y, owner, flags);
    if (!res.shown) {
        InterlockedExchange(&g_inCustomMenu, 0);
        return (BOOL)-1;
    }
    // Menu was displayed by us.
    InterlockedExchange(&g_inCustomMenu, 0);
    if (res.canceled) return 0;
    if (!res.pluginCommand.empty()) {
        std::vector<std::wstring> texts;
        texts.reserve(data.items.size());
        for (const auto& it : data.items) {
            if (!it.text.empty()) texts.push_back(it.text);
        }
        POINT pt = { x, y };
        MenuContext ctx = BuildMenuContext(owner, &pt, texts);
        std::wstring cmd = ExpandCommandVars(res.pluginCommand, ctx);
        LogW(L"plugin action: %s", cmd.c_str());
        RunPluginCommand(cmd, res.pluginAdmin, res.pluginConsole);
        return TRUE;
    }
    if (flags & TPM_RETURNCMD) return (BOOL)res.id;
    if (!(flags & TPM_NONOTIFY) && owner) {
        PostMessageW(owner, WM_COMMAND, MAKEWPARAM(res.id, 0), 0);
    }
    return TRUE;
}

static BOOL WINAPI Hook_TrackPopupMenuEx(HMENU hMenu, UINT uFlags, int x, int y, HWND hWnd, LPTPMPARAMS lptpm) {
    if (g_hooksReady && ShouldHandleMenu(hMenu, uFlags)) {
        BOOL r = HandleMenu(hMenu, uFlags, x, y, hWnd);
        if (r != (BOOL)-1) return r;
    }
    return g_origTrackPopupMenuEx ? g_origTrackPopupMenuEx(hMenu, uFlags, x, y, hWnd, lptpm) : FALSE;
}

static BOOL WINAPI Hook_TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT* prc) {
    if (g_hooksReady && ShouldHandleMenu(hMenu, uFlags)) {
        BOOL r = HandleMenu(hMenu, uFlags, x, y, hWnd);
        if (r != (BOOL)-1) return r;
    }
    return g_origTrackPopupMenu ? g_origTrackPopupMenu(hMenu, uFlags, x, y, nReserved, hWnd, prc) : FALSE;
}

static BOOL WINAPI Hook_InsertMenuItemW(HMENU hMenu, UINT item, BOOL byPos, LPCMENUITEMINFOW mii) {
    MirrorInsertMenuItem(hMenu, item, byPos, mii);
    return g_origInsertMenuItemW ? g_origInsertMenuItemW(hMenu, item, byPos, mii) : FALSE;
}
static BOOL WINAPI Hook_AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR id, LPCWSTR text) {
    MirrorAppendMenu(hMenu, uFlags, id, text);
    return g_origAppendMenuW ? g_origAppendMenuW(hMenu, uFlags, id, text) : FALSE;
}
static BOOL WINAPI Hook_InsertMenuW(HMENU hMenu, UINT pos, UINT uFlags, UINT_PTR id, LPCWSTR text) {
    MirrorInsertMenu(hMenu, pos, uFlags, id, text, true);
    return g_origInsertMenuW ? g_origInsertMenuW(hMenu, pos, uFlags, id, text) : FALSE;
}
static BOOL WINAPI Hook_ModifyMenuW(HMENU hMenu, UINT pos, UINT uFlags, UINT_PTR id, LPCWSTR text) {
    MirrorInsertMenu(hMenu, pos, uFlags, id, text, (uFlags & MF_BYPOSITION) != 0);
    return g_origModifyMenuW ? g_origModifyMenuW(hMenu, pos, uFlags, id, text) : FALSE;
}
static BOOL WINAPI Hook_SetMenuItemInfoW(HMENU hMenu, UINT item, BOOL byPos, LPCMENUITEMINFOW mii) {
    MirrorSetItemInfo(hMenu, item, byPos, mii);
    return g_origSetMenuItemInfoW ? g_origSetMenuItemInfoW(hMenu, item, byPos, mii) : FALSE;
}
static BOOL WINAPI Hook_DeleteMenu(HMENU hMenu, UINT item, UINT flags) {
    MirrorDeleteItem(hMenu, item, (flags & MF_BYPOSITION) != 0);
    return g_origDeleteMenu ? g_origDeleteMenu(hMenu, item, flags) : FALSE;
}
static BOOL WINAPI Hook_RemoveMenu(HMENU hMenu, UINT item, UINT flags) {
    MirrorDeleteItem(hMenu, item, (flags & MF_BYPOSITION) != 0);
    return g_origRemoveMenu ? g_origRemoveMenu(hMenu, item, flags) : FALSE;
}
static BOOL WINAPI Hook_DestroyMenu(HMENU hMenu) {
    MirrorDestroyMenu(hMenu);
    return g_origDestroyMenu ? g_origDestroyMenu(hMenu) : FALSE;
}

// ------------------------------------------------------------------
// IAT patching engine
// ------------------------------------------------------------------
struct PatchSpec {
    const char* dll;
    const char* name;
    void* hook;
    void** orig;
};

static const PatchSpec g_patches[] = {
    { "USER32.dll", "TrackPopupMenuEx",  (void*)Hook_TrackPopupMenuEx,  (void**)&g_origTrackPopupMenuEx },
    { "USER32.dll", "TrackPopupMenu",    (void*)Hook_TrackPopupMenu,    (void**)&g_origTrackPopupMenu },
    { "USER32.dll", "InsertMenuItemW",   (void*)Hook_InsertMenuItemW,   (void**)&g_origInsertMenuItemW },
    { "USER32.dll", "AppendMenuW",       (void*)Hook_AppendMenuW,       (void**)&g_origAppendMenuW },
    { "USER32.dll", "InsertMenuW",       (void*)Hook_InsertMenuW,       (void**)&g_origInsertMenuW },
    { "USER32.dll", "ModifyMenuW",       (void*)Hook_ModifyMenuW,       (void**)&g_origModifyMenuW },
    { "USER32.dll", "SetMenuItemInfoW",  (void*)Hook_SetMenuItemInfoW,  (void**)&g_origSetMenuItemInfoW },
    { "USER32.dll", "DeleteMenu",        (void*)Hook_DeleteMenu,        (void**)&g_origDeleteMenu },
    { "USER32.dll", "RemoveMenu",        (void*)Hook_RemoveMenu,        (void**)&g_origRemoveMenu },
    { "USER32.dll", "DestroyMenu",       (void*)Hook_DestroyMenu,       (void**)&g_origDestroyMenu },
};

// Every import slot we overwrite is recorded so UninstallHooks can put it back.
struct PatchRecord {
    void** slot;
    void* original;
};

static std::vector<PatchRecord> g_patchRecords;
static CRITICAL_SECTION g_patchLock;
static bool g_patchLockReady = false;

static int PatchModule(HMODULE hMod) {
    if (!hMod) return 0;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)hMod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)((BYTE*)hMod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)((BYTE*)hMod + dir.VirtualAddress);
    int patched = 0;
    for (; imp->Name; ++imp) {
        const char* modName = (const char*)((BYTE*)hMod + imp->Name);
        for (const auto& spec : g_patches) {
            if (_stricmp(modName, spec.dll) != 0) continue;
            IMAGE_THUNK_DATA64* thunk = (IMAGE_THUNK_DATA64*)((BYTE*)hMod + imp->FirstThunk);
            IMAGE_THUNK_DATA64* origThunk = imp->OriginalFirstThunk ? (IMAGE_THUNK_DATA64*)((BYTE*)hMod + imp->OriginalFirstThunk) : thunk;
            for (; thunk->u1.Function; ++thunk, ++origThunk) {
                if (!origThunk->u1.Function || IMAGE_SNAP_BY_ORDINAL64(origThunk->u1.Ordinal)) continue;
                IMAGE_IMPORT_BY_NAME* byName = (IMAGE_IMPORT_BY_NAME*)((BYTE*)hMod + origThunk->u1.AddressOfData);
                if (!byName->Name || strcmp((const char*)byName->Name, spec.name) != 0) continue;
                void** target = (void**)&thunk->u1.Function;
                void* cur = *target;
                if (cur == spec.hook) continue;
                DWORD oldProt = 0;
                if (VirtualProtect(target, sizeof(void*), PAGE_READWRITE, &oldProt)) {
                    if (spec.orig && !*spec.orig) *spec.orig = cur;
                    if (g_patchLockReady) EnterCriticalSection(&g_patchLock);
                    bool known = false;
                    for (const auto& rec : g_patchRecords) {
                        if (rec.slot == target) {
                            known = true;
                            break;
                        }
                    }
                    if (!known) {
                        PatchRecord rec;
                        rec.slot = target;
                        rec.original = cur;
                        g_patchRecords.push_back(rec);
                    }
                    if (g_patchLockReady) LeaveCriticalSection(&g_patchLock);
                    *target = spec.hook;
                    VirtualProtect(target, sizeof(void*), oldProt, &oldProt);
                    ++patched;
                }
            }
        }
    }
    return patched;
}

static void PatchAllModules() {
    HMODULE mods[1024];
    DWORD needed = 0;
    HANDLE proc = GetCurrentProcess();
    if (!EnumProcessModules(proc, mods, sizeof(mods), &needed)) return;
    int n = (int)(needed / sizeof(HMODULE));
    if (n > 1024) n = 1024;
    int total = 0;
    for (int i = 0; i < n; ++i) total += PatchModule(mods[i]);
    LogW(L"PatchAllModules: modules=%d patches=%d", n, total);
}

// module load notification: patch newly loaded modules
static PVOID g_dllNotifyCookie = nullptr;

typedef LONG RMG_NTSTATUS;
struct RMG_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};
struct RMG_LDR_DLL_LOADED_NOTIFICATION_DATA {
    ULONG Flags;
    const RMG_UNICODE_STRING* FullDllName;
    const RMG_UNICODE_STRING* BaseDllName;
    PVOID DllBase;
    ULONG SizeOfImage;
};

static VOID CALLBACK LdrDllNotification(ULONG reason, PVOID data, PVOID ctx) {
    (void)ctx;
    if (!g_hooksReady) return;
    if (reason == 1 && data) {
        auto* d = (RMG_LDR_DLL_LOADED_NOTIFICATION_DATA*)data;
        if (d->DllBase) {
            int p = PatchModule((HMODULE)d->DllBase);
            if (p > 0) {
                OutputDebugStringW(L"RMG patched module\n");
            }
        }
    }
}

typedef RMG_NTSTATUS (NTAPI* PFN_LdrRegisterDllNotification)(ULONG, PVOID, PVOID, PVOID*);

static void RegisterDllNotifications() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return;
    auto reg = (PFN_LdrRegisterDllNotification)GetProcAddress(ntdll, "LdrRegisterDllNotification");
    if (!reg) return;
    RMG_NTSTATUS st = reg(0, (PVOID)LdrDllNotification, nullptr, &g_dllNotifyCookie);
    LogW(L"LdrRegisterDllNotification status=0x%08X", (unsigned)st);
}

typedef RMG_NTSTATUS (NTAPI* PFN_LdrUnregisterDllNotification)(PVOID);

static void UnregisterDllNotifications() {
    if (!g_dllNotifyCookie) return;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto unreg = (PFN_LdrUnregisterDllNotification)GetProcAddress(ntdll, "LdrUnregisterDllNotification");
        if (unreg) {
            RMG_NTSTATUS st = unreg(g_dllNotifyCookie);
            LogW(L"LdrUnregisterDllNotification status=0x%08X", (unsigned)st);
        }
    }
    g_dllNotifyCookie = nullptr;
}

// ------------------------------------------------------------------
// Public entry
// ------------------------------------------------------------------
static bool g_hookInstalled = false;

void InstallHooks() {
    if (!g_patchLockReady) {
        InitializeCriticalSection(&g_patchLock);
        g_patchLockReady = true;
    }
    MirrorInstall();
    EnsureConfig();
    PatchAllModules();
    RegisterDllNotifications();
    g_hooksReady = 1;
    g_hookInstalled = true;
    LogW(L"hooks installed");
}

void UninstallHooks() {
    g_hooksReady = 0;
    UnregisterDllNotifications();
    int restored = 0;
    if (g_patchLockReady) EnterCriticalSection(&g_patchLock);
    for (size_t i = 0; i < g_patchRecords.size(); ++i) {
        void** slot = g_patchRecords[i].slot;
        DWORD oldProt = 0;
        if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
            *slot = g_patchRecords[i].original;
            VirtualProtect(slot, sizeof(void*), oldProt, &oldProt);
            ++restored;
        }
    }
    g_patchRecords.clear();
    if (g_patchLockReady) LeaveCriticalSection(&g_patchLock);
    g_hookInstalled = false;
    LogW(L"hooks uninstalled, %d slot(s) restored", restored);
}

bool HookIsInstalled() {
    return g_hookInstalled;
}

DWORD WINAPI InitHookThreadProc(LPVOID param) {
    (void)param;
    LogInit();
    LogW(L"DLL init thread start, exe=%s", GetProcessImagePath().c_str());
    if (!IsProcessExplorer()) {
        LogW(L"not explorer, inactive");
        return 0;
    }
    SaveDefaultConfigIfMissing();
    InstallHooks();
    return 0;
}
