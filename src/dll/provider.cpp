#include "provider.h"
#include "util.h"
#include "config.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <map>

namespace {

std::wstring TrimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

uint64_t Fnv1a(const std::wstring& s) {
    uint64_t h = 1469598103934665603ULL;
    for (wchar_t c : s) { h ^= (uint64_t)(unsigned)c; h *= 1099511628211ULL; }
    return h;
}

std::wstring HashToHex(uint64_t h) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%016llx", (unsigned long long)h);
    return buf;
}

// Longest substring shared by every label, used as a default group name.
std::wstring CommonSubstring(const std::vector<std::wstring>& v) {
    if (v.empty()) return L"";
    std::wstring best;
    const std::wstring& first = v[0];
    for (size_t i = 0; i < first.size(); ++i) {
        for (size_t len = first.size() - i; len > best.size(); --len) {
            std::wstring cand = first.substr(i, len);
            bool all = true;
            for (size_t k = 1; k < v.size(); ++k) {
                if (v[k].find(cand) == std::wstring::npos) { all = false; break; }
            }
            if (all) { best = cand; break; }
        }
    }
    return best;
}

int Hamming64(uint64_t a, uint64_t b) {
    uint64_t x = a ^ b;
    int n = 0;
    while (x) { x &= (x - 1); ++n; }
    return n;
}

}  // namespace

std::wstring NormalizeMenuText(const std::wstring& raw) {
    std::wstring s = raw;
    size_t tab = s.find(L'\t');
    if (tab != std::wstring::npos) s = s.substr(0, tab);

    std::wstring out;
    bool inQuote = false;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'"' || c == L'\u201c' || c == L'\u201d') { inQuote = !inQuote; continue; }
        if (inQuote) continue;
        if (c == L'&') continue;
        out.push_back(c);
    }

    // Strip ellipsis dots first so a name like "全部解压缩(T)..." ends up
    // without both the dots and the accelerator group.
    while (!out.empty() && (out.back() == L'.' || out.back() == L'\u2026' || out.back() == L' ')) out.pop_back();

    // Drop a trailing accelerator group such as "(T)" or "\u2026(T)".
    size_t close = out.find_last_of(L')');
    if (close != std::wstring::npos && close + 1 == out.size()) {
        size_t open = out.find_last_of(L'(');
        if (open != std::wstring::npos && close > open + 1 && close - open <= 8) {
            bool onlyAscii = true;
            for (size_t k = open + 1; k < close; ++k) {
                wchar_t c = out[k];
                if (c > 0x7F) { onlyAscii = false; break; }
            }
            if (onlyAscii) out = out.substr(0, open);
        }
    }

    while (!out.empty() && (out.back() == L'.' || out.back() == L'\u2026' || out.back() == L' ')) out.pop_back();
    out = TrimW(out);
    return out;
}

unsigned long long HashMenuItemBitmap(HBITMAP bmp) {
    if (!bmp || (uintptr_t)bmp < 0x10000) return 0;
    BITMAP bm = {};
    if (!GetObject(bmp, sizeof(bm), &bm)) return 0;
    if (bm.bmWidth <= 0 || bm.bmHeight <= 0) return 0;

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 16;
    bi.bmiHeader.biHeight = -16;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    unsigned long long hash = 0;
    if (dib && bits) {
        HGDIOBJ oldDst = SelectObject(mem, dib);
        RECT rc = { 0, 0, 16, 16 };
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(mem, &rc, white);
        DeleteObject(white);

        HDC src = CreateCompatibleDC(NULL);
        HGDIOBJ oldSrc = SelectObject(src, bmp);
        SetStretchBltMode(mem, HALFTONE);
        StretchBlt(mem, 0, 0, 16, 16, src, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        SelectObject(src, oldSrc);
        DeleteDC(src);

        unsigned char lum[256];
        int sum = 0;
        unsigned char* p = (unsigned char*)bits;
        for (int i = 0; i < 256; ++i) {
            unsigned char* q = p + i * 4;
            int L = (q[2] * 299 + q[1] * 587 + q[0] * 114) / 1000;
            lum[i] = (unsigned char)L;
            sum += L;
        }
        int avg = sum / 256;
        for (int i = 0; i < 64; ++i) {
            int x0 = (i % 8) * 2;
            int y0 = (i / 8) * 2;
            int v = (lum[y0 * 16 + x0] + lum[y0 * 16 + x0 + 1] +
                     lum[(y0 + 1) * 16 + x0] + lum[(y0 + 1) * 16 + x0 + 1]) / 4;
            if (v > avg) hash |= (1ull << i);
        }
        SelectObject(mem, oldDst);
    }
    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    return hash ? hash : 1;
}

std::wstring ProviderIdOf(const MenuItemData& it) {
    unsigned long long h = HashMenuItemBitmap(it.hbmp);
    if (h) return L"bmp:" + HashToHex(h);
    std::wstring norm = NormalizeMenuText(it.text);
    return L"txt:" + HashToHex(Fnv1a(norm));
}

std::wstring ObservedDbPath() {
    return GetLocalAppDataDir() + L"\\RightMenuGlass\\observed.json";
}

bool LoadObservedDb(ObservedDb& db) {
    std::wstring text = ReadFileTextW(ObservedDbPath());
    if (text.empty()) return false;
    bool ok = false;
    JsonValue root = ParseJson(text, &ok);
    if (!ok) return false;
    const JsonValue* ver = root.get(L"version");
    if (!ver || (int)ver->num != 2) return false;
    const JsonValue* provs = root.get(L"providers");
    if (!provs || provs->type != JsonValue::Array) return false;
    for (const auto& pv : provs->arr) {
        ObservedProvider p;
        const JsonValue* id = pv.get(L"id");
        if (!id) continue;
        p.id = id->asStr(L"");
        if (p.id.empty()) continue;
        p.name = pv.get(L"name") ? pv.get(L"name")->asStr(L"") : L"";
        p.lastSeen = pv.get(L"lastSeen") ? (long long)pv.get(L"lastSeen")->num : 0;
        const JsonValue* items = pv.get(L"items");
        if (items && items->type == JsonValue::Array) {
            for (const auto& iv : items->arr) {
                ObservedItem oi;
                oi.text = iv.get(L"text") ? iv.get(L"text")->asStr(L"") : L"";
                oi.norm = iv.get(L"norm") ? iv.get(L"norm")->asStr(L"") : L"";
                oi.popup = iv.get(L"popup") ? iv.get(L"popup")->asBool(false) : false;
                if (!oi.norm.empty()) p.items.push_back(oi);
            }
        }
        db.providers.push_back(p);
    }
    return true;
}

namespace {

std::wstring JsonEscape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) { /* skip control chars */ }
                else out.push_back(c);
                break;
        }
    }
    return out;
}

void SaveObservedDb(const ObservedDb& db) {
    std::wstring out = L"{\n  \"version\": 2,\n  \"providers\": [\n";
    for (size_t i = 0; i < db.providers.size(); ++i) {
        const ObservedProvider& p = db.providers[i];
        out += L"    { \"id\": \"" + JsonEscape(p.id) + L"\", \"name\": \"" + JsonEscape(p.name) +
               L"\", \"lastSeen\": " + std::to_wstring(p.lastSeen) + L", \"items\": [";
        for (size_t k = 0; k < p.items.size(); ++k) {
            const ObservedItem& oi = p.items[k];
            if (k) out += L", ";
            out += L"{ \"text\": \"" + JsonEscape(oi.text) + L"\", \"norm\": \"" + JsonEscape(oi.norm) +
                   L"\", \"popup\": " + (oi.popup ? L"true" : L"false") + L" }";
        }
        out += L"] }";
        if (i + 1 < db.providers.size()) out += L",";
        out += L"\n";
    }
    out += L"  ]\n}\n";
    WriteFileTextW(ObservedDbPath(), out);
}

}  // namespace

namespace {

// Menu entries that always belong to Windows itself; they never form a vendor group.
bool IsSystemMenuText(const std::wstring& low) {
    static const wchar_t* kSys[] = {
        L"打开", L"新建", L"发送到", L"剪切", L"复制", L"粘贴", L"创建快捷方式",
        L"删除", L"重命名", L"属性", L"固定到", L"查看", L"排序方式", L"分组依据",
        L"刷新", L"授予访问权限", L"显示设置", L"个性化", L"自定义文件夹",
        L"询问", L"copilot", L"共享", L"打开方式", L"工具栏", L"布局",
    };
    for (const wchar_t* k : kSys) {
        if (ContainsW(low, k)) return true;
    }
    return false;
}

// Words too generic to prove two entries come from the same product.
bool IsGenericGlue(const std::wstring& low) {
    static const wchar_t* kGlue[] = {
        L"打开", L"文件", L"工具", L"设置", L"上传", L"下载", L"发送", L"图片",
        L"编辑", L"转换", L"压缩", L"管理", L"查看", L"新建", L"复制", L"删除",
        L"属性", L"选项", L"更多", L"文件夹", L"here",
    };
    for (const wchar_t* k : kGlue) {
        if (low == k) return true;
    }
    return false;
}

std::wstring CommonBetween(const std::wstring& a, const std::wstring& b) {
    size_t n = a.size(), m = b.size();
    if (!n || !m) return L"";
    std::vector<int> prev(m + 1, 0), cur(m + 1, 0);
    size_t best = 0, bestEnd = 0;
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            if (towlower(a[i - 1]) == towlower(b[j - 1])) {
                cur[j] = prev[j - 1] + 1;
                if ((size_t)cur[j] > best) { best = (size_t)cur[j]; bestEnd = j; }
            }
            else {
                cur[j] = 0;
            }
        }
        std::swap(prev, cur);
        std::fill(cur.begin(), cur.end(), 0);
    }
    if (best == 0) return L"";
    return b.substr(bestEnd - best, best);
}

struct Dsu {
    std::vector<int> p;
    explicit Dsu(int n) : p((size_t)n) { for (int i = 0; i < n; ++i) p[(size_t)i] = i; }
    int find(int x) { while (p[(size_t)x] != x) { p[(size_t)x] = p[(size_t)p[(size_t)x]]; x = p[(size_t)x]; } return x; }
    void join(int a, int b) { a = find(a); b = find(b); if (a != b) p[(size_t)b] = a; }
};

void CollectSubtree(HMENU h, std::vector<ObservedItem>& cand, std::vector<std::wstring>& norms, int depth) {
    if (!h || depth > 3) return;
    int n = GetMenuItemCount(h);
    for (int i = 0; i < n; ++i) {
        wchar_t buf[512] = { 0 };
        MENUITEMINFOW mii = { 0 };
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_STRING | MIIM_SUBMENU;
        mii.dwTypeData = buf;
        mii.cch = 511;
        if (!GetMenuItemInfoW(h, i, TRUE, &mii)) continue;
        std::wstring raw = buf;
        std::wstring norm = NormalizeMenuText(raw);
        if (norm.empty()) continue;
        std::wstring low = ToLowerW(norm);
        if (!IsSystemMenuText(low)) {
            ObservedItem oi;
            oi.text = raw;
            oi.norm = norm;
            oi.popup = mii.hSubMenu != nullptr;
            cand.push_back(oi);
            norms.push_back(norm);
        }
        if (mii.hSubMenu) CollectSubtree(mii.hSubMenu, cand, norms, depth + 1);
    }
}

}  // namespace

void ObserveMenuItems(const std::vector<MenuItemData>& items) {
    // One record per vendor entry of the top level. A vendor submenu (such as
    // "7-Zip >") drags its whole subtree along, but a Windows submenu
    // ("查看 >", "排序方式 >") is skipped entirely, children included.
    struct TopEntry {
        std::wstring norm;
        std::vector<ObservedItem> items;
    };
    std::vector<TopEntry> tops;
    for (const auto& it : items) {
        if (it.separator) continue;
        std::wstring norm = NormalizeMenuText(it.text);
        if (norm.empty()) continue;
        if (IsSystemMenuText(ToLowerW(norm))) continue;

        TopEntry t;
        t.norm = norm;
        ObservedItem self;
        self.text = it.text;
        self.norm = norm;
        self.popup = it.popup;
        t.items.push_back(self);

        if (it.sub) {
            std::vector<std::wstring> dummy;
            CollectSubtree(it.sub, t.items, dummy, 1);
        }
        tops.push_back(t);
    }
    if (tops.empty()) return;

    // Two top level entries sharing a distinctive substring belong together:
    // "WPS解压" / "上传或同步到WPS" / "通过WPS发送" all contain "WPS".
    Dsu dsu((int)tops.size());
    for (size_t i = 0; i < tops.size(); ++i) {
        for (size_t j = i + 1; j < tops.size(); ++j) {
            std::wstring cs = CommonBetween(tops[i].norm, tops[j].norm);
            if (cs.size() >= 3 && !IsGenericGlue(ToLowerW(cs))) dsu.join((int)i, (int)j);
        }
    }

    std::map<int, std::vector<size_t>> groups;
    for (size_t i = 0; i < tops.size(); ++i) groups[dsu.find((int)i)].push_back(i);

    ObservedDb db;
    LoadObservedDb(db);

    long long now = (long long)time(nullptr);
    for (auto& kv : groups) {
        std::vector<size_t>& idx = kv.second;
        std::vector<std::wstring> names;
        for (size_t k : idx) names.push_back(tops[k].norm);

        std::wstring name = TrimW(CommonSubstring(names));
        if (name.size() < 3) {
            std::wstring best = names[0];
            for (const auto& s : names) if (s.size() < best.size()) best = s;
            name = best.size() > 24 ? best.substr(0, 24) : best;
        }
        name = TrimW(name);
        if (name.empty()) name = L"扩展";

        ObservedProvider* p = nullptr;
        for (auto& q : db.providers) {
            if (q.name == name) { p = &q; break; }
        }
        if (!p) {
            for (auto& q : db.providers) {
                std::wstring cs = CommonBetween(q.name, name);
                if (cs.size() >= 3 && !IsGenericGlue(ToLowerW(cs))) { p = &q; break; }
            }
        }
        if (!p) {
            ObservedProvider np;
            np.id = L"grp:" + HashToHex(Fnv1a(ToLowerW(name)));
            np.name = name;
            db.providers.push_back(np);
            p = &db.providers.back();
        }
        else if (p->name != name && p->name.find(name) != std::wstring::npos) {
            // The freshly observed name is a cleaner form (no trailing accelerator).
            p->name = name;
        }

        for (size_t k : idx) {
            for (const ObservedItem& src : tops[k].items) {
                bool have = false;
                for (auto& oi : p->items) {
                    if (oi.norm == src.norm) { oi.text = src.text; oi.popup = src.popup; have = true; break; }
                }
                if (!have) p->items.push_back(src);
            }
        }
        p->lastSeen = now;
    }

    for (auto& p : db.providers) {
        if (p.name.empty()) p.name = L"扩展";
        if (p.items.size() > 24) p.items.resize(24);
    }
    std::stable_sort(db.providers.begin(), db.providers.end(),
                     [](const ObservedProvider& a, const ObservedProvider& b) {
                         return a.lastSeen > b.lastSeen;
                     });
    if (db.providers.size() > 40) db.providers.resize(40);
    SaveObservedDb(db);
}
