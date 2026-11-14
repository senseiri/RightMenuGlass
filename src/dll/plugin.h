#pragma once
#include <windows.h>
#include <string>
#include <vector>

// ------------------------------------------------------------
//  Declarative plugin system
//  Each plugin lives in %LOCALAPPDATA%\RightMenuGlass\plugins\*.json
// ------------------------------------------------------------

struct PluginAction {
    std::wstring text;
    std::wstring icon;          // Segoe MDL2 glyph, e.g. "E8A5"
    std::wstring command;       // command line template
    bool admin = false;         // run elevated
    bool console = false;       // show console window
    bool separatorAfter = false;
};

struct Plugin {
    std::wstring name;
    std::wstring version;
    std::wstring author;
    bool enabled = true;
    // matching
    std::vector<std::wstring> contexts;      // any, files, folder, background, desktop
    std::vector<std::wstring> extensions;    // ".txt", "*" for all
    std::vector<std::wstring> nameContains;
    std::vector<std::wstring> pathContains;
    // presentation
    bool groupSubmenu = false;               // put inside a shared submenu
    std::wstring groupTitle;                 // default "插件"
    int order = 100;
    std::vector<PluginAction> actions;
    std::wstring sourceFile;
};

struct MenuContext {
    bool isDesktop = false;
    bool isFolderWindow = false;
    bool isBackground = false;               // right click on empty area
    bool hasFiles = false;
    std::vector<std::wstring> selectedPaths;
    std::wstring ownerClass;
};

struct PluginSet {
    std::vector<Plugin> plugins;
    int loadedFiles = 0;
    std::vector<std::wstring> errors;
};

// Load every *.json plugin from the plugin directory (creates the folder).
PluginSet LoadPlugins(bool verbose);

// Which plugin applies to this menu context.
bool PluginMatches(const Plugin& p, const MenuContext& ctx);

// Expand %1 %D %N %S %C placeholders in a command template.
std::wstring ExpandCommandVars(const std::wstring& tmpl, const MenuContext& ctx);

// Launch a plugin command (returns false when CreateProcess failed).
bool RunPluginCommand(const std::wstring& cmd, bool admin, bool console);

// Resolve the files/folders currently selected in the shell view that owns
// 'owner'. Returns an empty vector when the selection cannot be determined.
std::vector<std::wstring> GetShellSelection(HWND owner, const POINT* screenPt);

// Build a MenuContext for the menu that is about to be shown.
MenuContext BuildMenuContext(HWND owner, const POINT* screenPt, const std::vector<std::wstring>& menuTexts);
