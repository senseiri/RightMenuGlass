#include "config.h"
#include "util.h"
#include <cmath>

// ---------------- JSON parser ----------------
namespace {

struct Parser {
    const wchar_t* p;
    const wchar_t* end;
    bool ok = true;

    void skipWs() {
        while (p < end && (*p == L' ' || *p == L'\t' || *p == L'\n' || *p == L'\r')) ++p;
    }
    bool eof() const { return p >= end; }
    wchar_t peek() const { return p < end ? *p : 0; }

    std::wstring parseString() {
        std::wstring out;
        if (peek() != L'"') { ok = false; return out; }
        ++p;
        while (p < end) {
            wchar_t c = *p++;
            if (c == L'"') return out;
            if (c == L'\\') {
                if (p >= end) break;
                wchar_t e = *p++;
                switch (e) {
                    case L'"': out += L'"'; break;
                    case L'\\': out += L'\\'; break;
                    case L'/': out += L'/'; break;
                    case L'b': out += L'\b'; break;
                    case L'f': out += L'\f'; break;
                    case L'n': out += L'\n'; break;
                    case L'r': out += L'\r'; break;
                    case L't': out += L'\t'; break;
                    case L'u': {
                        if (end - p < 4) { ok = false; return out; }
                        wchar_t hi = 0;
                        for (int i = 0; i < 4; ++i) {
                            wchar_t h = *p++;
                            hi <<= 4;
                            if (h >= L'0' && h <= L'9') hi |= (wchar_t)(h - L'0');
                            else if (h >= L'a' && h <= L'f') hi |= (wchar_t)(h - L'a' + 10);
                            else if (h >= L'A' && h <= L'F') hi |= (wchar_t)(h - L'A' + 10);
                            else { ok = false; return out; }
                        }
                        if (hi >= 0xD800 && hi <= 0xDBFF && end - p >= 6 && p[0] == L'\\' && p[1] == L'u') {
                            p += 2;
                            wchar_t lo = 0;
                            for (int i = 0; i < 4; ++i) {
                                wchar_t h = *p++;
                                lo <<= 4;
                                if (h >= L'0' && h <= L'9') lo |= (wchar_t)(h - L'0');
                                else if (h >= L'a' && h <= L'f') lo |= (wchar_t)(h - L'a' + 10);
                                else if (h >= L'A' && h <= L'F') lo |= (wchar_t)(h - L'A' + 10);
                                else { ok = false; return out; }
                            }
                            unsigned cp = 0x10000 + (((unsigned)hi - 0xD800) << 10) + ((unsigned)lo - 0xDC00);
                            out += (wchar_t)(0xD800 + ((cp - 0x10000) >> 10));
                            out += (wchar_t)(0xDC00 + ((cp - 0x10000) & 0x3FF));
                        } else {
                            out += hi;
                        }
                        break;
                    }
                    default: ok = false; return out;
                }
            } else {
                out += c;
            }
        }
        ok = false;
        return out;
    }

    JsonValue parseValue(int depth) {
        JsonValue v;
        if (depth > 64) { ok = false; return v; }
        skipWs();
        if (eof()) { ok = false; return v; }
        wchar_t c = peek();
        if (c == L'{') {
            ++p;
            v.type = JsonValue::Object;
            skipWs();
            if (peek() == L'}') { ++p; return v; }
            while (p < end) {
                skipWs();
                std::wstring key = parseString();
                if (!ok) return v;
                skipWs();
                if (peek() != L':') { ok = false; return v; }
                ++p;
                JsonValue child = parseValue(depth + 1);
                if (!ok) return v;
                v.obj.emplace_back(key, std::move(child));
                skipWs();
                if (peek() == L',') { ++p; continue; }
                if (peek() == L'}') { ++p; break; }
                ok = false; return v;
            }
            return v;
        }
        if (c == L'[') {
            ++p;
            v.type = JsonValue::Array;
            skipWs();
            if (peek() == L']') { ++p; return v; }
            while (p < end) {
                JsonValue child = parseValue(depth + 1);
                if (!ok) return v;
                v.arr.push_back(std::move(child));
                skipWs();
                if (peek() == L',') { ++p; continue; }
                if (peek() == L']') { ++p; break; }
                ok = false; return v;
            }
            return v;
        }
        if (c == L'"') {
            v.type = JsonValue::String;
            v.str = parseString();
            return v;
        }
        if (c == L't' && end - p >= 4 && wcsncmp(p, L"true", 4) == 0) { p += 4; v.type = JsonValue::Bool; v.b = true; return v; }
        if (c == L'f' && end - p >= 5 && wcsncmp(p, L"false", 5) == 0) { p += 5; v.type = JsonValue::Bool; v.b = false; return v; }
        if (c == L'n' && end - p >= 4 && wcsncmp(p, L"null", 4) == 0) { p += 4; v.type = JsonValue::Null; return v; }
        if (c == L'-' || (c >= L'0' && c <= L'9')) {
            wchar_t* endp = nullptr;
            double d = wcstod(p, &endp);
            if (endp == p) { ok = false; return v; }
            p = endp;
            v.type = JsonValue::Number;
            v.num = d;
            return v;
        }
        ok = false;
        return v;
    }
};

}  // namespace

const JsonValue* JsonValue::get(const wchar_t* key) const {
    if (type != Object) return nullptr;
    for (auto& kv : obj) if (kv.first == key) return &kv.second;
    return nullptr;
}

JsonValue ParseJson(const std::wstring& text, bool* okOut) {
    Parser ps;
    ps.p = text.c_str();
    ps.end = text.c_str() + text.size();
    JsonValue v = ps.parseValue(0);
    if (okOut) *okOut = ps.ok;
    return v;
}

COLORREF ParseColorHex(const std::wstring& s, COLORREF def) {
    if (s.size() != 7 || s[0] != L'#') return def;
    auto hx = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    int r = hx(s[1]) * 16 + hx(s[2]);
    int g = hx(s[3]) * 16 + hx(s[4]);
    int b = hx(s[5]) * 16 + hx(s[6]);
    if (r < 0 || g < 0 || b < 0) return def;
    return RGB(r, g, b);
}

// ---------------- defaults ----------------
Config DefaultConfig() {
    Config c;
    c.buttons = {
        { L"copy",   L"\u590d\u5236", L"\uE8C8", true },
        { L"cut",    L"\u526a\u5207", L"\uE8C6", true },
        { L"paste",  L"\u7c98\u8d34", L"\uE77F", true },
        { L"rename", L"\u91cd\u547d\u540d", L"\uE8AC", true },
        { L"delete", L"\u5220\u9664", L"\uE74D", true },
        { L"props",  L"\u5c5e\u6027", L"\uE713", true },
    };
    return c;
}

static std::wstring GetCfgStr(const JsonValue* obj, const wchar_t* key, const std::wstring& def) {
    if (!obj) return def;
    const JsonValue* v = obj->get(key);
    if (!v || v->type != JsonValue::String) return def;
    return v->str;
}
static int GetCfgInt(const JsonValue* obj, const wchar_t* key, int def) {
    if (!obj) return def;
    const JsonValue* v = obj->get(key);
    if (!v || v->type != JsonValue::Number) return def;
    return (int)v->num;
}
static bool GetCfgBool(const JsonValue* obj, const wchar_t* key, bool def) {
    if (!obj) return def;
    const JsonValue* v = obj->get(key);
    if (!v || v->type != JsonValue::Bool) return def;
    return v->b;
}

Config LoadConfig() {
    Config c = DefaultConfig();
    std::wstring path = GetConfigPath();
    if (!FileExistsW(path)) { SaveDefaultConfigIfMissing(); return c; }
    std::wstring txt = ReadFileTextW(path);
    bool ok = false;
    JsonValue root = ParseJson(txt, &ok);
    if (!ok || root.type != JsonValue::Object) {
        LogW(L"config parse failed, using defaults");
        return c;
    }
    const JsonValue* v = nullptr;
    c.version = GetCfgInt(&root, L"version", c.version);
    c.enabled = GetCfgBool(&root, L"enabled", c.enabled);
    c.logVerbose = GetCfgBool(&root, L"logVerbose", c.logVerbose);

    if ((v = root.get(L"colors"))) {
        c.colors.enabled = GetCfgBool(v, L"enabled", c.colors.enabled);
        c.colors.perPixel = GetCfgBool(v, L"perPixel", c.colors.perPixel);
        c.colors.darkText = GetCfgStr(v, L"darkText", c.colors.darkText);
        c.colors.lightText = GetCfgStr(v, L"lightText", c.colors.lightText);
        c.colors.threshold = GetCfgInt(v, L"threshold", c.colors.threshold);
        c.colors.softness = GetCfgInt(v, L"softness", c.colors.softness);
        c.colors.haloWidth = GetCfgInt(v, L"haloWidth", c.colors.haloWidth);
        c.colors.haloAlpha = GetCfgInt(v, L"haloAlpha", c.colors.haloAlpha);
        c.colors.extraDark = GetCfgStr(v, L"extraDark", c.colors.extraDark);
        c.colors.extraLight = GetCfgStr(v, L"extraLight", c.colors.extraLight);
    }
    if ((v = root.get(L"acrylic"))) {
        c.acrylic.enabled = GetCfgBool(v, L"enabled", c.acrylic.enabled);
        c.acrylic.blurRadius = GetCfgInt(v, L"blurRadius", c.acrylic.blurRadius);
        c.acrylic.opacity = GetCfgInt(v, L"opacity", c.acrylic.opacity);
        c.acrylic.tint = GetCfgStr(v, L"tint", c.acrylic.tint);
        c.acrylic.autoTint = GetCfgBool(v, L"autoTint", c.acrylic.autoTint);
        c.acrylic.lightTint = GetCfgStr(v, L"lightTint", c.acrylic.lightTint);
        c.acrylic.noise = GetCfgInt(v, L"noise", c.acrylic.noise);
        c.acrylic.corner = GetCfgInt(v, L"corner", c.acrylic.corner);
        c.acrylic.shadow = GetCfgInt(v, L"shadow", c.acrylic.shadow);
        c.acrylic.margin = GetCfgInt(v, L"margin", c.acrylic.margin);
    }
    if ((v = root.get(L"style"))) {
        c.style.fontFamily = GetCfgStr(v, L"fontFamily", c.style.fontFamily);
        c.style.fontFile = GetCfgStr(v, L"fontFile", c.style.fontFile);
        c.style.fontSize = GetCfgInt(v, L"fontSize", c.style.fontSize);
        c.style.itemHeight = GetCfgInt(v, L"itemHeight", c.style.itemHeight);
        c.style.iconSize = GetCfgInt(v, L"iconSize", c.style.iconSize);
        c.style.minWidth = GetCfgInt(v, L"minWidth", c.style.minWidth);
        c.style.maxWidth = GetCfgInt(v, L"maxWidth", c.style.maxWidth);
        c.style.maxHeight = GetCfgInt(v, L"maxHeight", c.style.maxHeight);
        c.style.showShortcut = GetCfgBool(v, L"showShortcut", c.style.showShortcut);
        c.style.showCheckmarks = GetCfgBool(v, L"showCheckmarks", c.style.showCheckmarks);
        c.style.hoverDelayMs = GetCfgInt(v, L"hoverDelayMs", c.style.hoverDelayMs);
    }
    if ((v = root.get(L"thirdParty"))) {
        c.thirdParty.enabled = GetCfgBool(v, L"enabled", c.thirdParty.enabled);
        c.thirdParty.title = GetCfgStr(v, L"title", c.thirdParty.title);
        c.thirdParty.submenu = GetCfgBool(v, L"submenu", c.thirdParty.submenu);
        c.thirdParty.submenuTitleIcon = GetCfgBool(v, L"submenuTitleIcon", c.thirdParty.submenuTitleIcon);
        const JsonValue* kt = v->get(L"keepTop");
        if (kt && kt->type == JsonValue::Array) {
            c.thirdParty.keepTop.clear();
            for (auto& e : kt->arr) if (e.type == JsonValue::String) c.thirdParty.keepTop.push_back(e.str);
        }
        const JsonValue* hd = v->get(L"hide");
        if (hd && hd->type == JsonValue::Array) {
            c.thirdParty.hide.clear();
            for (auto& e : hd->arr) if (e.type == JsonValue::String) c.thirdParty.hide.push_back(e.str);
        }
        c.thirdParty.mergePromoted = GetCfgBool(v, L"mergePromoted", c.thirdParty.mergePromoted);
        c.thirdParty.mergedTitle = GetCfgStr(v, L"mergedTitle", c.thirdParty.mergedTitle);
        const JsonValue* pg = v->get(L"promoteGroups");
        if (pg && pg->type == JsonValue::Array) {
            c.thirdParty.promoteGroups.clear();
            for (auto& e : pg->arr) {
                if (e.type != JsonValue::Object) continue;
                PromoteGroup g;
                const JsonValue* id = e.get(L"id");
                if (!id) continue;
                g.id = id->asStr(L"");
                if (g.id.empty()) continue;
                if (const JsonValue* nm = e.get(L"name")) g.name = nm->asStr(L"");
                if (const JsonValue* al = e.get(L"all")) g.all = al->asBool(false);
                c.thirdParty.promoteGroups.push_back(g);
            }
        }
        const JsonValue* pi = v->get(L"promoteItems");
        if (pi && pi->type == JsonValue::Array) {
            c.thirdParty.promoteItems.clear();
            for (auto& e : pi->arr) {
                if (e.type != JsonValue::Object) continue;
                PromoteItem it;
                const JsonValue* id = e.get(L"id");
                if (!id) continue;
                it.id = id->asStr(L"");
                if (const JsonValue* nm = e.get(L"item")) it.item = nm->asStr(L"");
                if (it.id.empty() || it.item.empty()) continue;
                c.thirdParty.promoteItems.push_back(it);
            }
        }
    }
    if ((v = root.get(L"buttons")) && v->type == JsonValue::Array) {
        std::vector<TopBarButton> btns;
        for (auto& e : v->arr) {
            if (e.type != JsonValue::Object) continue;
            TopBarButton b;
            b.cmd = GetCfgStr(&e, L"cmd", L"");
            b.label = GetCfgStr(&e, L"label", L"");
            b.icon = GetCfgStr(&e, L"icon", L"");
            b.enabled = GetCfgBool(&e, L"enabled", true);
            if (!b.cmd.empty()) btns.push_back(b);
        }
        // An explicitly empty array hides the top bar entirely.
        c.buttons = btns;
    }
    LogW(L"config loaded: buttons=%d blur=%d threshold=%d thirdParty=%d",
         (int)c.buttons.size(), c.acrylic.blurRadius, c.colors.threshold, (int)c.thirdParty.enabled);
    return c;
}

void SaveDefaultConfigIfMissing() {
    std::wstring path = GetConfigPath();
    if (FileExistsW(path)) return;
    std::wstring json =
        L"{\n"
        L"  \"version\": 1,\n"
        L"  \"enabled\": true,\n"
        L"  \"logVerbose\": false,\n"
        L"  \"colors\": {\n"
        L"    \"enabled\": true,\n"
        L"    \"perPixel\": true,\n"
        L"    \"darkText\": \"#000000\",\n"
        L"    \"lightText\": \"#FFFFFF\",\n"
        L"    \"threshold\": 140,\n"
        L"    \"softness\": 6,\n"
        L"    \"haloWidth\": 1,\n"
        L"    \"haloAlpha\": 40,\n"
        L"    \"extraDark\": \"\",\n"
        L"    \"extraLight\": \"\"\n"
        L"  },\n"
        L"  \"acrylic\": {\n"
        L"    \"enabled\": true,\n"
        L"    \"blurRadius\": 18,\n"
        L"    \"opacity\": 58,\n"
        L"    \"tint\": \"#202020\",\n"
        L"    \"autoTint\": true,\n"
        L"    \"lightTint\": \"#F3F3F5\",\n"
        L"    \"noise\": 5,\n"
        L"    \"corner\": 10,\n"
        L"    \"shadow\": 14,\n"
        L"    \"margin\": 10\n"
        L"  },\n"
        L"  \"style\": {\n"
        L"    \"fontFamily\": \"Microsoft YaHei UI\",\n"
        L"    \"fontFile\": \"\",\n"
        L"    \"fontSize\": 9,\n"
        L"    \"itemHeight\": 32,\n"
        L"    \"iconSize\": 16,\n"
        L"    \"minWidth\": 180,\n"
        L"    \"maxWidth\": 520,\n"
        L"    \"maxHeight\": 720,\n"
        L"    \"showShortcut\": true,\n"
        L"    \"showCheckmarks\": true,\n"
        L"    \"hoverDelayMs\": 320\n"
        L"  },\n"
        L"  \"thirdParty\": {\n"
        L"    \"enabled\": true,\n"
        L"    \"title\": \"\u66f4\u591a\u9009\u9879\",\n"
        L"    \"submenu\": true,\n"
        L"    \"submenuTitleIcon\": true,\n"
        L"    \"keepTop\": [],\n"
        L"    \"hide\": [],\n"
        L"    \"promoteGroups\": [],\n"
        L"    \"promoteItems\": [],\n"
        L"    \"mergePromoted\": false,\n"
        L"    \"mergedTitle\": \"\u5e38\u7528\u5de5\u5177\"\n"
        L"  },\n"
        L"  \"buttons\": [\n"
        L"    { \"cmd\": \"copy\",   \"label\": \"\u590d\u5236\",   \"icon\": \"\\uE8C8\", \"enabled\": true },\n"
        L"    { \"cmd\": \"cut\",    \"label\": \"\u526a\u5207\",   \"icon\": \"\\uE8C6\", \"enabled\": true },\n"
        L"    { \"cmd\": \"paste\",  \"label\": \"\u7c98\u8d34\",   \"icon\": \"\\uE77F\", \"enabled\": true },\n"
        L"    { \"cmd\": \"rename\", \"label\": \"\u91cd\u547d\u540d\", \"icon\": \"\\uE8AC\", \"enabled\": true },\n"
        L"    { \"cmd\": \"delete\", \"label\": \"\u5220\u9664\",   \"icon\": \"\\uE74D\", \"enabled\": true },\n"
        L"    { \"cmd\": \"props\",  \"label\": \"\u5c5e\u6027\",   \"icon\": \"\\uE713\", \"enabled\": true }\n"
        L"  ]\n"
        L"}\n";
    WriteFileTextW(path, json);
}
