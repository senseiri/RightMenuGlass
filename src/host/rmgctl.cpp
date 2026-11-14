#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

static DWORD g_silent = 0;

static void Say(const wchar_t* fmt, ...) {
    if (g_silent) return;
    va_list ap;
    va_start(ap, fmt);
    vwprintf(fmt, ap);
    va_end(ap);
    wprintf(L"\n");
}

static std::vector<DWORD> FindProcesses(const wchar_t* exeName) {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

static bool EnableDebugPrivilege() {
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    LUID luid;
    bool ok = false;
    if (LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &luid)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL) != 0;
    }
    CloseHandle(token);
    return ok;
}

static bool FindModuleBase(DWORD pid, const wchar_t* dllPath, HMODULE* outBase) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    const wchar_t* base = wcsrchr(dllPath, L'\\');
    base = base ? base + 1 : dllPath;
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, base) == 0) {
                if (outBase) *outBase = (HMODULE)me.modBaseAddr;
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

static bool IsModuleLoaded(DWORD pid, const wchar_t* dllPath) {
    return FindModuleBase(pid, dllPath, NULL);
}

// Asks the target process to FreeLibrary the DLL. The DLL restores its hooks from
// DLL_PROCESS_DETACH, so the patched import table points back at user32 first.
static bool UninjectDll(DWORD pid, const std::wstring& dllPath, std::wstring& err) {
    HMODULE mod = NULL;
    if (!FindModuleBase(pid, dllPath.c_str(), &mod)) {
        err = L"module not loaded";
        return false;
    }
    DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ;
    HANDLE proc = OpenProcess(access, FALSE, pid);
    if (!proc) {
        EnableDebugPrivilege();
        proc = OpenProcess(access, FALSE, pid);
    }
    if (!proc) {
        err = L"OpenProcess failed, error=" + std::to_wstring(GetLastError());
        return false;
    }
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE freeLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "FreeLibrary");
    HANDLE th = CreateRemoteThread(proc, NULL, 0, freeLib, (void*)mod, 0, NULL);
    if (!th) {
        err = L"CreateRemoteThread failed, error=" + std::to_wstring(GetLastError());
        CloseHandle(proc);
        return false;
    }
    WaitForSingleObject(th, 10000);
    DWORD rc = 0;
    GetExitCodeThread(th, &rc);
    CloseHandle(th);
    CloseHandle(proc);
    if (!rc) {
        err = L"FreeLibrary failed inside target";
        return false;
    }
    return true;
}

static int UninjectAll(const std::wstring& dllPath, bool verbose) {
    int removed = 0;
    for (DWORD pid : FindProcesses(L"explorer.exe")) {
        if (!IsModuleLoaded(pid, dllPath.c_str())) {
            if (verbose) Say(L"pid=%lu not loaded", pid);
            continue;
        }
        std::wstring err;
        if (UninjectDll(pid, dllPath, err)) {
            ++removed;
            if (verbose) Say(L"unloaded from pid=%lu", pid);
        } else if (verbose) {
            Say(L"uninject pid=%lu failed: %ls", pid, err.c_str());
        }
    }
    return removed;
}

static bool InjectDll(DWORD pid, const std::wstring& dllPath, std::wstring& err) {
    DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                   PROCESS_VM_READ;
    HANDLE proc = OpenProcess(access, FALSE, pid);
    if (!proc) {
        EnableDebugPrivilege();
        proc = OpenProcess(access, FALSE, pid);
    }
    if (!proc) {
        err = L"OpenProcess failed, error=" + std::to_wstring(GetLastError());
        return false;
    }
    SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        err = L"VirtualAllocEx failed, error=" + std::to_wstring(GetLastError());
        CloseHandle(proc);
        return false;
    }
    if (!WriteProcessMemory(proc, remote, dllPath.c_str(), bytes, NULL)) {
        err = L"WriteProcessMemory failed, error=" + std::to_wstring(GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE th = CreateRemoteThread(proc, NULL, 0, loadLib, remote, 0, NULL);
    if (!th) {
        err = L"CreateRemoteThread failed, error=" + std::to_wstring(GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }
    WaitForSingleObject(th, 20000);
    DWORD modBase = 0;
    GetExitCodeThread(th, &modBase);
    CloseHandle(th);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    if (!modBase) {
        err = L"LoadLibraryW returned NULL inside target (DLL blocked or missing dependency)";
        return false;
    }
    return true;
}

// Pids we already failed on. Some explorer.exe processes are short-lived leftovers that
// refuse CreateRemoteThread; retrying them forever would burn CPU every poll.
static std::vector<DWORD> g_failedPids;

static bool WasFailed(DWORD pid) {
    for (DWORD p : g_failedPids) {
        if (p == pid) return true;
    }
    return false;
}

// Drops failed pids that no longer exist, so a recycled pid gets a fresh chance.
static void PruneFailed(const std::vector<DWORD>& live) {
    std::vector<DWORD> keep;
    for (DWORD p : g_failedPids) {
        for (DWORD l : live) {
            if (l == p) {
                keep.push_back(p);
                break;
            }
        }
    }
    g_failedPids.swap(keep);
}

// Injects every explorer.exe process that does not have the DLL yet.
static int InjectAll(const std::wstring& dllPath, bool verbose) {
    std::vector<DWORD> pids = FindProcesses(L"explorer.exe");
    if (pids.empty()) {
        if (verbose) Say(L"explorer.exe not running");
        return 0;
    }
    PruneFailed(pids);
    int injected = 0;
    for (DWORD pid : pids) {
        if (IsModuleLoaded(pid, dllPath.c_str())) {
            if (verbose) Say(L"pid=%lu already loaded", pid);
            continue;
        }
        if (WasFailed(pid)) continue;
        std::wstring err;
        if (InjectDll(pid, dllPath, err)) {
            ++injected;
            if (verbose) Say(L"injected into pid=%lu", pid);
        } else {
            g_failedPids.push_back(pid);
            if (verbose) Say(L"inject pid=%lu failed: %ls", pid, err.c_str());
        }
    }
    return injected;
}

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 2] = {0};
    GetModuleFileNameW(NULL, buf, MAX_PATH * 2);
    wchar_t* p = wcsrchr(buf, L'\\');
    if (p) *p = 0;
    return buf;
}

static std::wstring DefaultDllPath() {
    return ExeDir() + L"\\MenuGlass.dll";
}

static void PrintUsage() {
    wprintf(L"RightMenuGlass control\n");
    wprintf(L"  rmgctl status  [dllpath]   list explorer processes and hook state\n");
    wprintf(L"  rmgctl inject  [dllpath]   inject into every explorer.exe process\n");
    wprintf(L"  rmgctl once    [dllpath]   wait for explorer, inject once, then exit\n");
    wprintf(L"  rmgctl watch   [dllpath]   keep injecting new explorer.exe processes\n");
    wprintf(L"  rmgctl restart [dllpath]   restart explorer.exe so a new DLL takes effect\n");
    wprintf(L"  rmgctl uninject [dllpath]  unload the DLL from every explorer.exe process\n");
    wprintf(L"  rmgctl stop                stop background watch processes\n");
    wprintf(L"  rmgctl uninstall           remove autostart entry\n");
    wprintf(L"  rmgctl install [--watch]   add autostart entry (inject-once by default)\n");
    wprintf(L"options: --silent, --watch, --no-inject (restart only)\n");
}

static bool SetAutoStart(bool enable, bool watch) {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE,
                      &key) != ERROR_SUCCESS) {
        return false;
    }
    LONG rc;
    if (enable) {
        // Default is "once": inject and exit, so nothing stays resident after logon.
        std::wstring verb = watch ? L"watch" : L"once";
        std::wstring cmd = L"\"" + ExeDir() + L"\\rmgctl.exe\" " + verb + L" --silent";
        rc = RegSetValueExW(key, L"RightMenuGlass", 0, REG_SZ, (const BYTE*)cmd.c_str(),
                            (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, L"RightMenuGlass");
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

int wmain(int argc, wchar_t** argv) {
    std::wstring cmd;
    std::wstring dllPath;
    bool haveDllArg = false;
    bool noInject = false;
    bool watchMode = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--silent") {
            g_silent = 1;
        } else if (a == L"--watch") {
            watchMode = true;
        } else if (a == L"--no-inject") {
            noInject = true;
        } else if (cmd.empty()) {
            cmd = a;
        } else {
            dllPath = a;
            haveDllArg = true;
        }
    }
    if (cmd.empty()) {
        PrintUsage();
        return 1;
    }
    if (!haveDllArg) dllPath = DefaultDllPath();

    if (cmd == L"status") {
        std::vector<DWORD> pids = FindProcesses(L"explorer.exe");
        wprintf(L"dll: %ls\n", dllPath.c_str());
        if (pids.empty()) {
            wprintf(L"no explorer.exe running\n");
            return 2;
        }
        for (DWORD pid : pids) {
            wprintf(L"explorer pid=%lu loaded=%d\n", pid, IsModuleLoaded(pid, dllPath.c_str()) ? 1 : 0);
        }
        return 0;
    }
    if (cmd == L"inject") {
        if (!g_silent) wprintf(L"dll: %ls\n", dllPath.c_str());
        int n = InjectAll(dllPath, !g_silent);
        if (!g_silent) wprintf(L"injected %d process(es)\n", n);
        return 0;
    }
    if (cmd == L"once") {
        EnableDebugPrivilege();
        // The Run key fires before the shell is fully up on slow logins, so wait for
        // explorer instead of failing immediately. Bounded so a broken session cannot hang.
        for (int waited = 0; waited < 90000; waited += 500) {
            if (!FindProcesses(L"explorer.exe").empty()) {
                int n = InjectAll(dllPath, !g_silent);
                if (!g_silent) wprintf(L"injected %d process(es), exiting\n", n);
                return 0;
            }
            Sleep(500);
        }
        if (!g_silent) wprintf(L"explorer.exe did not start within 90s, giving up\n");
        return 2;
    }
    if (cmd == L"watch") {
        EnableDebugPrivilege();
        for (;;) {
            InjectAll(dllPath, false);
            Sleep(1500);
        }
    }
    if (cmd == L"uninject") {
        if (!g_silent) wprintf(L"dll: %ls\n", dllPath.c_str());
        int n = UninjectAll(dllPath, !g_silent);
        if (!g_silent) wprintf(L"unloaded from %d process(es)\n", n);
        return 0;
    }
    if (cmd == L"stop") {
        DWORD self = GetCurrentProcessId();
        int n = 0;
        for (DWORD pid : FindProcesses(L"rmgctl.exe")) {
            if (pid == self) continue;
            HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (proc) {
                TerminateProcess(proc, 0);
                CloseHandle(proc);
                ++n;
            }
        }
        if (!g_silent) wprintf(L"stopped %d watch process(es)\n", n);
        return 0;
    }
    if (cmd == L"restart") {
        std::vector<DWORD> pids = FindProcesses(L"explorer.exe");
        for (DWORD pid : pids) {
            HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (proc) {
                TerminateProcess(proc, 0);
                CloseHandle(proc);
            }
        }
        Sleep(1200);
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        wchar_t exe[MAX_PATH] = L"C:\\Windows\\explorer.exe";
        if (CreateProcessW(exe, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        Sleep(1500);
        if (noInject) {
            Say(L"explorer restarted without injecting");
        } else {
            InjectAll(dllPath, !g_silent);
            Say(L"explorer restarted");
        }
        return 0;
    }
    if (cmd == L"install") {
        if (!SetAutoStart(true, watchMode)) {
            wprintf(L"failed to write autostart entry\n");
            return 3;
        }
        wprintf(L"autostart installed: %ls\\rmgctl.exe %ls --silent\n", ExeDir().c_str(),
                watchMode ? L"watch" : L"once");
        if (!watchMode) {
            wprintf(L"mode: inject once at logon and exit (run rmgctl inject to redo it after an explorer restart)\n");
        }
        return 0;
    }
    if (cmd == L"uninstall") {
        SetAutoStart(false, false);
        wprintf(L"autostart entry removed (running instance keeps working until explorer restarts)\n");
        return 0;
    }
    PrintUsage();
    return 1;
}