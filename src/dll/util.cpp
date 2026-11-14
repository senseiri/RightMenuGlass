#include "util.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>
#include <cwchar>

static CRITICAL_SECTION g_logCs;
static bool g_logCsInit = false;
static HANDLE g_logFile = INVALID_HANDLE_VALUE;
static wchar_t g_logPath[MAX_PATH];

void LogInit() {
    if (!g_logCsInit) {
        InitializeCriticalSection(&g_logCs);
        g_logCsInit = true;
    }
    std::wstring dir = GetLocalAppDataDir() + L"\\RightMenuGlass\\";
    EnsureDirExists(dir);
    g_logPath[0] = 0;
    LogW(L"log init");
}

std::wstring GetLocalAppDataDir() {
    wchar_t buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, buf))) return buf;
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n > 0) return buf;
    return L"C:\\Users\\Public";
}

void EnsureDirExists(const std::wstring& dir) {
    if (dir.empty()) return;
    std::wstring cur;
    size_t i = 0;
    if (dir.size() >= 3 && dir[1] == L':' && dir[2] == L'\\') { cur = dir.substr(0, 3); i = 3; }
    for (; i < dir.size(); ++i) {
        cur += dir[i];
        if (dir[i] == L'\\' || dir[i] == L'/') {
            if (cur.size() > 3) CreateDirectoryW(cur.c_str(), NULL);
        }
    }
    CreateDirectoryW(dir.c_str(), NULL);
}

std::wstring FontFamilyFromFile(const std::wstring& path) {
    std::string data = ReadFileBytes(path);
    if (data.size() < 12) return L"";
    auto u16 = [&](size_t o) -> unsigned {
        if (o + 2 > data.size()) return 0;
        return (unsigned)((unsigned char)data[o] << 8 | (unsigned char)data[o + 1]);
    };
    auto u32 = [&](size_t o) -> unsigned {
        if (o + 4 > data.size()) return 0;
        return ((unsigned)(unsigned char)data[o] << 24) | ((unsigned)(unsigned char)data[o + 1] << 16) |
               ((unsigned)(unsigned char)data[o + 2] << 8) | (unsigned)(unsigned char)data[o + 3];
    };
    size_t base = 0;
    if (data.size() >= 16 && data.compare(0, 4, "ttcf") == 0) {
        unsigned numFonts = u32(8);
        if (numFonts == 0) return L"";
        base = u32(12);
    }
    if (base + 12 > data.size()) return L"";
    unsigned numTables = u16(base + 4);
    size_t nameOff = 0;
    for (unsigned i = 0; i < numTables; ++i) {
        size_t rec = base + 12 + (size_t)i * 16;
        if (rec + 16 > data.size()) break;
        if (data.compare(rec, 4, "name") == 0) {
            nameOff = u32(rec + 8);
            break;
        }
    }
    if (!nameOff || nameOff + 6 > data.size()) return L"";
    unsigned count = u16(nameOff + 2);
    unsigned strOff = u16(nameOff + 4);
    size_t storage = nameOff + strOff;
    std::wstring best;
    int bestScore = -1;
    for (unsigned i = 0; i < count; ++i) {
        size_t rec = nameOff + 6 + (size_t)i * 12;
        if (rec + 12 > data.size()) break;
        unsigned platform = u16(rec);
        unsigned encoding = u16(rec + 2);
        unsigned language = u16(rec + 4);
        unsigned nameId = u16(rec + 6);
        unsigned len = u16(rec + 8);
        unsigned off = u16(rec + 10);
        if (nameId != 1 && nameId != 16 && nameId != 4) continue;
        if (platform != 3 && platform != 0) continue;
        if (storage + off + len > data.size()) continue;
        std::wstring s;
        if (platform == 3 && (encoding == 0 || encoding == 1 || encoding == 10)) {
            for (unsigned k = 0; k + 1 < len; k += 2) {
                wchar_t ch = (wchar_t)((unsigned char)data[storage + off + k] << 8 | (unsigned char)data[storage + off + k + 1]);
                s += ch;
            }
        } else if (platform == 0) {
            for (unsigned k = 0; k + 1 < len; k += 2) {
                wchar_t ch = (wchar_t)((unsigned char)data[storage + off + k] << 8 | (unsigned char)data[storage + off + k + 1]);
                s += ch;
            }
        }
        if (s.empty()) continue;
        int score = 10;
        if (nameId == 16) score += 5;          // typographic family preferred
        if (nameId == 1) score += 3;
        if (language == 0x409) score += 2;     // English
        if (language == 0x804) score += 4;     // Simplified Chinese
        if (score > bestScore) { bestScore = score; best = s; }
    }
    return best;
}

bool LoadFontFilePrivate(const std::wstring& path) {
    if (path.empty() || !FileExistsW(path)) return false;
    int added = AddFontResourceExW(path.c_str(), FR_PRIVATE, 0);
    if (added > 0) {
        LogW(L"font loaded: %s (%d face(s))", path.c_str(), added);
        return true;
    }
    DWORD err = GetLastError();
    LogW(L"font load failed: %s err=%lu", path.c_str(), err);
    return false;
}

std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    wchar_t* p = wcsrchr(buf, L'\\');
    if (p) *p = 0;
    return buf;
}

std::wstring GetModulePathW(HMODULE mod) {
    wchar_t buf[MAX_PATH * 2] = {0};
    GetModuleFileNameW(mod, buf, MAX_PATH * 2);
    return buf;
}

std::wstring GetProcessImagePath() {
    wchar_t buf[MAX_PATH * 2] = {0};
    GetModuleFileNameW(NULL, buf, MAX_PATH * 2);
    return buf;
}

bool IsProcessExplorer() {
    std::wstring p = ToLowerW(GetProcessImagePath());
    return p.size() > 12 && p.substr(p.size() - 12) == L"explorer.exe";
}

std::wstring GetConfigPath() {
    return GetLocalAppDataDir() + L"\\RightMenuGlass\\config.json";
}

std::wstring GetLogPath() {
    return GetLocalAppDataDir() + L"\\RightMenuGlass\\dll.log";
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0, NULL, NULL);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, NULL, NULL);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    size_t start = 0;
    // Skip a UTF-8 BOM: Notepad and PowerShell add one and it breaks JSON parsing.
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
        start = 3;
    }
    const char* p = s.c_str() + start;
    int len = (int)(s.size() - start);
    if (len <= 0) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, p, len, NULL, 0);
    if (n <= 0) return L"";
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, p, len, &out[0], n);
    return out;
}

std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

bool ContainsW(const std::wstring& hay, const std::wstring& needle) {
    return hay.find(needle) != std::wstring::npos;
}

bool FileExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string ReadFileBytes(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return "";
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(h); return ""; }
    std::string out((size_t)sz.QuadPart, 0);
    DWORD read = 0;
    if (sz.QuadPart) ReadFile(h, &out[0], (DWORD)sz.QuadPart, &read, NULL);
    CloseHandle(h);
    return out;
}

std::wstring ReadFileTextW(const std::wstring& path) {
    return Utf8ToWide(ReadFileBytes(path));
}

bool WriteFileTextW(const std::wstring& path, const std::wstring& text) {
    EnsureDirExists(path.substr(0, path.find_last_of(L"\\")));
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string u = WideToUtf8(text);
    DWORD written = 0;
    WriteFile(h, u.c_str(), (DWORD)u.size(), &written, NULL);
    CloseHandle(h);
    return true;
}

void LogW(const wchar_t* fmt, ...) {
    if (!g_logCsInit) { InitializeCriticalSection(&g_logCs); g_logCsInit = true; }
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[2300];
    _snwprintf_s(line, _TRUNCATE, L"[%02d:%02d:%02d.%03d pid=%lu] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId(), buf);
    OutputDebugStringW(line);
    EnterCriticalSection(&g_logCs);
    CreateDirectoryW((GetLocalAppDataDir() + L"\\RightMenuGlass").c_str(), NULL);
    HANDLE h = CreateFileW(GetLogPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        std::string u = WideToUtf8(line);
        DWORD w = 0;
        WriteFile(h, u.c_str(), (DWORD)u.size(), &w, NULL);
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_logCs);
}

void LogA(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    LogW(L"%S", buf);
}
