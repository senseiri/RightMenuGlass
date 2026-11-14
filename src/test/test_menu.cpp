#include "../dll/hook.h"
#include "../dll/util.h"
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

HINSTANCE g_hInst = NULL;

static bool g_light = false;
static std::wstring g_outDir;
static int g_subY = -1;

static void SaveBmp32(const std::wstring& path, int w, int h, const void* bits) {
    DWORD dataSize = (DWORD)w * (DWORD)h * 4;
    BITMAPFILEHEADER fh = {};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + dataSize;
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = -h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = dataSize;
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    fwrite(&fh, 1, sizeof(fh), f);
    fwrite(&ih, 1, sizeof(ih), f);
    fwrite(bits, 1, dataSize, f);
    fclose(f);
}

static void GrabWindow(HWND h, const std::wstring& path) {
    RECT rc;
    GetWindowRect(h, &rc);
    int w = rc.right - rc.left;
    int hh = rc.bottom - rc.top;
    if (w <= 0 || hh <= 0) return;
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, hh, screen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    SaveBmp32(path, w, hh, bits);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static BOOL CALLBACK EnumMenuProc(HWND h, LPARAM lp) {
    wchar_t cls[64] = {0};
    GetClassNameW(h, cls, 64);
    if (wcscmp(cls, L"RMG_CustomMenuWnd") == 0 && IsWindowVisible(h)) {
        auto* v = (std::vector<HWND>*)lp;
        v->push_back(h);
    }
    return TRUE;
}

static DWORD WINAPI ShotThread(LPVOID p) {
    (void)p;
    HWND root = NULL;
    for (int i = 0; i < 60 && !root; ++i) {
        Sleep(100);
        std::vector<HWND> v;
        EnumWindows(EnumMenuProc, (LPARAM)&v);
        if (!v.empty()) root = v[0];
    }
    if (!root) {
        OutputDebugStringW(L"test: menu window not found\n");
        return 1;
    }
    Sleep(700);
    RECT rc;
    GetWindowRect(root, &rc);
    {
        HWND bgw = FindWindowW(L"RMG_TestBg", NULL);
        RECT br = {0, 0, 0, 0};
        if (bgw) GetWindowRect(bgw, &br);
        LogW(L"shot: bg hwnd=%p visible=%d rect=%ld,%ld,%ld,%ld menu=%ld,%ld,%ld,%ld",
             (void*)bgw, bgw ? (int)IsWindowVisible(bgw) : -1,
             br.left, br.top, br.right, br.bottom, rc.left, rc.top, rc.right, rc.bottom);
        GrabWindow(GetDesktopWindow(), g_outDir + L"\\full.bmp");
    }
    if (g_light) {
        GrabWindow(root, g_outDir + L"\\menu_light.bmp");
    } else {
        GrabWindow(root, g_outDir + L"\\menu_root.bmp");
    }
    // hover the last item (third-party container) to open its submenu
    {
        int hh = rc.bottom - rc.top;
        int y = g_subY > 0 ? g_subY : (hh - 46);
        PostMessageW(root, WM_MOUSEMOVE, 0, MAKELPARAM(150, y));
        for (int i = 0; i < 20; ++i) {
            Sleep(100);
            std::vector<HWND> v;
            EnumWindows(EnumMenuProc, (LPARAM)&v);
            if (v.size() > 1) break;
        }
        std::vector<HWND> v;
        EnumWindows(EnumMenuProc, (LPARAM)&v);
        HWND sub = NULL;
        for (auto h : v) if (h != root) sub = h;
        if (sub) {
            Sleep(250);
            GrabWindow(sub, g_outDir + L"\\menu_sub.bmp");
        }
    }
    Sleep(150);
    std::vector<HWND> v;
    EnumWindows(EnumMenuProc, (LPARAM)&v);
    for (auto h : v) PostMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
    return 0;
}

static LRESULT CALLBACK BgProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        int mid = rc.right / 2;
        HBRUSH b1 = CreateSolidBrush(RGB(252, 251, 245));
        HBRUSH b2 = CreateSolidBrush(RGB(12, 12, 18));
        RECT l = {0, 0, mid, rc.bottom};
        FillRect(dc, &l, b1);
        RECT r = {mid, 0, rc.right, rc.bottom};
        FillRect(dc, &r, b2);
        DeleteObject(b1);
        DeleteObject(b2);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static HWND CreateLightBg(HINSTANCE hinst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = BgProc;
    wc.hInstance = hinst;
    wc.lpszClassName = L"RMG_TestBg";
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    HWND h = CreateWindowExW(WS_EX_TOPMOST, L"RMG_TestBg", L"bg", WS_POPUP | WS_VISIBLE,
                             0, 0, sw, sh, NULL, NULL, hinst, NULL);
    RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    UpdateWindow(h);
    Sleep(400);
    GrabWindow(h, g_outDir + L"\\bg.bmp");
    return h;
}

static HMENU BuildTestMenu() {
    HMENU root = CreatePopupMenu();
    HMENU sendTo = CreatePopupMenu();
    AppendMenuW(sendTo, MF_STRING, 2101, L"\u684C\u9762\u5FEB\u6377\u65B9\u5F0F");
    AppendMenuW(sendTo, MF_STRING, 2102, L"\u90AE\u4EF6\u63A5\u6536\u8005");
    HMENU zip = CreatePopupMenu();
    AppendMenuW(zip, MF_STRING, 2201, L"\u6DFB\u52A0\u5230\u538B\u7F29\u5305...");
    AppendMenuW(zip, MF_STRING, 2202, L"\u89E3\u538B\u5230\u5F53\u524D\u6587\u4EF6\u5939");
    HMENU rar = CreatePopupMenu();
    AppendMenuW(rar, MF_STRING, 2301, L"\u89E3\u538B\u5230 file\\");
    HMENU svn = CreatePopupMenu();
    AppendMenuW(svn, MF_STRING, 2401, L"SVN Commit...");
    AppendMenuW(svn, MF_STRING, 2402, L"SVN Update");

    AppendMenuW(root, MF_STRING | MF_DEFAULT, 1001, L"\u6253\u5F00(&O)");
    AppendMenuW(root, MF_STRING, 1002, L"\u5728\u65B0\u7A97\u53E3\u4E2D\u6253\u5F00");
    AppendMenuW(root, MF_STRING | MF_GRAYED, 1003, L"\u56FA\u5B9A\u5230\u5FEB\u901F\u8BBF\u95EE");
    AppendMenuW(root, MF_POPUP, (UINT_PTR)sendTo, L"\u53D1\u9001\u5230(&N)");
    AppendMenuW(root, MF_SEPARATOR, 0, NULL);
    AppendMenuW(root, MF_STRING, 1004, L"\u590D\u5236(&C)\tCtrl+C");
    AppendMenuW(root, MF_STRING, 1005, L"\u526A\u5207(&X)\tCtrl+X");
    AppendMenuW(root, MF_STRING, 1006, L"\u7C98\u8D34(&P)\tCtrl+V");
    AppendMenuW(root, MF_STRING, 1007, L"\u521B\u5EFA\u5FEB\u6377\u65B9\u5F0F(&S)");
    AppendMenuW(root, MF_STRING, 1008, L"\u5220\u9664(&D)\tDel");
    AppendMenuW(root, MF_STRING, 1009, L"\u91CD\u547D\u540D(&M)\tF2");
    AppendMenuW(root, MF_SEPARATOR, 0, NULL);
    AppendMenuW(root, MF_STRING | MF_CHECKED, 1010, L"\u663E\u793A\u9690\u85CF\u6587\u4EF6");
    AppendMenuW(root, MF_POPUP, (UINT_PTR)zip, L"7-Zip");
    AppendMenuW(root, MF_POPUP, (UINT_PTR)rar, L"WinRAR");
    AppendMenuW(root, MF_STRING, 1011, L"Git Bash Here");
    AppendMenuW(root, MF_STRING, 1012, L"\u4F7F\u7528 Visual Studio Code \u6253\u5F00");
    AppendMenuW(root, MF_STRING, 1013, L"\u4E0A\u4F20\u5230\u767E\u5EA6\u7F51\u76D8");
    AppendMenuW(root, MF_POPUP, (UINT_PTR)svn, L"TortoiseSVN");
    AppendMenuW(root, MF_SEPARATOR, 0, NULL);
    AppendMenuW(root, MF_STRING, 1014, L"\u5C5E\u6027(&R)");
    return root;
}

int wmain(int argc, wchar_t** argv) {
    LogInit();
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"light") == 0) g_light = true;
        else if (wcsncmp(argv[i], L"y=", 2) == 0) g_subY = _wtoi(argv[i] + 2);
    }
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI* PFN_SetCtx)(HANDLE);
    auto setCtx = (PFN_SetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (setCtx) setCtx((HANDLE)-4);
    else SetProcessDPIAware();

    g_outDir = L"out";
    EnsureDirExists(g_outDir.c_str());

    HWND bg = NULL;
    if (g_light) bg = CreateLightBg(GetModuleHandleW(NULL));

    HMENU menu = BuildTestMenu();
    MenuData data;
    bool ok = BuildMenuData(menu, data, true);
    LogW(L"test: BuildMenuData ok=%d items=%d light=%d", (int)ok, (int)data.items.size(), (int)g_light);

    CreateThread(NULL, 0, ShotThread, NULL, 0, NULL);
    CustomMenuResult res = ShowCustomMenu(menu, data, g_light ? 820 : 700, 380, bg, 0);
    LogW(L"test: result shown=%d canceled=%d id=%u", (int)res.shown, (int)res.canceled, res.id);
    DestroyMenu(menu);
    if (bg) DestroyWindow(bg);
    return 0;
}
