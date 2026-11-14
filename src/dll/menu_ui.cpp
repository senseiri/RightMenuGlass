#include "hook.h"
#include "util.h"
#include "config.h"
#include "draw.h"
#include "plugin.h"
#include "provider.h"
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <cwctype>
#include <vector>
#include <string>

// ============================================================
//  RightMenuGlass custom menu window (fully owner drawn)
// ============================================================

static const wchar_t* kMenuClass = L"RMG_CustomMenuWnd";
static const int kTimerSub = 1;
static const int kTimerLeave = 2;
static const int kTimerAnim = 3;
static const int kAnimFrameMs = 15;
// Grace period before submenus fold away when the pointer leaves the whole chain.
// Long enough to let the pointer travel diagonally from a parent item into its submenu.
static const int kLeaveDelayMs = 220;

extern HINSTANCE g_hInst;

struct UiState;
struct UiWindow;

struct UiItem {
    MenuItemData d;
    std::vector<UiItem> children;
    bool customPopup = false;
    bool third = false;
    // plugin action payload
    std::wstring pluginCmd;
    bool pluginAdmin = false;
    bool pluginConsole = false;
    // marked when the user pinned this entry to the first level
    bool promote = false;
};

struct UiWindow {
    HWND hwnd = NULL;
    UiState* ui = NULL;
    HMENU hmenu = NULL;
    std::vector<UiItem> items;
    bool isRoot = false;
    UiWindow* parent = NULL;
    int parentItem = -1;
    int dpi = 96;
    int winW = 0, winH = 0;
    int contentX = 0, contentY = 0, contentW = 0, contentH = 0;
    int pad = 10;
    int headerH = 0;
    int itemTop = 0;
    int itemAreaH = 0;
    int itemH = 32;
    int iconSz = 16;
    int corner = 10;
    int shadow = 14;
    int firstIndex = 0;
    int maxFirst = 0;
    int hover = -1;
    int hoverBtn = -1;
    RECT btnRects[12];
    int nBtn = 0;
    Bitmap32 bg;
    bool bgReady = false;
    bool rendered = false;
    Bitmap32 renderBuf;
    RECT rect = {0, 0, 0, 0};
    // The menu may pop up under a resting cursor. Windows sends a WM_MOUSEMOVE when the
    // window appears there; we ignore it until the cursor really moves, otherwise a
    // submenu would spring open under the pointer before the user does anything.
    bool mouseMoved = false;
    POINT shownAt = {0, 0};
    // pop-up animation state; animBase is the final (100%) rectangle
    bool animating = false;
    ULONGLONG animStart = 0;
    int animDurMs = 0;
    int animFromOpacity = 100;
    int animFromScale = 100;
    int animFromOffY = 0;
    double animBez[4] = {0.16, 0.84, 0.44, 1.0};
    RECT animBase = {0, 0, 0, 0};
};

struct UiState {
    Config cfg;
    HINSTANCE hinst = NULL;
    int dpi = 96;
    HWND owner = NULL;
    std::vector<UiWindow*> wins;
    bool done = false;
    bool canceled = true;
    // Destroying a submenu that owns focus or capture makes Windows send WM_KILLFOCUS /
    // WM_CAPTURECHANGED with a target that is already gone from `wins`. Without this
    // guard the whole chain (root menu included) would close itself while folding a
    // submenu away.
    int suppressFocusClose = 0;
    UINT selectedId = 0;
    HFONT font = NULL;
    HFONT iconFont = NULL;
    int hoverDelayMs = 320;
    std::vector<bool> btnEnabled;
    std::wstring pluginCmd;
    bool pluginAdmin = false;
    bool pluginConsole = false;
};

// ------------------------------------------------------------
//  forward declarations
// ------------------------------------------------------------
static void EnsureMenuClass(HINSTANCE hinst);
static void OpenSubmenu(UiWindow* w, int index);
static void ClearCaptureAndClose(UiWindow* w);
static LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void PaintWindow(UiWindow* w);
static void InvalidateWindow(UiWindow* w);

// ------------------------------------------------------------
//  helpers
// ------------------------------------------------------------
static UINT QueryDpi() {
    typedef UINT (WINAPI* PFN_GetDpiForSystem)();
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        PFN_GetDpiForSystem f = (PFN_GetDpiForSystem)GetProcAddress(u, "GetDpiForSystem");
        if (f) {
            UINT d = f();
            if (d) return d;
        }
    }
    HDC dc = GetDC(NULL);
    UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    return d ? d : 96;
}

int DpiForPoint(POINT pt) {
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    HMODULE sh = GetModuleHandleW(L"shcore.dll");
    if (!sh) sh = LoadLibraryW(L"shcore.dll");
    if (sh) {
        typedef HRESULT (WINAPI* PFN_GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
        PFN_GetDpiForMonitor f = (PFN_GetDpiForMonitor)GetProcAddress(sh, "GetDpiForMonitor");
        if (f) {
            UINT dx = 96, dy = 96;
            if (SUCCEEDED(f(mon, 0, &dx, &dy)) && dx > 0) return (int)dx;
        }
    }
    return (int)QueryDpi();
}

static int ScalePx(int v, int dpi) {
    return (int)llround((double)v * dpi / 96.0);
}

static HFONT MakeMenuFont(const std::wstring& family, int px, int weight) {
    LOGFONTW lf = {};
    lf.lfHeight = -px;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    lstrcpynW(lf.lfFaceName, family.c_str(), LF_FACESIZE);
    return CreateFontIndirectW(&lf);
}

static wchar_t ParseGlyph(const std::wstring& s) {
    if (s.empty()) return 0;
    if (s.size() >= 4 && s.size() <= 6) {
        bool hex = true;
        for (wchar_t c : s) {
            if (!iswxdigit(c)) { hex = false; break; }
        }
        if (hex) {
            long v = wcstol(s.c_str(), NULL, 16);
            if (v > 0 && v < 0x10000) return (wchar_t)v;
        }
    }
    return s[0];
}

static bool MatchAny(const std::wstring& text, const std::vector<std::wstring>& keywords) {
    std::wstring low = ToLowerW(text);
    for (const auto& k : keywords) {
        if (k.empty()) continue;
        if (low.find(ToLowerW(k)) != std::wstring::npos) return true;
    }
    return false;
}

static bool IsSystemKeyword(const std::wstring& text) {
    static const wchar_t* kws[] = {
        L"打开", L"open", L"新建", L"new", L"查看", L"view",
        L"排序", L"sort", L"刷新", L"refresh", L"复制", L"copy",
        L"剪切", L"cut", L"粘贴", L"paste", L"删除", L"delete",
        L"重命名", L"rename", L"属性", L"properties", L"发送到",
        L"send to", L"共享", L"share", L"授予访问", L"give access",
        L"固定", L"pin", L"显示设置", L"display settings",
        L"个性化", L"personalize", L"打开方式", L"open with",
        L"打印", L"print", L"刻录", L"burn", L"压缩", L"compress",
        L"解压", L"extract", L"快捷方式", L"shortcut", L"权限",
        L"permission", L"高级", L"advanced", L"显示", L"show", L"排列",
        L"align", L"格式化", L"format", L"弹出", L"eject", L"撤销",
        L"undo", L"还原", L"restore", L"疑难解答", L"troubleshoot",
        L"设置", L"settings", L"管理", L"manage", L"结束任务",
        L"end task", L"关闭", L"close", L"访问", L"access",
        L"上传", L"upload", L"同步", L"sync",
        L"备份", L"backup", L"扫描", L"scan", L"克隆", L"clone",
        L"提交", L"commit", L"更新", L"update", L"切换", L"switch",
        L"编辑", L"edit", L"copilot", L"询问"
    };
    std::wstring low = ToLowerW(text);
    for (const wchar_t* k : kws) {
        std::wstring kk = ToLowerW(k);
        if (!kk.empty() && low.find(kk) != std::wstring::npos) return true;
    }
    return false;
}

static bool LooksThirdParty(const std::wstring& text) {
    static const wchar_t* kws[] = {
        L"7-zip", L"winrar", L"winzip", L"bandizip", L"7z", L"rar", L"zip",
        L"tortoise", L"git", L"vscode", L"visual studio", L"code", L"sublime",
        L"notepad", L"atom", L"jetbrains", L"intellij", L"pycharm", L"webstorm",
        L"360", L"huorong", L"eset", L"kaspersky", L"avast", L"norton",
        L"onedrive", L"dropbox", L"google drive", L"baidu", L"百度", L"网盘",
        L"坚果云", L"阿里云盘", L"迅雷", L"腾讯",
        L"snipaste", L"everything", L"potplayer", L"vlc", L"nvidia", L"amd", L"adobe",
        L"使用", L"上传到", L"上传", L"分享到", L"分享",
        L"同步至", L"备份到", L"打包", L"加密", L"解密",
        L"粉碎", L"扫描病毒", L"编辑器", L"mediainfo",
        L"wps", L"office", L"福昕", L"foxit", L"批量",
        L"压缩文件", L"压缩到", L"添加到",
        L"解压到", L"添加压缩", L"解压"
    };
    std::wstring low = ToLowerW(text);
    for (const wchar_t* k : kws) {
        std::wstring kk = ToLowerW(k);
        if (!kk.empty() && low.find(kk) != std::wstring::npos) return true;
    }
    return false;
}

// Words that only ever belong to the shell's own menu.  Anything
// matching one of these stays in the first level even when a vendor
// keyword (7z / wps / ...) also appears in its text.
static bool IsHardSystem(const std::wstring& text) {
    static const wchar_t* kws[] = {
        L"打开", L"发送到", L"剪切", L"复制", L"粘贴", L"创建快捷方式",
        L"删除", L"重命名", L"属性", L"固定到", L"查看", L"排序方式",
        L"分组依据", L"刷新", L"新建", L"授予访问权限", L"显示设置",
        L"个性化", L"自定义文件夹", L"共享", L"打开方式"
    };
    std::wstring low = ToLowerW(text);
    for (const wchar_t* k : kws) {
        std::wstring kk = ToLowerW(k);
        if (!kk.empty() && low.find(kk) != std::wstring::npos) return true;
    }
    return false;
}

// ------------------------------------------------------------
//  build ui items from parsed menu
// ------------------------------------------------------------
static void ConvertMenuRec(const MenuData& md, std::vector<UiItem>& out) {
    for (const auto& it : md.items) {
        UiItem u;
        u.d = it;
        if (it.sub) {
            MenuData sub;
            if (BuildMenuData(it.sub, sub, false)) {
                ConvertMenuRec(sub, u.children);
            }
        }
        out.push_back(std::move(u));
    }
}

static void ConvertMenu(const MenuData& md, std::vector<UiItem>& out) {
    out.clear();
    ConvertMenuRec(md, out);
}

// ------------------------------------------------------------
//  third party folding
// ------------------------------------------------------------
// Insert declarative plugin items (level 1 or grouped submenu).
static void ApplyPlugins(UiState* ui, const MenuData& data, std::vector<UiItem>& items) {
    if (!ui || !ui->owner) return;
    std::vector<std::wstring> texts;
    texts.reserve(data.items.size());
    for (const auto& it : data.items) {
        if (!it.text.empty()) texts.push_back(it.text);
    }
    POINT cur = {0, 0};
    GetCursorPos(&cur);
    MenuContext ctx = BuildMenuContext(ui->owner, &cur, texts);
    PluginSet ps = LoadPlugins(true);
    if (ps.plugins.empty()) {
        LogW(L"plugins: no plugin loaded (dir missing or invalid)");
        return;
    }

    std::vector<std::wstring> groupTitles;
    std::map<std::wstring, std::vector<UiItem>> groups;
    std::vector<UiItem> direct;
    int inserted = 0;
    for (const auto& p : ps.plugins) {
        if (!PluginMatches(p, ctx)) continue;
        for (const auto& a : p.actions) {
            UiItem uiIt;
            uiIt.d.text = a.text;
            uiIt.d.rawText = a.text;
            uiIt.d.id = 1;
            uiIt.pluginCmd = a.command;
            uiIt.pluginAdmin = a.admin;
            uiIt.pluginConsole = a.console;
            ++inserted;
            if (p.groupSubmenu) {
                if (std::find(groupTitles.begin(), groupTitles.end(), p.groupTitle) == groupTitles.end()) {
                    groupTitles.push_back(p.groupTitle);
                }
                groups[p.groupTitle].push_back(uiIt);
            } else {
                direct.push_back(uiIt);
            }
        }
    }
    if (!inserted) {
        LogW(L"plugins: %d loaded, none matched ctx(files=%d bg=%d folder=%d desktop=%d sel=%d owner=%s)",
             (int)ps.plugins.size(), ctx.hasFiles ? 1 : 0, ctx.isBackground ? 1 : 0, ctx.isFolderWindow ? 1 : 0,
             ctx.isDesktop ? 1 : 0, (int)ctx.selectedPaths.size(), ctx.ownerClass.c_str());
        return;
    }
    if (!items.empty() && !items.back().d.separator) {
        UiItem sep;
        sep.d.separator = true;
        sep.d.type = MF_SEPARATOR;
        sep.d.id = 0;
        items.push_back(sep);
    }
    for (auto& d : direct) items.push_back(d);
    for (auto& title : groupTitles) {
        UiItem sub;
        sub.d.text = title;
        sub.d.rawText = title;
        sub.d.popup = true;
        sub.customPopup = true;
        sub.children = groups[title];
        items.push_back(sub);
    }
    LogW(L"plugins: inserted %d item(s), groups=%d, ctx(files=%d bg=%d desktop=%d selection=%d)",
         inserted, (int)groupTitles.size(), ctx.hasFiles ? 1 : 0, ctx.isBackground ? 1 : 0,
         ctx.isDesktop ? 1 : 0, (int)ctx.selectedPaths.size());
}

// Which software groups a normalised label belongs to, taken from the symbols
// the DLL has actually seen (observed.json).  A label can appear in more than
// one group, so the caller checks all of them.
typedef std::map<std::wstring, std::vector<std::wstring>> GroupIndex;

static void BuildGroupIndex(GroupIndex& idx) {
    ObservedDb db;
    if (!LoadObservedDb(db)) return;
    for (const auto& p : db.providers) {
        for (const auto& o : p.items) {
            std::wstring key = o.norm.empty() ? NormalizeMenuText(o.text) : o.norm;
            if (!key.empty()) idx[key].push_back(p.id);
        }
    }
}

static bool ItemPromoted(const ThirdPartyConfig& tp, const UiItem& it, const GroupIndex& idx) {
    if (tp.promoteGroups.empty() && tp.promoteItems.empty()) return false;
    std::wstring norm = NormalizeMenuText(it.d.text);
    GroupIndex::const_iterator f = idx.find(norm);
    if (f == idx.end()) return false;
    for (size_t k = 0; k < f->second.size(); ++k) {
        const std::wstring& gid = f->second[k];
        for (size_t i = 0; i < tp.promoteGroups.size(); ++i) {
            if (tp.promoteGroups[i].all && tp.promoteGroups[i].id == gid) return true;
        }
        for (size_t i = 0; i < tp.promoteItems.size(); ++i) {
            if (tp.promoteItems[i].id == gid && tp.promoteItems[i].item == norm) return true;
        }
    }
    return false;
}

static void ApplyThirdParty(UiState* ui, std::vector<UiItem>& items, bool root) {
    if (!root || !ui->cfg.thirdParty.enabled) return;
    const ThirdPartyConfig& tp = ui->cfg.thirdParty;
    GroupIndex groups;
    if (!tp.promoteGroups.empty() || !tp.promoteItems.empty()) BuildGroupIndex(groups);
    std::vector<UiItem> keep;
    std::vector<UiItem> third;
    for (auto& it : items) {
        if (it.d.separator) { keep.push_back(it); continue; }
        if (MatchAny(it.d.text, tp.hide)) continue;
        // Vendor keywords win over the hard-system list: a tool that calls
        // its entry "批量重命名" is a third-party entry even though
        // it contains the shell word "重命名".
        bool isThird = LooksThirdParty(it.d.text) ||
                       (!IsHardSystem(it.d.text) && !IsSystemKeyword(it.d.text) && !it.d.popup);
        if (!isThird) { keep.push_back(it); continue; }
        if (MatchAny(it.d.text, tp.keepTop)) { keep.push_back(it); continue; }
        if (ItemPromoted(tp, it, groups)) { it.promote = true; keep.push_back(it); continue; }
        third.push_back(it);
    }

    if (tp.mergePromoted) {
        int promoted = 0;
        for (const auto& k : keep) if (k.promote) ++promoted;
        if (promoted > 1) {
            std::vector<UiItem> merged;
            std::vector<UiItem> rest;
            for (auto& k : keep) {
                if (k.promote) merged.push_back(k);
                else rest.push_back(k);
            }
            while (!rest.empty() && rest.back().d.separator) rest.pop_back();
            if (!rest.empty()) {
                UiItem sep;
                sep.d.separator = true;
                rest.push_back(sep);
            }
            UiItem node;
            node.d.text = tp.mergedTitle.empty() ? L"\u5e38\u7528\u5de5\u5177" : tp.mergedTitle;
            node.d.popup = true;
            node.customPopup = true;
            node.children = merged;
            rest.push_back(node);
            keep = rest;
        }
    }

    if (third.empty()) { items = keep; return; }
    while (!keep.empty() && keep.back().d.separator) keep.pop_back();
    UiItem tpv;
    tpv.d.text = tp.title;
    tpv.d.popup = true;
    tpv.customPopup = true;
    tpv.third = true;
    tpv.d.id = 0;
    tpv.children = third;
    if (!keep.empty()) {
        UiItem sep;
        sep.d.separator = true;
        keep.push_back(sep);
    }
    keep.push_back(tpv);
    items = keep;
}

// ------------------------------------------------------------
//  layout
// ------------------------------------------------------------
static int ItemHeightOf(UiWindow* w, const UiItem& it) {
    if (it.d.separator) return ScalePx(9, w->dpi);
    return w->itemH;
}

static int ComputeItemWidth(UiWindow* w, const UiItem& it) {
    int iconW = 0;
    if (!it.d.separator) iconW = w->iconSz + ScalePx(8, w->dpi);
    SIZE tsz = MeasureTextSize(w->ui->font, it.d.text);
    int shortcutW = 0;
    if (w->ui->cfg.style.showShortcut && !it.d.shortcut.empty()) {
        SIZE s = MeasureTextSize(w->ui->font, it.d.shortcut);
        shortcutW = (int)s.cx + ScalePx(20, w->dpi);
    }
    int arrowW = 0;
    if (it.d.popup || it.customPopup) arrowW = ScalePx(18, w->dpi);
    return iconW + (int)tsz.cx + shortcutW + arrowW;
}

static RECT MonitorWorkRect(POINT pt) {
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    RECT r = {0, 0, 1920, 1080};
    if (GetMonitorInfoW(mon, &mi)) r = mi.rcWork;
    return r;
}

// Clamp a window origin into [lo, hi]. An inverted span means the window is larger than
// the work area, in which case centring it keeps both edges equally (in)visible.
static int FitSpan(int pos, int lo, int hi) {
    if (lo > hi) return (lo + hi) / 2;
    if (pos < lo) return lo;
    if (pos > hi) return hi;
    return pos;
}

// The icon bar shrinks its own buttons when the bar is short, so setting a small
// headerHeight keeps the icons evenly spaced instead of clipping them.
static int HeaderButtonSize(const UiWindow* w) {
    int dpi = w->dpi;
    int btn = ScalePx(30, dpi);
    int room = w->headerH - ScalePx(4, dpi);
    if (room > 0 && btn > room) btn = room;
    int floorSz = ScalePx(14, dpi);
    if (btn < floorSz) btn = floorSz;
    return btn;
}

static void LayoutWindow(UiWindow* w, POINT anchor) {
    UiState* ui = w->ui;
    int dpi = w->dpi;
    w->pad = ScalePx(ui->cfg.acrylic.margin, dpi);
    w->headerH = w->isRoot ? ScalePx(ui->cfg.style.headerHeight, dpi) : 0;
    w->itemH = ScalePx(ui->cfg.style.itemHeight, dpi);
    w->iconSz = ScalePx(ui->cfg.style.iconSize, dpi);
    w->corner = ScalePx(ui->cfg.acrylic.corner, dpi);
    w->shadow = ScalePx(ui->cfg.acrylic.shadow, dpi);
    if (w->shadow < 1) w->shadow = 1;

    int minW = ScalePx(ui->cfg.style.minWidth, dpi);
    int maxW = ScalePx(ui->cfg.style.maxWidth, dpi);
    int maxH = ScalePx(ui->cfg.style.maxHeight, dpi);

    int contentW = minW;
    int itemsH = 0;
    for (const auto& it : w->items) {
        itemsH += ItemHeightOf(w, it);
        int cw = ComputeItemWidth(w, it) + w->pad * 2;
        if (cw > contentW) contentW = cw;
    }
    if (w->isRoot && !ui->cfg.buttons.empty()) {
        int n = (int)ui->cfg.buttons.size();
        int btn = HeaderButtonSize(w);
        int gap = ScalePx(4, dpi);
        int bw = n * btn + (n - 1) * gap + w->pad * 2;
        if (bw > contentW) contentW = bw;
    }
    if (contentW > maxW) contentW = maxW;
    if (contentW < minW) contentW = minW;

    int topFixed = w->pad + (w->isRoot ? w->headerH + 1 + w->pad : 0);
    int bodyH = topFixed + itemsH + w->pad;
    if (bodyH > maxH) bodyH = maxH;

    w->contentX = w->shadow;
    w->contentY = w->shadow;
    w->contentW = contentW;
    w->contentH = bodyH;
    w->winW = contentW + w->shadow * 2;
    w->winH = bodyH + w->shadow * 2;
    w->itemTop = w->shadow + topFixed;
    w->itemAreaH = bodyH - topFixed - w->pad;
    if (w->itemAreaH < 0) w->itemAreaH = 0;

    w->maxFirst = 0;
    int avail = w->itemAreaH;
    int h = 0;
    int start = (int)w->items.size();
    while (start > 0) {
        int ih = ItemHeightOf(w, w->items[start - 1]);
        if (h + ih > avail) break;
        h += ih;
        start--;
    }
    w->maxFirst = start;
    if (w->firstIndex > w->maxFirst) w->firstIndex = w->maxFirst;
    if (w->firstIndex < 0) w->firstIndex = 0;

    RECT work = MonitorWorkRect(anchor);
    int edge = ScalePx(8, dpi);
    int loX = work.left + edge;
    int hiX = work.right - edge - w->winW;
    int loY = work.top + edge;
    int hiY = work.bottom - edge - w->winH;

    int x = anchor.x;
    int y = anchor.y;
    if (w->isRoot) {
        // The root menu opens towards the configured direction (default down-right, i.e. its
        // top-left corner sits on the pointer). If that would leave the work area it flips to
        // the opposite side of the pointer, so the card stays visible and points at the middle
        // of the screen. Only when neither side fits does it fall back to clamping.
        const std::wstring& dir = ui->cfg.style.openDirection;
        bool right = true;
        bool down = true;
        if (dir == L"right-up") {
            down = false;
        } else if (dir == L"left-down") {
            right = false;
        } else if (dir == L"left-up") {
            right = false;
            down = false;
        } else if (dir == L"auto") {
            right = anchor.x < (work.left + work.right) / 2;
            down = anchor.y < (work.top + work.bottom) / 2;
        }
        int px = right ? anchor.x : anchor.x - w->winW;
        if (px > hiX || px < loX) {
            int flipped = right ? (anchor.x - w->winW) : anchor.x;
            px = (flipped >= loX && flipped <= hiX) ? flipped : FitSpan(px, loX, hiX);
        }
        int py = down ? anchor.y : anchor.y - w->winH;
        if (py > hiY || py < loY) {
            int flipped = down ? (anchor.y - w->winH) : anchor.y;
            py = (flipped >= loY && flipped <= hiY) ? flipped : FitSpan(py, loY, hiY);
        }
        x = px;
        y = py;
    } else {
        // Submenus stay glued to the parent item: prefer the right side, fall back to the
        // left, and centre only when neither side has room.
        if (x > hiX) {
            int flipped = anchor.x - w->winW;
            x = (flipped >= loX) ? flipped : (work.left + work.right - w->winW) / 2;
        }
        x = FitSpan(x, loX, hiX);
        y = FitSpan(y, loY, hiY);
    }
    w->rect.left = x;
    w->rect.top = y;
    w->rect.right = x + w->winW;
    w->rect.bottom = y + w->winH;
}

static void EnsureVisible(UiWindow* w, int idx) {
    if (idx < 0) return;
    while (w->firstIndex > idx && w->firstIndex > 0) w->firstIndex--;
    for (;;) {
        int y = w->itemTop;
        int last = w->firstIndex;
        for (int i = w->firstIndex; i < (int)w->items.size(); ++i) {
            int ih = ItemHeightOf(w, w->items[i]);
            if (y + ih > w->itemTop + w->itemAreaH) break;
            y += ih;
            last = i;
        }
        if (idx <= last) break;
        if (w->firstIndex >= w->maxFirst) break;
        w->firstIndex++;
    }
}

// ------------------------------------------------------------
//  painting
// ------------------------------------------------------------
// Hands a premultiplied BGRA layer to the compositor. The layer is the whole window, so the
// card can be scaled and faded without the window ever painting a stale screenshot around it.
static void PresentLayered(HWND hwnd, const Bitmap32& bm, int dstW, int dstH, int alphaPct) {
    if (!hwnd || !IsWindow(hwnd) || bm.w <= 0 || bm.h <= 0 || dstW <= 0 || dstH <= 0) return;
    if (alphaPct < 0) alphaPct = 0;
    if (alphaPct > 100) alphaPct = 100;

    HDC screen = GetDC(NULL);
    if (!screen) return;
    HDC mem = CreateCompatibleDC(screen);
    if (!mem) { ReleaseDC(NULL, screen); return; }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = dstW;
    bi.bmiHeader.biHeight = -dstH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) { DeleteDC(mem); ReleaseDC(NULL, screen); return; }
    HGDIOBJ old = SelectObject(mem, dib);

    if (dstW == bm.w && dstH == bm.h) {
        memcpy(bits, bm.px.data(), (size_t)bm.w * (size_t)bm.h * 4);
    } else {
        Bitmap32 scaled;
        ResamplePremultiplied(bm, dstW, dstH, scaled);
        if (scaled.w == dstW && scaled.h == dstH) memcpy(bits, scaled.px.data(), (size_t)dstW * (size_t)dstH * 4);
        else memset(bits, 0, (size_t)dstW * (size_t)dstH * 4);
    }

    SIZE sz = { dstW, dstH };
    POINT srcPt = { 0, 0 };
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = (BYTE)(alphaPct * 255 / 100);
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(hwnd, screen, NULL, &sz, mem, &srcPt, 0, &bf, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static void BlitBitmap(HWND hwnd, const Bitmap32& bm) {
    if (bm.w <= 0 || bm.h <= 0) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) { cw = bm.w; ch = bm.h; }
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) {
        PresentLayered(hwnd, bm, cw, ch, 100);
        return;
    }
    HDC dc = GetDC(hwnd);
    if (!dc) return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.w;
    bi.bmiHeader.biHeight = -bm.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    if (cw == bm.w && ch == bm.h) {
        SetDIBitsToDevice(dc, 0, 0, bm.w, bm.h, 0, 0, 0, bm.h,
            (void*)bm.px.data(), &bi, DIB_RGB_COLORS);
    } else {
        // The pop-up animation resizes the window for a few frames; stretch the
        // pre-rendered card instead of re-laying it out on every tick.
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchDIBits(dc, 0, 0, cw, ch, 0, 0, bm.w, bm.h,
            (void*)bm.px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    }
    ReleaseDC(hwnd, dc);
}

static void FillRoundRectAdaptive(Bitmap32& dst, int x, int y, int w, int h, int radius,
                                  COLORREF dark, COLORREF light, int threshold, int alphaPct) {
    if (w <= 0 || h <= 0) return;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 0) radius = 0;
    float r = (float)radius;
    int a = alphaPct * 255 / 100;
    for (int yy = 0; yy < h; ++yy) {
        int gy = y + yy;
        if (!dst.Has(x, gy)) continue;
        for (int xx = 0; xx < w; ++xx) {
            int gx = x + xx;
            if (!dst.Has(gx, gy)) continue;
            float px = xx + 0.5f;
            float py = yy + 0.5f;
            float cx = std::min(std::max(px, r), (float)w - r);
            float cy = std::min(std::max(py, r), (float)h - r);
            float dx = px - cx;
            float dy = py - cy;
            float d = sqrtf(dx * dx + dy * dy) - r;
            float cov = 0.5f - d;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            uint32_t cur = dst.At(gx, gy);
            uint32_t pick = (Luma255(cur) >= threshold) ? ToArgb(light) : ToArgb(dark);
            dst.At(gx, gy) = BlendArgb(cur, pick, (int)(a * cov));
        }
    }
}

static COLORREF HexColor(const std::wstring& s, COLORREF def) {
    return ParseColorHex(s, def);
}

static void PaintWindow(UiWindow* w) {
    if (!w || !w->bgReady) return;
    if (w->rendered) {
        BlitBitmap(w->hwnd, w->renderBuf);
        return;
    }

    UiState* ui = w->ui;
    const ColorConfig& cc = ui->cfg.colors;
    int dpi = w->dpi;
    Bitmap32 canvas = w->bg;

    COLORREF dark = HexColor(cc.darkText, RGB(0, 0, 0));
    COLORREF light = HexColor(cc.lightText, RGB(255, 255, 255));
    int threshold = cc.threshold;
    int softness = ScalePx(cc.softness, dpi);   // 过渡带宽同样跟随 DPI
    int haloW = ScalePx(cc.haloWidth, dpi);     // 描边宽度（0 = 关闭）
    int haloA = cc.haloAlpha;                   // 描边强度
    // 0 = per pixel, 1 = per glyph, 2 = per menu item (see draw.h).
    int colorUnit = 0;
    if (cc.colorUnit == L"glyph") colorUnit = 1;
    else if (cc.colorUnit == L"item") colorUnit = 2;
    if (!cc.enabled) {
        // Smart colouring switched off: paint every glyph in one fixed ink, without sampling
        // the backdrop at all. The colour is colors.darkText so the user still picks it.
        // No fade and no rim either - both only make sense relative to the backdrop.
        light = dark;
        softness = 0;
        haloA = 0;
        colorUnit = 0;
    }
    int textGap = ScalePx(8, dpi);
    COLORREF lineColor = RGB(128, 128, 128);

    if (w->isRoot) {
        w->nBtn = 0;
        int btn = HeaderButtonSize(w);
        int gap = ScalePx(4, dpi);
        int bx = w->contentX + w->pad;
        int by = w->contentY + w->pad + (w->headerH - btn) / 2;
        int n = (int)ui->cfg.buttons.size();
        if (n > 12) n = 12;
        for (int i = 0; i < n; ++i) {
            if (!ui->cfg.buttons[i].enabled) { w->btnRects[i] = {0, 0, 0, 0}; continue; }
            RECT rr = { bx, by, bx + btn, by + btn };
            w->btnRects[i] = rr;
            w->nBtn = i + 1;
            if (i == w->hoverBtn) {
                FillRoundRectAdaptive(canvas, rr.left, rr.top, btn, btn, ScalePx(6, dpi),
                    RGB(0, 0, 0), RGB(255, 255, 255), threshold, 14);
            }
            wchar_t glyph = ParseGlyph(ui->cfg.buttons[i].icon);
            if (glyph) {
                SIZE gsz = MeasureTextSize(ui->iconFont, std::wstring(1, glyph));
                int gx = rr.left + (btn - (int)gsz.cx) / 2;
                int gy = rr.top + (btn - (int)gsz.cy) / 2;
                bool en = (i < (int)ui->btnEnabled.size()) ? ui->btnEnabled[i] : false;
                DrawGlyphColored(canvas, gx, gy, glyph, ui->iconFont,
                    dark, light, threshold, en ? 92 : 38, softness, haloW, haloA, colorUnit);
            }
            bx += btn + gap;
        }
        int sy = w->contentY + w->pad + w->headerH;
        DrawHLine(canvas, w->contentX + w->pad, sy, w->contentW - w->pad * 2, lineColor, 22);
    }

    int y = w->itemTop;
    int hoveredIdx = w->hover;
    for (int i = 0; i < (int)w->items.size(); ++i) {
        if (i < w->firstIndex) continue;
        int ih = ItemHeightOf(w, w->items[i]);
        if (y >= w->itemTop + w->itemAreaH) break;
        UiItem& it = w->items[i];
        if (it.d.separator) {
            int ly = y + ih / 2;
            DrawHLine(canvas, w->contentX + w->pad + ScalePx(6, dpi), ly,
                w->contentW - w->pad * 2 - ScalePx(12, dpi), lineColor, 20);
            y += ih;
            continue;
        }
        bool disabled = it.d.disabled;
        if (i == hoveredIdx && !disabled) {
            FillRoundRectAdaptive(canvas, w->contentX + w->pad, y, w->contentW - w->pad * 2, ih,
                ScalePx(6, dpi), RGB(0, 0, 0), RGB(255, 255, 255), threshold, 13);
        }
        int tx = w->contentX + w->pad;
        if (it.d.checked) {
            DrawGlyphColored(canvas, tx + ScalePx(2, dpi), y + (ih - w->iconSz) / 2, 0xE73E,
                ui->iconFont, dark, light, threshold, 85, softness, haloW, haloA, colorUnit);
        }
        tx += w->iconSz + textGap;

        SIZE tsz = MeasureTextSize(ui->font, it.d.text);
        int ty = y + (ih - (int)tsz.cy) / 2;
        int alpha = disabled ? 40 : 96;
        DrawStringColored(canvas, tx, ty, it.d.text, ui->font,
            dark, light, threshold, alpha, 1, 1, disabled ? 8 : 24, softness, haloW, haloA, colorUnit);

        if (ui->cfg.style.showShortcut && !it.d.shortcut.empty()) {
            SIZE ssz = MeasureTextSize(ui->font, it.d.shortcut);
            int sx = w->contentX + w->contentW - w->pad - (int)ssz.cx - ScalePx(6, dpi);
            if (it.d.popup || it.customPopup) sx -= ScalePx(16, dpi);
            DrawStringColored(canvas, sx, y + (ih - (int)ssz.cy) / 2, it.d.shortcut, ui->font,
                dark, light, threshold, 52, 1, 1, 14, softness, haloW, haloA, colorUnit);
        }
        if (it.d.popup || it.customPopup) {
            int ax = w->contentX + w->contentW - w->pad - ScalePx(14, dpi);
            int asz = ScalePx(10, dpi);
            int ay = y + (ih - asz) / 2;
            DrawGlyphColored(canvas, ax, ay, 0xE76C, ui->iconFont,
                dark, light, threshold, disabled ? 30 : 78, softness, haloW, haloA, colorUnit);
        }
        y += ih;
    }

    if (w->maxFirst > 0) {
        int trackX = w->contentX + w->contentW - ScalePx(4, dpi);
        int trackTop = w->itemTop;
        int trackH = w->itemAreaH;
        int thumbH = (int)((double)trackH * w->itemAreaH / std::max(1, (int)w->items.size() * w->itemH));
        if (thumbH < ScalePx(24, dpi)) thumbH = ScalePx(24, dpi);
        if (thumbH > trackH) thumbH = trackH;
        int off = w->maxFirst > 0 ? (trackH - thumbH) * w->firstIndex / w->maxFirst : 0;
        FillRoundRectAdaptive(canvas, trackX, trackTop + off, ScalePx(3, dpi), thumbH,
            ScalePx(2, dpi), RGB(0, 0, 0), RGB(255, 255, 255), threshold, 45);
    }

    w->renderBuf = std::move(canvas);
    w->rendered = true;
    BlitBitmap(w->hwnd, w->renderBuf);
}

static void InvalidateWindow(UiWindow* w) {
    if (!w) return;
    w->rendered = false;
    if (w->hwnd && IsWindow(w->hwnd)) InvalidateRect(w->hwnd, NULL, FALSE);
}

// ------------------------------------------------------------
//  hit testing
// ------------------------------------------------------------
static int HitItem(UiWindow* w, int mx, int my) {
    if (my < w->itemTop || my >= w->itemTop + w->itemAreaH) return -1;
    int y = w->itemTop;
    for (int i = 0; i < (int)w->items.size(); ++i) {
        int ih = ItemHeightOf(w, w->items[i]);
        if (i >= w->firstIndex && my >= y && my < y + ih) return i;
        y += ih;
        if (y > my) break;
    }
    return -1;
}

static int HitButton(UiWindow* w, int mx, int my) {
    if (!w->isRoot) return -1;
    for (int i = 0; i < w->nBtn; ++i) {
        const RECT& r = w->btnRects[i];
        if (r.right > r.left && mx >= r.left && mx < r.right && my >= r.top && my < r.bottom) return i;
    }
    return -1;
}

static bool FindItemCmd(UiWindow* w, const std::wstring& cmd, int& outIndex) {
    std::wstring c = ToLowerW(cmd);
    const wchar_t* words[4] = { NULL, NULL, NULL, NULL };
    if (c == L"copy") { words[0] = L"复制"; words[1] = L"copy"; words[2] = L"拷贝"; }
    else if (c == L"cut") { words[0] = L"剪切"; words[1] = L"cut"; }
    else if (c == L"paste") { words[0] = L"粘贴"; words[1] = L"paste"; }
    else if (c == L"rename") { words[0] = L"重命名"; words[1] = L"rename"; }
    else if (c == L"delete") { words[0] = L"删除"; words[1] = L"delete"; }
    else if (c == L"props") { words[0] = L"属性"; words[1] = L"properties"; }
    else if (c == L"open") { words[0] = L"打开"; words[1] = L"open"; }
    else if (c == L"refresh") { words[0] = L"刷新"; words[1] = L"refresh"; }
    else if (c == L"new") { words[0] = L"新建"; words[1] = L"new"; }
    else if (c == L"settings") { words[0] = L"显示设置"; words[1] = L"个性化"; words[2] = L"settings"; }
    else if (c == L"share") { words[0] = L"共享"; words[1] = L"share"; }
    else { return false; }

    for (int wi = 0; wi < 4 && words[wi]; ++wi) {
        std::wstring word = ToLowerW(words[wi]);
        for (int i = 0; i < (int)w->items.size(); ++i) {
            if (w->items[i].d.separator || w->items[i].d.disabled) continue;
            if (ToLowerW(w->items[i].d.text).find(word) != std::wstring::npos) {
                outIndex = i;
                return true;
            }
        }
    }
    return false;
}

// ------------------------------------------------------------
//  window lifecycle
// ------------------------------------------------------------
static void CloseAllWindows(UiState* ui, bool canceled) {
    if (ui->done) return;
    ui->canceled = canceled;
    ui->done = true;
    std::vector<UiWindow*> copy = ui->wins;
    ui->wins.clear();
    for (auto* w : copy) {
        if (w->hwnd && IsWindow(w->hwnd)) DestroyWindow(w->hwnd);
        else delete w;
    }
}

// Mouse capture keeps delivering clicks to the owning menu window even when the
// pointer sits on another window (the desktop included), so a click that lands
// outside every menu window has to close the chain by hand.
static bool PointInAnyMenuWindow(UiState* ui, POINT pt) {
    if (!ui) return false;
    for (auto* win : ui->wins) {
        if (!win->hwnd || !IsWindow(win->hwnd)) continue;
        RECT r;
        if (GetWindowRect(win->hwnd, &r) && PtInRect(&r, pt)) return true;
    }
    return false;
}

// Topmost menu window under a screen point. Windows are created parent first, so the
// last match is the deepest / topmost one.
static UiWindow* MenuWindowAt(UiState* ui, POINT pt) {
    UiWindow* found = NULL;
    if (!ui) return NULL;
    for (auto* win : ui->wins) {
        if (!win->hwnd || !IsWindow(win->hwnd)) continue;
        RECT r;
        if (GetWindowRect(win->hwnd, &r) && PtInRect(&r, pt)) found = win;
    }
    return found;
}

static UiWindow* RootWindowOf(UiState* ui) {
    if (!ui) return NULL;
    for (auto* win : ui->wins) if (win->isRoot) return win;
    return NULL;
}

static bool IsInSubtreeOf(UiWindow* node, UiWindow* top) {
    for (UiWindow* p = node; p; p = p->parent) if (p == top) return true;
    return false;
}

static void DestroyWindows(UiState* ui, std::vector<UiWindow*>& dead) {
    if (!ui || dead.empty()) return;
    for (auto* win : dead) {
        ui->wins.erase(std::remove(ui->wins.begin(), ui->wins.end(), win), ui->wins.end());
    }
    ++ui->suppressFocusClose;
    for (auto* win : dead) {
        if (win->hwnd && IsWindow(win->hwnd)) DestroyWindow(win->hwnd);
        else delete win;
    }
    --ui->suppressFocusClose;
}

// Drops the submenus of w that do not belong to keepItem (pass -1 for "none").
// This is what un-sticks a submenu when the pointer sweeps on to another row.
static void CloseSubmenusOf(UiWindow* w, int keepItem) {
    if (!w || !w->ui) return;
    UiState* ui = w->ui;
    std::vector<UiWindow*> dead;
    for (auto* cw : ui->wins) {
        if (cw == w || !IsInSubtreeOf(cw, w)) continue;
        UiWindow* direct = cw;
        while (direct && direct->parent != w) direct = direct->parent;
        if (direct && direct->parentItem != keepItem) dead.push_back(cw);
    }
    if (dead.empty()) return;
    DestroyWindows(ui, dead);
    if (w->hwnd && IsWindow(w->hwnd) && !ui->done) {
        SetCapture(w->hwnd);
        SetFocus(w->hwnd);
    }
}

// Closes every submenu but keeps the outermost menu on screen (native behaviour
// when the pointer walks away from the chain).
static void CloseAllSubmenus(UiState* ui) {
    if (!ui) return;
    std::vector<UiWindow*> dead;
    for (auto* win : ui->wins) if (!win->isRoot) dead.push_back(win);
    if (dead.empty()) return;
    DestroyWindows(ui, dead);
    for (auto* win : ui->wins) {
        if (win->isRoot && win->hwnd && IsWindow(win->hwnd)) {
            SetCapture(win->hwnd);
            SetFocus(win->hwnd);
            break;
        }
    }
}

// The captured window receives every mouse message, even the ones produced while the
// pointer sits on another menu layer, so it hands the message over to the window under
// the cursor and transfers the capture. Without this, sweeping the root menu after a
// submenu opened leaves the old row highlighted and the submenu stuck open.
static bool ForwardMouseToWindowUnderCursor(UiWindow* w, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!w || !w->ui) return false;
    POINT sp = { (short)LOWORD(lp), (short)HIWORD(lp) };
    ClientToScreen(hwnd, &sp);
    UiWindow* target = MenuWindowAt(w->ui, sp);
    if (!target || target == w || !target->hwnd) return false;
    POINT tp = sp;
    ScreenToClient(target->hwnd, &tp);
    if (w->hover != -1 || w->hoverBtn != -1) {
        w->hover = -1;
        w->hoverBtn = -1;
        InvalidateWindow(w);
        PaintWindow(w);
    }
    KillTimer(hwnd, kTimerSub);
    SetCapture(target->hwnd);
    SetFocus(target->hwnd);
    SendMessageW(target->hwnd, msg, wp, MAKELPARAM(tp.x, tp.y));
    return true;
}

// Runs the settings UI next to the DLL and closes the menu.
static void LaunchSettings(UiState* ui) {
    wchar_t path[MAX_PATH * 2] = {0};
    GetModuleFileNameW(g_hInst, path, MAX_PATH * 2);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *slash = 0;
    std::wstring exe = std::wstring(path) + L"\\rmgcfg.exe";
    HINSTANCE rc = ShellExecuteW(NULL, L"open", exe.c_str(), NULL, path, SW_SHOWNORMAL);
    LogW(L"launch settings [%s] rc=%p", exe.c_str(), (void*)rc);
    CloseAllWindows(ui, true);
}

static void SelectIndex(UiWindow* w, int index) {
    if (index < 0 || index >= (int)w->items.size()) return;
    UiItem& it = w->items[index];
    if (it.d.separator || it.d.disabled) return;
    if (it.d.popup || it.customPopup) {
        OpenSubmenu(w, index);
        return;
    }
    UiState* ui = w->ui;
    if (!it.pluginCmd.empty()) {
        ui->pluginCmd = it.pluginCmd;
        ui->pluginAdmin = it.pluginAdmin;
        ui->pluginConsole = it.pluginConsole;
        ui->selectedId = 0;
        ui->canceled = false;
        ui->done = true;
        std::vector<UiWindow*> copy = ui->wins;
        ui->wins.clear();
        for (auto* win : copy) {
            if (win->hwnd && IsWindow(win->hwnd)) DestroyWindow(win->hwnd);
            else delete win;
        }
        return;
    }
    if (it.d.id == 0) {
        CloseAllWindows(ui, true);
        return;
    }
    ui->selectedId = it.d.id;
    ui->canceled = false;
    ui->done = true;
    std::vector<UiWindow*> copy = ui->wins;
    ui->wins.clear();
    for (auto* win : copy) {
        if (win->hwnd && IsWindow(win->hwnd)) DestroyWindow(win->hwnd);
        else delete win;
    }
}

static void ClearCaptureAndClose(UiWindow* w) {
    if (!w) return;
    UiWindow* p = w->parent;
    UiState* ui = w->ui;
    if (!p || !ui) return;
    ui->wins.erase(std::remove(ui->wins.begin(), ui->wins.end(), w), ui->wins.end());
    if (w->hwnd && IsWindow(w->hwnd)) DestroyWindow(w->hwnd);
    else delete w;
    if (p->hwnd && IsWindow(p->hwnd)) {
        SetCapture(p->hwnd);
        SetFocus(p->hwnd);
        InvalidateWindow(p);
        PaintWindow(p);
    }
}

static UiWindow* CreateMenuWindow(UiState* ui, std::vector<UiItem> items, bool isRoot,
                                  UiWindow* parent, int parentItem, POINT anchor) {
    if (items.empty()) return NULL;
    UiWindow* w = new UiWindow();
    w->ui = ui;
    w->dpi = ui->dpi;
    w->isRoot = isRoot;
    w->parent = parent;
    w->parentItem = parentItem;
    w->items = std::move(items);
    LayoutWindow(w, anchor);
    ui->wins.push_back(w);
    EnsureMenuClass(ui->hinst);
    // Always layered: the card plus its shadow go to the compositor as a genuinely transparent
    // layer, so nothing outside the rounded rect is ever painted and the animation can scale
    // and fade the card without leaving a square of stale desktop behind.
    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED;
    w->hwnd = CreateWindowExW(exStyle, kMenuClass, L"",
        WS_POPUP, w->rect.left, w->rect.top, w->winW, w->winH,
        ui->owner, NULL, ui->hinst, w);
    if (!w->hwnd) {
        ui->wins.erase(std::remove(ui->wins.begin(), ui->wins.end(), w), ui->wins.end());
        delete w;
        return NULL;
    }
    Bitmap32 capture;
    if (!CaptureScreenRect(w->rect.left, w->rect.top, w->winW, w->winH, capture)) {
        DestroyWindow(w->hwnd);
        return NULL;
    }
    const AcrylicConfig& ac = ui->cfg.acrylic;
    Bitmap32 acrylic;
    if (ac.enabled) {
        int blur = ScalePx(std::max(2, ac.blurRadius), w->dpi);
        BuildAcrylicBackdrop(capture, blur, ac.tint, ac.lightTint, ac.opacity, ac.noise, ac.autoTint, acrylic);
    } else {
        acrylic = capture;
    }
    CompositeMenuLayer(acrylic,
        w->contentX, w->contentY, w->contentW, w->contentH,
        w->corner, w->shadow, w->bg);
    w->bgReady = true;
    return w;
}

// ------------------------------------------------------------
//  pop-up animation
// ------------------------------------------------------------
static double BezierAxis(double t, double p1, double p2) {
    double mt = 1.0 - t;
    return 3.0 * mt * mt * t * p1 + 3.0 * mt * t * t * p2 + t * t * t;
}

// y of a cubic-bezier(x1,y1,x2,y2) at the given x, endpoints fixed at (0,0)/(1,1).
static double BezierEase(double x, const double b[4]) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    double lo = 0.0, hi = 1.0, t = x;
    for (int i = 0; i < 32; ++i) {
        double cx = BezierAxis(t, b[0], b[2]);
        if (cx < x) lo = t;
        else hi = t;
        t = (lo + hi) * 0.5;
    }
    return BezierAxis(t, b[1], b[3]);
}

static void AnimFrameAt(UiWindow* w, double t, int* outX, int* outY, int* outW, int* outH, int* outAlpha) {
    double e = BezierEase(t, w->animBez);
    int alpha = w->animFromOpacity + (int)((100 - w->animFromOpacity) * e + 0.5);
    int sc = w->animFromScale + (int)((100 - w->animFromScale) * e + 0.5);
    int offY = (int)(w->animFromOffY * (1.0 - e) + 0.5);
    int sw = w->winW * sc / 100;
    int sh = w->winH * sc / 100;
    if (sw < 16) sw = 16;
    if (sh < 16) sh = 16;
    *outX = w->animBase.left + (w->winW - sw) / 2;
    *outY = w->animBase.top + (w->winH - sh) / 2 + offY;
    *outW = sw;
    *outH = sh;
    *outAlpha = alpha;
}

static void ApplyAnimFrame(UiWindow* w, double t) {
    int x = 0, y = 0, cw = 0, ch = 0, alpha = 100;
    AnimFrameAt(w, t, &x, &y, &cw, &ch, &alpha);
    SetWindowPos(w->hwnd, HWND_TOPMOST, x, y, cw, ch, SWP_NOACTIVATE);
    if (w->rendered) PresentLayered(w->hwnd, w->renderBuf, cw, ch, alpha);
    else PaintWindow(w);
}

static void TickAnim(UiWindow* w) {
    if (!w || !w->animating || !w->hwnd) return;
    double t = 1.0;
    if (w->animDurMs > 0) {
        ULONGLONG now = GetTickCount64();
        t = (double)(now - w->animStart) / (double)w->animDurMs;
    }
    if (t < 1.0) {
        ApplyAnimFrame(w, t);
        return;
    }
    w->animating = false;
    KillTimer(w->hwnd, kTimerAnim);
    SetWindowPos(w->hwnd, HWND_TOPMOST, w->animBase.left, w->animBase.top, w->winW, w->winH, SWP_NOACTIVATE);
    if (w->rendered) BlitBitmap(w->hwnd, w->renderBuf);
    else PaintWindow(w);
}

// While the card is still scaled down the client box is smaller than the laid-out
// bitmap; map pointer coordinates back into layout space so hit-testing stays right.
static void MapAnimPoint(UiWindow* w, int& mx, int& my) {
    if (!w || !w->animating || !w->hwnd) return;
    RECT rc;
    GetClientRect(w->hwnd, &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0 || w->winW <= 0 || w->winH <= 0) return;
    mx = mx * w->winW / cw;
    my = my * w->winH / ch;
}

static void ShowBuiltWindow(UiWindow* w) {
    GetCursorPos(&w->shownAt);
    w->mouseMoved = false;
    w->animBase = w->rect;
    const AnimationConfig& an = w->ui->cfg.animation;
    if (an.enabled && an.durationMs > 0) {
        w->animating = true;
        w->animStart = GetTickCount64();
        w->animDurMs = an.durationMs;
        w->animFromOpacity = an.fromOpacity;
        w->animFromScale = an.fromScale;
        w->animFromOffY = an.fromOffsetY;
        w->animBez[0] = an.bx1;
        w->animBez[1] = an.by1;
        w->animBez[2] = an.bx2;
        w->animBez[3] = an.by2;
        int x = 0, y = 0, cw = 0, ch = 0, alpha = 100;
        AnimFrameAt(w, 0.0, &x, &y, &cw, &ch, &alpha);
        SetWindowPos(w->hwnd, HWND_TOPMOST, x, y, cw, ch, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetCapture(w->hwnd);
        SetFocus(w->hwnd);
        PaintWindow(w);
        // The first paint presents the full-size layer; swap in the small, faint opening frame.
        if (w->rendered) PresentLayered(w->hwnd, w->renderBuf, cw, ch, alpha);
        SetTimer(w->hwnd, kTimerAnim, kAnimFrameMs, NULL);
        return;
    }
    SetWindowPos(w->hwnd, HWND_TOPMOST, w->rect.left, w->rect.top, w->winW, w->winH,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetCapture(w->hwnd);
    SetFocus(w->hwnd);
    PaintWindow(w);
}

static void OpenSubmenu(UiWindow* w, int index) {
    if (index < 0 || index >= (int)w->items.size()) return;
    UiItem& it = w->items[index];
    if (it.d.separator || it.d.disabled) return;
    if (it.children.empty()) return;

    UiState* ui = w->ui;
    std::vector<UiWindow*> old;
    for (int i = (int)ui->wins.size() - 1; i >= 0; --i) {
        UiWindow* cw = ui->wins[i];
        if (cw->parent == w) {
            old.push_back(cw);
            ui->wins.erase(ui->wins.begin() + i);
        }
    }
    for (auto* cw : old) {
        if (cw->hwnd && IsWindow(cw->hwnd)) DestroyWindow(cw->hwnd);
        else delete cw;
    }

    POINT anchor;
    int yy = w->itemTop;
    for (int i = 0; i < index; ++i) yy += ItemHeightOf(w, w->items[i]);
    anchor.x = w->rect.left + w->winW - w->shadow + ScalePx(2, w->dpi);
    anchor.y = w->rect.top + yy;

    std::vector<UiItem> children = it.children;
    UiWindow* cw = CreateMenuWindow(ui, std::move(children), false, w, index, anchor);
    if (!cw) return;
    ShowBuiltWindow(cw);
}

// ============================================================
//  window procedure
// ============================================================
static bool g_classRegistered = false;

static void EnsureMenuClass(HINSTANCE hinst) {
    if (g_classRegistered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = MenuWndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = kMenuClass;
    if (RegisterClassExW(&wc)) g_classRegistered = true;
}

static LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    UiWindow* w = (UiWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        UiWindow* cw = (UiWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cw);
        if (cw) cw->hwnd = hwnd;
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (w) {
            if (!w->rendered) PaintWindow(w);
            else BlitBitmap(hwnd, w->renderBuf);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_KILLFOCUS: {
        // Focus moving to anything that is not one of our windows (desktop,
        // taskbar, another app) means the menu has to go away.
        if (!w) break;
        if (w->ui && w->ui->suppressFocusClose > 0) break;
        HWND nf = (HWND)wp;
        if (nf) {
            bool belongs = false;
            for (auto* win : w->ui->wins) {
                if (win->hwnd == nf) { belongs = true; break; }
            }
            if (!belongs && !w->ui->done) {
                CloseAllWindows(w->ui, true);
                return 0;
            }
        }
        break;
    }
    case WM_RBUTTONDOWN: {
        if (!w) break;
        if (ForwardMouseToWindowUnderCursor(w, hwnd, msg, wp, lp)) return 0;
        POINT sp = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ClientToScreen(hwnd, &sp);
        if (!PointInAnyMenuWindow(w->ui, sp)) {
            CloseAllWindows(w->ui, true);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (!w) break;
        POINT sp = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ClientToScreen(hwnd, &sp);
        UiWindow* under = MenuWindowAt(w->ui, sp);
        UiWindow* root = RootWindowOf(w->ui);
        if (root && root->hwnd && IsWindow(root->hwnd)) {
            if (under) KillTimer(root->hwnd, kTimerLeave);
            else SetTimer(root->hwnd, kTimerLeave, kLeaveDelayMs, NULL);
        }
        if (under && under != w && ForwardMouseToWindowUnderCursor(w, hwnd, msg, wp, lp)) {
            return 0;
        }
        if (!under) {
            // Off the whole chain: keep the current highlight, the leave timer folds
            // the submenus after a short grace period (diagonal travel into a submenu).
            break;
        }
        if (!w->mouseMoved) {
            POINT cp;
            GetCursorPos(&cp);
            if (cp.x == w->shownAt.x && cp.y == w->shownAt.y) break;
            w->mouseMoved = true;
        }
        int mx = (short)LOWORD(lp);
        int my = (short)HIWORD(lp);
        MapAnimPoint(w, mx, my);
        int hit = HitItem(w, mx, my);
        int hb = HitButton(w, mx, my);
        if (hit != w->hover || hb != w->hoverBtn) {
            w->hover = hit;
            w->hoverBtn = hb;
            KillTimer(hwnd, kTimerSub);
            int keep = -1;
            if (hit >= 0) {
                UiItem& it = w->items[hit];
                if (!it.d.separator && !it.d.disabled && !it.children.empty()) {
                    keep = hit;
                    SetTimer(hwnd, kTimerSub, w->ui->hoverDelayMs, NULL);
                }
            }
            // A submenu belongs to the row that spawned it; once the pointer moves on
            // to another row (or to blank space) it has to fold away right there.
            CloseSubmenusOf(w, keep);
            InvalidateWindow(w);
            PaintWindow(w);
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        if (!w) break;
        if (ForwardMouseToWindowUnderCursor(w, hwnd, msg, wp, lp)) return 0;
        {
            POINT sp = { (short)LOWORD(lp), (short)HIWORD(lp) };
            ClientToScreen(hwnd, &sp);
            if (!PointInAnyMenuWindow(w->ui, sp)) {
                CloseAllWindows(w->ui, true);
                return 0;
            }
        }
        int mx = (short)LOWORD(lp);
        int my = (short)HIWORD(lp);
        MapAnimPoint(w, mx, my);
        int hb = HitButton(w, mx, my);
        if (hb >= 0) {
            const std::wstring cmd = ToLowerW(w->ui->cfg.buttons[hb].cmd);
            if (cmd == L"rmgcfg" || cmd == L"config" || cmd == L"settings-ui") {
                LaunchSettings(w->ui);
                break;
            }
            bool enabled = hb < (int)w->ui->btnEnabled.size() ? w->ui->btnEnabled[hb] : false;
            int idx = -1;
            if (enabled && FindItemCmd(w, w->ui->cfg.buttons[hb].cmd, idx)) {
                SelectIndex(w, idx);
            }
            break;
        }
        int hit = HitItem(w, mx, my);
        if (hit >= 0 && !w->items[hit].d.separator && !w->items[hit].d.disabled) {
            SelectIndex(w, hit);
        }
        break;
    }
    case WM_TIMER:
        if (wp == kTimerAnim) {
            TickAnim(w);
        } else if (wp == kTimerSub && w) {
            KillTimer(hwnd, kTimerSub);
            if (w->hover >= 0) {
                UiItem& it = w->items[w->hover];
                if (!it.d.separator && !it.d.disabled && !it.children.empty()) {
                    OpenSubmenu(w, w->hover);
                }
            }
        } else if (wp == kTimerLeave && w && w->ui && !w->ui->done) {
            KillTimer(hwnd, kTimerLeave);
            POINT cp;
            GetCursorPos(&cp);
            if (!MenuWindowAt(w->ui, cp)) {
                CloseAllSubmenus(w->ui);
                if (w->hover != -1 || w->hoverBtn != -1) {
                    w->hover = -1;
                    w->hoverBtn = -1;
                    InvalidateWindow(w);
                    PaintWindow(w);
                }
            }
        }
        break;
    case WM_MOUSEWHEEL: {
        if (!w) break;
        {
            // WM_MOUSEWHEEL carries screen coordinates already.
            POINT sp = { (short)LOWORD(lp), (short)HIWORD(lp) };
            UiWindow* under = MenuWindowAt(w->ui, sp);
            if (under && under != w && under->hwnd && IsWindow(under->hwnd)) {
                SendMessageW(under->hwnd, WM_MOUSEWHEEL, wp, lp);
                return 0;
            }
        }
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (delta > 0) w->firstIndex -= 1;
        else w->firstIndex += 1;
        if (w->firstIndex < 0) w->firstIndex = 0;
        if (w->firstIndex > w->maxFirst) w->firstIndex = w->maxFirst;
        InvalidateWindow(w);
        PaintWindow(w);
        break;
    }
    case WM_KEYDOWN: {
        if (!w) break;
        int key = (int)wp;
        if (key == VK_UP || key == VK_DOWN) {
            int dir = (key == VK_UP) ? -1 : 1;
            int idx = w->hover;
            for (int step = 0; step < (int)w->items.size(); ++step) {
                idx += dir;
                if (idx < 0) idx = (int)w->items.size() - 1;
                if (idx >= (int)w->items.size()) idx = 0;
                if (!w->items[idx].d.separator && !w->items[idx].d.disabled) break;
            }
            if (idx >= 0 && idx < (int)w->items.size()) {
                w->hover = idx;
                EnsureVisible(w, idx);
                InvalidateWindow(w);
                PaintWindow(w);
            }
        } else if (key == VK_RIGHT) {
            if (w->hover >= 0) OpenSubmenu(w, w->hover);
        } else if (key == VK_LEFT) {
            ClearCaptureAndClose(w);
        } else if (key == VK_RETURN) {
            if (w->hover >= 0) SelectIndex(w, w->hover);
        } else if (key == VK_ESCAPE) {
            // Native menus close the whole chain on ESC, submenus included.
            CloseAllWindows(w->ui, true);
        } else if (key == VK_HOME) {
            w->firstIndex = 0;
            w->hover = 0;
            InvalidateWindow(w);
            PaintWindow(w);
        } else if (key == VK_END) {
            w->firstIndex = w->maxFirst;
            w->hover = (int)w->items.size() - 1;
            InvalidateWindow(w);
            PaintWindow(w);
        }
        break;
    }
    case WM_CAPTURECHANGED: {
        if (!w) break;
        if (w->ui && w->ui->suppressFocusClose > 0) break;
        HWND nw = (HWND)lp;
        if (nw && nw != hwnd) {
            UiState* ui = w->ui;
            bool belongs = false;
            for (auto* win : ui->wins) {
                if (win->hwnd == nw) { belongs = true; break; }
            }
            if (!belongs && !ui->done) {
                CloseAllWindows(ui, true);
                return 0;
            }
        }
        break;
    }
    case WM_DESTROY: {
        if (w) {
            UiState* ui = w->ui;
            if (ui) {
                ui->wins.erase(std::remove(ui->wins.begin(), ui->wins.end(), w), ui->wins.end());
            }
            delete w;
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================
//  public entry
// ============================================================
CustomMenuResult ShowCustomMenu(HMENU hmenu, const MenuData& data, int x, int y, HWND owner, UINT flags) {
    CustomMenuResult res;
    UiState ui;
    ui.cfg = LoadConfig();
    if (!ui.cfg.enabled) return res;
    ui.hinst = g_hInst;
    ui.owner = owner;
    POINT pt = { x, y };
    ui.dpi = DpiForPoint(pt);
    ui.hoverDelayMs = ui.cfg.style.hoverDelayMs;
    int fontPx = (int)llround((double)ui.cfg.style.fontSize * ui.dpi / 72.0);
    if (fontPx < 10) fontPx = 10;
    std::wstring family = ui.cfg.style.fontFamily;
    if (!ui.cfg.style.fontFile.empty()) {
        static std::wstring s_fontFile;
        static std::wstring s_family;
        if (s_fontFile != ui.cfg.style.fontFile) {
            s_fontFile = ui.cfg.style.fontFile;
            s_family.clear();
            if (LoadFontFilePrivate(s_fontFile)) s_family = FontFamilyFromFile(s_fontFile);
            LogW(L"font file [%s] -> family [%s]", s_fontFile.c_str(), s_family.c_str());
        }
        if (!s_family.empty()) family = s_family;
    }
    ui.font = MakeMenuFont(family, fontPx, FW_NORMAL);
    if (!ui.font) ui.font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    ui.iconFont = MakeMenuFont(L"Segoe MDL2 Assets", ScalePx(std::max(12, ui.cfg.style.iconSize), ui.dpi), FW_NORMAL);
    if (!ui.iconFont) ui.iconFont = ui.font;

    std::vector<UiItem> items;
    ConvertMenu(data, items);
    // Record what the shell actually offered so the settings UI can list the
    // real vendor entries instead of asking the user to type keywords.
    ObserveMenuItems(data.items);
    ApplyPlugins(&ui, data, items);
    ApplyThirdParty(&ui, items, true);
    if (items.empty()) return res;

    ui.wins.reserve(8);
    UiWindow* root = CreateMenuWindow(&ui, std::move(items), true, NULL, -1, pt);
    if (!root) {
        if (ui.font) DeleteObject(ui.font);
        if (ui.iconFont) DeleteObject(ui.iconFont);
        return res;
    }
    ui.btnEnabled.assign(ui.cfg.buttons.size(), false);
    for (size_t i = 0; i < ui.cfg.buttons.size(); ++i) {
        const std::wstring cmd = ToLowerW(ui.cfg.buttons[i].cmd);
        if (cmd == L"rmgcfg" || cmd == L"config" || cmd == L"settings-ui") {
            ui.btnEnabled[i] = true;
            continue;
        }
        int idx = -1;
        ui.btnEnabled[i] = FindItemCmd(root, ui.cfg.buttons[i].cmd, idx);
    }
    root->hover = -1;
    ShowBuiltWindow(root);

    MSG msg;
    while (!ui.done) {
        BOOL r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // ensure everything is destroyed
    {
        std::vector<UiWindow*> copy = ui.wins;
        ui.wins.clear();
        for (auto* w : copy) {
            if (w->hwnd && IsWindow(w->hwnd)) DestroyWindow(w->hwnd);
            else delete w;
        }
    }
    if (ui.font) DeleteObject(ui.font);
    if (ui.iconFont) DeleteObject(ui.iconFont);
    res.shown = true;
    res.canceled = ui.canceled;
    res.id = ui.selectedId;
    res.pluginCommand = ui.pluginCmd;
    res.pluginAdmin = ui.pluginAdmin;
    res.pluginConsole = ui.pluginConsole;
    return res;
}
