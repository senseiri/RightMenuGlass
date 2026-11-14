#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../dll/util.h"

HINSTANCE g_hInst = NULL;

static HWND g_main = NULL;
static HWND g_btn = NULL;
static const wchar_t* kClass = L"RMG_HookTest";

static HMENU BuildMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1001, L"\u6253\u5f00(&O)");
    AppendMenuW(m, MF_STRING, 1002, L"\u5728\u65b0\u7a97\u53e3\u4e2d\u6253\u5f00");
    AppendMenuW(m, MF_GRAYED, 1003, L"\u56fa\u5b9a\u5230\u5feb\u901f\u8bbf\u95ee");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 1004, L"\u590d\u5236(&C)\tCtrl+C");
    AppendMenuW(m, MF_STRING, 1005, L"\u526a\u5207(&X)\tCtrl+X");
    AppendMenuW(m, MF_STRING, 1006, L"\u7c98\u8d34(&P)\tCtrl+V");
    AppendMenuW(m, MF_STRING, 1007, L"\u5220\u9664(&D)\tDel");
    AppendMenuW(m, MF_STRING | MF_CHECKED, 1008, L"\u663e\u793a\u9690\u85cf\u6587\u4ef6");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING, 2001, L"7-Zip");
    AppendMenuW(sub, MF_STRING, 2002, L"WinRAR");
    AppendMenuW(sub, MF_STRING, 2003, L"Git Bash Here");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, L"7-Zip");
    AppendMenuW(m, MF_STRING, 1009, L"\u5c5e\u6027(&R)");
    return m;
}

static void RunMenuOnce(int x, int y) {
    HMENU m = BuildMenu();
    UINT r = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, x, y, g_main, NULL);
    wchar_t buf[128];
    swprintf_s(buf, L"TrackPopupMenuEx returned %u", r);
    OutputDebugStringW(buf);
    FILE* f = NULL;
    if (_wfopen_s(&f, L"out\\hook_result.txt", L"w") == 0 && f) {
        fwprintf(f, L"result=%u\n", r);
        fclose(f);
    }
    DestroyMenu(m);
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(w) == 9001) { RunMenuOnce(300, 220); return 0; }
        break;
    case WM_RBUTTONUP: {
        POINT pt; GetCursorPos(&pt);
        RunMenuOnce(pt.x, pt.y);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

static void SaveBmp32(const wchar_t* path, int w, int h, const void* bits) {
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
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    fwrite(&fh, 1, sizeof(fh), f);
    fwrite(&ih, 1, sizeof(ih), f);
    fwrite(bits, 1, dataSize, f);
    fclose(f);
}

static void GrabWindow(HWND h, const wchar_t* path) {
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
        ((std::vector<HWND>*)lp)->push_back(h);
    }
    return TRUE;
}

static DWORD WINAPI WatchThread(LPVOID) {
    HWND root = NULL;
    for (int i = 0; i < 60 && !root; ++i) {
        Sleep(100);
        std::vector<HWND> v;
        EnumWindows(EnumMenuProc, (LPARAM)&v);
        if (!v.empty()) root = v[0];
    }
    if (!root) {
        OutputDebugStringW(L"RMG hook test: no custom menu appeared\n");
        ExitProcess(2);
    }
    Sleep(500);
    GrabWindow(root, L"out\\hook_menu.bmp");
    PostMessageW(root, WM_KEYDOWN, VK_ESCAPE, 0);
    Sleep(400);
    ExitProcess(0);
}

DWORD WINAPI DriverThread(LPVOID) {
    Sleep(600);
    RunMenuOnce(300, 220);
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    LogInit();
    HMODULE dll = LoadLibraryW(L"bin\\MenuGlass.dll");
    if (!dll) { wprintf(L"LoadLibrary failed %lu\n", GetLastError()); return 1; }
    typedef void (*FnInstall)();
    typedef int (*FnInstalled)();
    FnInstall install = (FnInstall)GetProcAddress(dll, "RMG_Install");
    FnInstalled installed = (FnInstalled)GetProcAddress(dll, "RMG_Installed");
    if (!install) { wprintf(L"no RMG_Install export\n"); return 1; }
    install();
    wprintf(L"installed=%d\n", installed ? installed() : -1);

    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    g_main = CreateWindowExW(0, kClass, L"RMG hook test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             100, 100, 800, 600, NULL, NULL, wc.hInstance, NULL);
    if (!g_main) { wprintf(L"CreateWindow failed %lu\n", GetLastError()); return 1; }
    CreateThread(NULL, 0, DriverThread, NULL, 0, NULL);
    CreateThread(NULL, 0, WatchThread, NULL, 0, NULL);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
