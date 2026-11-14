#pragma once
#include <windows.h>
#include <string>
#include <vector>

// ---------- tiny JSON ----------
struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::wstring str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::wstring, JsonValue>> obj;

    const JsonValue* get(const wchar_t* key) const;
    bool isNull() const { return type == Null; }
    bool has(const wchar_t* key) const { return get(key) != nullptr; }
    int asInt(int def) const { return type == Number ? (int)num : def; }
    bool asBool(bool def) const { return type == Bool ? b : def; }
    std::wstring asStr(const std::wstring& def) const { return type == String ? str : def; }
};

JsonValue ParseJson(const std::wstring& text, bool* ok);

// ---------- config model ----------
struct TopBarButton {
    std::wstring cmd;       // matching keyword, e.g. "copy"
    std::wstring label;     // tooltip label
    std::wstring icon;      // icon glyph or custom char
    bool enabled = true;
};

struct ColorConfig {
    bool enabled = true;
    bool perPixel = true;         // legacy flag: true == colorUnit "pixel", false == "item"
    // How much text shares one colour: "pixel" (a stroke may hold both colours),
    // "glyph" (one colour per character, characters may differ) or "item" (one colour
    // per menu entry). Kept as a string so new granularities can be added later.
    std::wstring colorUnit = L"pixel";
    std::wstring darkText = L"#000000";   // text color on light background
    std::wstring lightText = L"#FFFFFF";  // text color on dark background
    int threshold = 140;          // 0..255 luminance split
    int softness = 2;             // width of the cross-fade band around the split, 0 = hard switch
    int haloWidth = 2;            // px of contrasting outline behind the glyphs, 0 = off
    int haloAlpha = 30;           // 0..100 strength of that outline
    std::wstring extraDark = L""; // optional third color slot (unused by default)
    std::wstring extraLight = L"";
};

struct AcrylicConfig {
    bool enabled = true;
    int blurRadius = 10;     // px
    int opacity = 0;         // 0..100 tint strength
    std::wstring tint = L"#202020";       // dark backdrop tint
    bool autoTint = true;    // blend tint toward lightTint on bright backdrop
    std::wstring lightTint = L"#F3F3F5";  // light backdrop tint
    int noise = 0;           // 0..40
    int corner = 10;         // px
    int shadow = 14;         // px
    int margin = 10;         // inner padding px
};

// Pop-up animation. The time curve is a cubic bezier so "custom" can dial any feel.
struct AnimationConfig {
    bool enabled = true;
    std::wstring mode = L"elegant"; // none | fast | smooth | elegant | custom
    int durationMs = 420;           // 0 = switch the animation off
    int fromOpacity = 0;            // 0..100 starting window alpha
    int fromScale = 93;             // 90..100 starting size, percent
    int fromOffsetY = 12;           // px the card starts below its final position
    double bx1 = 0.22;              // cubic-bezier(x1, y1, x2, y2)
    double by1 = 1.0;
    double bx2 = 0.36;
    double by2 = 1.0;
};

struct MenuStyle {
    std::wstring fontFamily = L"Microsoft YaHei UI";
    std::wstring fontFile = L"";   // optional .ttf/.otf/.ttc path; overrides fontFamily when readable
    int fontSize = 9;        // pt
    int itemHeight = 32;     // logical px
    int iconSize = 16;
    int headerHeight = 32;   // logical px, height of the icon bar; buttons shrink to fit
    int minWidth = 180;
    int maxWidth = 520;
    int maxHeight = 720;
    bool showShortcut = true;
    bool showCheckmarks = true;
    int hoverDelayMs = 320;
    // Which side of the pointer the root menu opens towards: right-down | right-up |
    // left-down | left-up | auto. "auto" picks whichever side faces the middle of the screen.
    std::wstring openDirection = L"right-down";
};

// A whole vendor (identified by the icon group seen in real menus) that the
// user wants in the first level instead of inside "more options".
struct PromoteGroup {
    std::wstring id;      // provider id, e.g. "bmp:0123456789abcdef"
    std::wstring name;    // display name shown in the settings UI
    bool all = false;     // true = every entry of this vendor
};

// A single menu entry promoted out of "more options".
struct PromoteItem {
    std::wstring id;      // provider id it was observed in
    std::wstring item;    // normalised label
};

struct ThirdPartyConfig {
    bool enabled = true;
    std::wstring title = L"\u66f4\u591a\u9009\u9879";
    bool submenu = true;
    bool submenuTitleIcon = true;
    std::vector<std::wstring> keepTop;   // legacy keyword whitelist (kept for old configs)
    std::vector<std::wstring> hide;      // keywords hidden entirely
    std::vector<PromoteGroup> promoteGroups;
    std::vector<PromoteItem> promoteItems;
    bool mergePromoted = true;           // pack promoted entries into one first-level submenu
    std::wstring mergedTitle = L"\u5e38\u7528\u5de5\u5177";
};

struct Config {
    int version = 1;
    bool enabled = true;
    bool logVerbose = true;
    std::vector<TopBarButton> buttons;
    ColorConfig colors;
    AcrylicConfig acrylic;
    AnimationConfig animation;
    MenuStyle style;
    ThirdPartyConfig thirdParty;
};

Config DefaultConfig();
Config LoadConfig();
void SaveDefaultConfigIfMissing();

COLORREF ParseColorHex(const std::wstring& s, COLORREF def);
