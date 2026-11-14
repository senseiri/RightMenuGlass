#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdarg>

// ---- logging ----
void LogInit();
void LogW(const wchar_t* fmt, ...);
void LogA(const char* fmt, ...);

// ---- paths ----
std::wstring GetLocalAppDataDir();
std::wstring GetConfigPath();
std::wstring GetLogPath();
std::wstring GetExeDir();

// ---- string ----
std::string WideToUtf8(const std::wstring& s);
std::wstring Utf8ToWide(const std::string& s);
std::wstring ToLowerW(std::wstring s);
bool ContainsW(const std::wstring& hay, const std::wstring& needle);

// ---- file ----
bool FileExistsW(const std::wstring& p);
std::string ReadFileBytes(const std::wstring& path);
std::wstring ReadFileTextW(const std::wstring& path);
bool WriteFileTextW(const std::wstring& path, const std::wstring& text);

// ---- fonts ----
// Reads the family name out of a .ttf/.otf/.ttc file (name table).
std::wstring FontFamilyFromFile(const std::wstring& path);
// Adds a font file to the current process (FR_PRIVATE) so GDI can use it.
bool LoadFontFilePrivate(const std::wstring& path);

// ---- misc ----
std::wstring GetModulePathW(HMODULE mod);
std::wstring GetProcessImagePath();
bool IsProcessExplorer();
void EnsureDirExists(const std::wstring& dir);
