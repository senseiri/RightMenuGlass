#include <windows.h>
#include "hook.h"
#include "util.h"

HINSTANCE g_hInst = NULL;

// Explicit test/setup entry points (stable names, no C++ mangling).
extern "C" __declspec(dllexport) void RMG_Install() { InstallHooks(); }
extern "C" __declspec(dllexport) int RMG_Installed() { return HookIsInstalled() ? 1 : 0; }

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hInst;
        DisableThreadLibraryCalls(hInst);
        LogInit();
        CreateThread(NULL, 0, InitHookThreadProc, NULL, 0, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        // reserved != NULL means the process is exiting: touching code pages then is pointless
        // and unsafe. A plain FreeLibrary (rmgctl uninject) passes NULL, so restore the hooks.
        if (reserved == NULL) UninstallHooks();
    }
    return TRUE;
}
