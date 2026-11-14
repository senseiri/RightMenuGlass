#include "plugin.h"
#include "config.h"
#include "util.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shldisp.h>
#include <exdisp.h>
#include <shlguid.h>
#include <servprov.h>
#include <shlwapi.h>
#include <algorithm>

// ------------------------------------------------------------
//  helpers
// ------------------------------------------------------------
static std::wstring JoinW(const std::vector<std::wstring>& v) {
    std::wstring s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += L", ";
        s += v[i];
    }
    return s;
}

static std::wstring ToLowerStr(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = (wchar_t)towlower(c);
    return r;
}

static bool EndsWithI(const std::wstring& s, const std::wstring& suffix) {
    if (suffix.size() > s.size()) return false;
    return _wcsicmp(s.c_str() + (s.size() - suffix.size()), suffix.c_str()) == 0;
}

static std::wstring FileNameOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

static std::wstring DirOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(0, p);
}

static std::wstring ExtensionOf(const std::wstring& path) {
    std::wstring name = FileNameOf(path);
    size_t p = name.find_last_of(L'.');
    if (p == std::wstring::npos || p == 0) return L"";
    return ToLowerStr(name.substr(p));
}

static std::wstring QuoteArg(const std::wstring& s) {
    if (s.find_first_of(L" \t\"") == std::wstring::npos) return s;
    std::wstring q = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') q += L'\\';
        q += c;
    }
    q += L"\"";
    return q;
}

static std::vector<std::wstring> ReadStringArray(const JsonValue* v) {
    std::vector<std::wstring> out;
    if (!v || v->type != JsonValue::Array) return out;
    for (const auto& e : v->arr) {
        if (e.type == JsonValue::String) out.push_back(e.str);
    }
    return out;
}

// ------------------------------------------------------------
//  plugin loading
// ------------------------------------------------------------
static bool ParsePlugin(const std::wstring& path, Plugin& p, std::wstring& err) {
    std::string bytes = ReadFileBytes(path);
    if (bytes.empty()) { err = L"cannot read file"; return false; }
    std::wstring text = Utf8ToWide(bytes);
    bool ok = false;
    JsonValue root = ParseJson(text, &ok);
    if (!ok || root.type != JsonValue::Object) { err = L"invalid json"; return false; }
    p.sourceFile = path;
    if (const JsonValue* v = root.get(L"name")) p.name = v->asStr(L"");
    if (p.name.empty()) p.name = FileNameOf(path);
    if (const JsonValue* v = root.get(L"version")) p.version = v->asStr(L"");
    if (const JsonValue* v = root.get(L"author")) p.author = v->asStr(L"");
    if (const JsonValue* v = root.get(L"enabled")) p.enabled = v->asBool(true);
    if (const JsonValue* v = root.get(L"order")) p.order = v->asInt(100);
    if (const JsonValue* v = root.get(L"group")) {
        if (v->type == JsonValue::Bool) p.groupSubmenu = v->b;
        else if (v->type == JsonValue::String) { p.groupSubmenu = true; p.groupTitle = v->str; }
    }
    if (p.groupTitle.empty()) p.groupTitle = L"\u63d2\u4ef6";

    if (const JsonValue* m = root.get(L"match")) {
        p.contexts = ReadStringArray(m->get(L"contexts"));
        p.extensions = ReadStringArray(m->get(L"extensions"));
        p.nameContains = ReadStringArray(m->get(L"nameContains"));
        p.pathContains = ReadStringArray(m->get(L"pathContains"));
    } else {
        // also accept the flags at top level
        p.contexts = ReadStringArray(root.get(L"contexts"));
        p.extensions = ReadStringArray(root.get(L"extensions"));
    }

    const JsonValue* items = root.get(L"items");
    if (!items || items->type != JsonValue::Array || items->arr.empty()) { err = L"no items"; return false; }
    for (const auto& e : items->arr) {
        if (e.type != JsonValue::Object) continue;
        PluginAction a;
        if (const JsonValue* v = e.get(L"text")) a.text = v->asStr(L"");
        if (a.text.empty()) continue;
        if (const JsonValue* v = e.get(L"icon")) a.icon = v->asStr(L"");
        if (const JsonValue* v = e.get(L"command")) a.command = v->asStr(L"");
        if (const JsonValue* v = e.get(L"admin")) a.admin = v->asBool(false);
        if (const JsonValue* v = e.get(L"console")) a.console = v->asBool(false);
        if (const JsonValue* v = e.get(L"separatorAfter")) a.separatorAfter = v->asBool(false);
        if (a.command.empty()) { err = L"item without command"; continue; }
        p.actions.push_back(a);
    }
    if (p.actions.empty()) { err = L"no valid items"; return false; }
    return true;
}

PluginSet LoadPlugins(bool verbose) {
    PluginSet ps;
    std::wstring dir = GetLocalAppDataDir() + L"\\RightMenuGlass\\plugins";
    EnsureDirExists(dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return ps;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring path = dir + L"\\" + fd.cFileName;
        Plugin p;
        std::wstring err;
        if (ParsePlugin(path, p, err)) {
            ++ps.loadedFiles;
            if (verbose) LogW(L"plugin loaded: %s (%d actions)", p.name.c_str(), (int)p.actions.size());
            ps.plugins.push_back(p);
        } else {
            std::wstring msg = fd.cFileName + std::wstring(L": ") + err;
            ps.errors.push_back(msg);
            LogW(L"plugin error: %s", msg.c_str());
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(ps.plugins.begin(), ps.plugins.end(), [](const Plugin& a, const Plugin& b) {
        if (a.order != b.order) return a.order < b.order;
        return a.name < b.name;
    });
    return ps;
}

// ------------------------------------------------------------
//  matching
// ------------------------------------------------------------
static bool ContextMatches(const Plugin& p, const MenuContext& ctx) {
    if (p.contexts.empty()) return true;
    for (const auto& c : p.contexts) {
        std::wstring lc = ToLowerStr(c);
        if (lc == L"any") return true;
        if (lc == L"files" && ctx.hasFiles && !ctx.isBackground) return true;
        if (lc == L"desktop" && ctx.isDesktop) return true;
        if (lc == L"folder" && ctx.isFolderWindow) return true;
        if (lc == L"background" && ctx.isBackground) return true;
    }
    return false;
}

bool PluginMatches(const Plugin& p, const MenuContext& ctx) {
    if (!p.enabled) return false;
    if (!ContextMatches(p, ctx)) return false;
    if (!p.extensions.empty()) {
        bool ok = false;
        for (const auto& e : p.extensions) {
            if (e == L"*") { ok = true; break; }
            for (const auto& path : ctx.selectedPaths) {
                if (EndsWithI(path, e)) { ok = true; break; }
            }
            if (ok) break;
        }
        if (!ok) return false;
    }
    if (!p.nameContains.empty()) {
        bool ok = false;
        for (const auto& needle : p.nameContains) {
            for (const auto& path : ctx.selectedPaths) {
                if (FileNameOf(path).find(needle) != std::wstring::npos) { ok = true; break; }
            }
            if (ok) break;
        }
        if (!ok) return false;
    }
    if (!p.pathContains.empty()) {
        bool ok = false;
        for (const auto& needle : p.pathContains) {
            for (const auto& path : ctx.selectedPaths) {
                if (path.find(needle) != std::wstring::npos) { ok = true; break; }
            }
            if (ok) break;
        }
        if (!ok) return false;
    }
    return true;
}

// ------------------------------------------------------------
//  command expansion
// ------------------------------------------------------------
static std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::wstring ExpandCommandVars(const std::wstring& tmpl, const MenuContext& ctx) {
    std::wstring first = ctx.selectedPaths.empty() ? L"" : ctx.selectedPaths[0];
    std::wstring joined;
    for (size_t i = 0; i < ctx.selectedPaths.size(); ++i) {
        if (i) joined += L" ";
        joined += QuoteArg(ctx.selectedPaths[i]);
    }
    std::wstring out = tmpl;
    out = ReplaceAll(out, L"%1", first);
    out = ReplaceAll(out, L"%D", DirOf(first));
    out = ReplaceAll(out, L"%N", FileNameOf(first));
    out = ReplaceAll(out, L"%S", joined);
    out = ReplaceAll(out, L"%C", std::to_wstring(ctx.selectedPaths.size()));
    out = ReplaceAll(out, L"%E", ExtensionOf(first));
    return out;
}

bool RunPluginCommand(const std::wstring& cmd, bool admin, bool console) {
    if (cmd.empty()) return false;
    LogW(L"plugin run: [%s] admin=%d", cmd.c_str(), admin ? 1 : 0);
    if (admin) {
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = cmd.c_str();
        sei.nShow = console ? SW_SHOWNORMAL : SW_HIDE;
        if (!ShellExecuteExW(&sei)) {
            LogW(L"ShellExecuteEx(runas) failed: %lu", GetLastError());
            return false;
        }
        if (sei.hProcess) CloseHandle(sei.hProcess);
        return true;
    }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = console ? SW_SHOWNORMAL : SW_HIDE;
    PROCESS_INFORMATION pi = {};
    DWORD flags = console ? 0 : CREATE_NO_WINDOW;
    if (!CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        LogW(L"CreateProcess failed: %lu, cmd=[%s]", err, cmd.c_str());
        // fall back to ShellExecute (lets Windows resolve documents and URLs)
        HINSTANCE r = ShellExecuteW(NULL, L"open", cmd.c_str(), NULL, NULL, console ? SW_SHOWNORMAL : SW_HIDE);
        return (INT_PTR)r > 32;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ------------------------------------------------------------
//  shell selection
// ------------------------------------------------------------
static HRESULT ViewForWindow(HWND top, IShellView** outView) {
    *outView = nullptr;
    IShellWindows* psw = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&psw));
    if (FAILED(hr)) return hr;
    IDispatch* pdisp = NULL;
    VARIANT vLoc; VariantInit(&vLoc);
    VARIANT vRoot; VariantInit(&vRoot);
    long found = 0;
    wchar_t cls[64] = {0};
    GetClassNameW(top, cls, 64);
    bool desktop = (_wcsicmp(cls, L"Progman") == 0 || _wcsicmp(cls, L"WorkerW") == 0 ||
                    _wcsicmp(cls, L"Shell_TrayWnd") == 0);
    if (desktop) {
        hr = psw->FindWindowSW(&vRoot, &vRoot, SWC_DESKTOP, &found, SWFO_NEEDDISPATCH, &pdisp);
    } else {
        vLoc.vt = VT_I4;
        vLoc.lVal = (long)(INT_PTR)top;
        hr = psw->FindWindowSW(&vLoc, &vRoot, SWC_BROWSER, &found, SWFO_NEEDDISPATCH, &pdisp);
    }
    psw->Release();
    if (FAILED(hr) || !pdisp) return FAILED(hr) ? hr : E_FAIL;

    IServiceProvider* psp = NULL;
    hr = pdisp->QueryInterface(IID_PPV_ARGS(&psp));
    pdisp->Release();
    if (FAILED(hr)) return hr;
    IShellBrowser* psb = NULL;
    hr = psp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&psb));
    psp->Release();
    if (FAILED(hr)) return hr;
    hr = psb->QueryActiveShellView(outView);
    psb->Release();
    return hr;
}

std::vector<std::wstring> GetShellSelection(HWND owner, const POINT* screenPt) {
    (void)screenPt;
    std::vector<std::wstring> out;
    if (!owner) return out;
    bool comInit = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    HWND top = GetAncestor(owner, GA_ROOT);
    if (!top) top = owner;
    IShellView* view = NULL;
    if (SUCCEEDED(ViewForWindow(top, &view)) && view) {
        IDataObject* pdo = NULL;
        if (SUCCEEDED(view->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&pdo))) && pdo) {
            FORMATETC fe = {(CLIPFORMAT)CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM sm = {};
            if (SUCCEEDED(pdo->GetData(&fe, &sm))) {
                HDROP hd = (HDROP)GlobalLock(sm.hGlobal);
                if (hd) {
                    UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
                    for (UINT i = 0; i < n; ++i) {
                        wchar_t buf[1024];
                        if (DragQueryFileW(hd, i, buf, 1024)) out.push_back(buf);
                    }
                    GlobalUnlock(sm.hGlobal);
                }
                ReleaseStgMedium(&sm);
            }
            pdo->Release();
        }
        view->Release();
    }
    if (comInit) CoUninitialize();
    LogW(L"shell selection: %d item(s)", (int)out.size());
    return out;
}

MenuContext BuildMenuContext(HWND owner, const POINT* screenPt, const std::vector<std::wstring>& menuTexts) {
    MenuContext ctx;
    if (owner) {
        wchar_t cls[64] = {0};
        GetClassNameW(owner, cls, 64);
        ctx.ownerClass = cls;
        HWND top = GetAncestor(owner, GA_ROOT);
        wchar_t topCls[64] = {0};
        if (top) GetClassNameW(top, topCls, 64);
        ctx.isDesktop = (_wcsicmp(topCls, L"Progman") == 0 || _wcsicmp(topCls, L"WorkerW") == 0);
        ctx.isFolderWindow = (_wcsicmp(topCls, L"CabinetWClass") == 0 || _wcsicmp(topCls, L"ExploreWClass") == 0);
    }
    bool hasView = false, hasSort = false, hasOpen = false, hasNew = false, hasPaste = false, hasRefresh = false;
    for (const auto& t : menuTexts) {
        if (t.find(L"\u67e5\u770b") != std::wstring::npos) hasView = true;         // 查看
        if (t.find(L"\u6392\u5e8f\u65b9\u5f0f") != std::wstring::npos) hasSort = true; // 排序方式
        if (t.find(L"\u5237\u65b0") != std::wstring::npos) hasRefresh = true;      // 刷新
        if (t.find(L"\u65b0\u5efa") != std::wstring::npos) hasNew = true;          // 新建
        if (t.find(L"\u7c98\u8d34") != std::wstring::npos) hasPaste = true;        // 粘贴
        if (t.find(L"\u6253\u5f00") != std::wstring::npos) hasOpen = true;         // 打开
    }
    ctx.isBackground = (hasSort || hasRefresh) && !hasOpen;
    if (!ctx.isBackground && !ctx.isDesktop && ctx.isFolderWindow && hasNew && !hasOpen) ctx.isBackground = true;
    ctx.selectedPaths = GetShellSelection(owner, screenPt);
    ctx.hasFiles = !ctx.selectedPaths.empty();
    // Empty area of a shell window: nothing selected, but paste/new are offered.
    if (!ctx.isBackground && !ctx.hasFiles && hasPaste && (hasOpen || hasNew) && !hasSort) {
        ctx.isBackground = true;
        if (!ctx.isFolderWindow && ctx.ownerClass.empty()) ctx.isFolderWindow = false;
    }
    return ctx;
}
