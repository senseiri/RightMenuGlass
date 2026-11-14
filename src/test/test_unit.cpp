#include "../dll/draw.h"
#include "../dll/util.h"
#include <cstdio>
#include <cstring>
#include <vector>

HINSTANCE g_hInst = NULL;

static void SaveBmp(const wchar_t* path, const Bitmap32& bm) {
    BITMAPFILEHEADER fh; BITMAPINFOHEADER ih;
    memset(&fh, 0, sizeof(fh)); memset(&ih, 0, sizeof(ih));
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + (DWORD)bm.px.size() * 4;
    ih.biSize = sizeof(ih); ih.biWidth = bm.w; ih.biHeight = -bm.h;
    ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB;
    ih.biSizeImage = (DWORD)bm.px.size() * 4;
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f);
    fwrite(bm.px.data(), 4, bm.px.size(), f); fclose(f);
}

static int PrefixWidth(HFONT font, const std::wstring& s, int n) {
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, s.c_str(), n, &sz);
    SelectObject(dc, old); DeleteDC(dc);
    return (int)sz.cx;
}

struct Stat { int darkOnly; int lightOnly; int mixed; int totalDark; int totalLight; };

// unit: 0 = per pixel, 1 = per glyph, 2 = per item
static Stat RunCase(const Bitmap32& base, HFONT font, const std::wstring& text,
                    const std::vector<int>& edges, int unit, int W, int H) {
    Bitmap32 after = base;
    DrawStringColored(after, 14, 18, text, font, RGB(0, 0, 0), RGB(255, 255, 255),
                      140, 100, 0, 0, 0, 0, 0, 0, unit);
    Stat st = {0, 0, 0, 0, 0};
    for (size_t i = 0; i < text.size(); ++i) {
        int x0 = edges[i], x1 = edges[i + 1];
        int d = 0, l = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (x < 0 || x >= W) continue;
                if (after.At(x, y) == base.At(x, y)) continue;
                int luma = Luma255(after.At(x, y));
                int bl = Luma255(base.At(x, y));
                if (luma < bl - 10) ++d; else if (luma > bl + 10) ++l;
            }
        }
        if (d > 5 && l > 5) ++st.mixed;
        else if (d > 5) ++st.darkOnly;
        else if (l > 5) ++st.lightOnly;
        st.totalDark += d; st.totalLight += l;
        wprintf(L"  char %zu [%d..%d] dark=%d light=%d\n", i, x0, x1, d, l);
    }
    wchar_t name[64];
    wcscpy_s(name, L"out\\unit_case.bmp");
    if (unit == 0) wcscpy_s(name, L"out\\unit_pixel.bmp");
    else if (unit == 1) wcscpy_s(name, L"out\\unit_glyph.bmp");
    else wcscpy_s(name, L"out\\unit_item.bmp");
    SaveBmp(name, after);
    return st;
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDPIAware();
    const int W = 460, H = 72;
    const int split = 150;
    Bitmap32 base; base.Resize(W, H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int l = x < split ? 245 : 16;
            base.At(x, y) = 0xFF000000u | ((uint32_t)l << 16) | ((uint32_t)l << 8) | (uint32_t)l;
        }
    }
    HFONT font = CreateFontW(-26, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Microsoft YaHei UI");
    if (!font) { wprintf(L"FAIL: no font\n"); return 1; }
    std::wstring text = L"\u590d\u5236\u7c98\u8d34\u5207\u5260\u5220\u9664";  // 复制粘贴剪切删除
    std::vector<int> edges; edges.push_back(14);
    for (size_t i = 1; i <= text.size(); ++i) edges.push_back(14 + PrefixWidth(font, text, (int)i));

    bool ok = true;

    wprintf(L"[unit=0 per pixel]\n");
    Stat s0 = RunCase(base, font, text, edges, 0, W, H);
    wprintf(L"  -> darkOnly=%d lightOnly=%d mixed=%d\n", s0.darkOnly, s0.lightOnly, s0.mixed);
    if (s0.mixed < 1) { wprintf(L"  FAIL: pixel mode should split at least one glyph\n"); ok = false; }
    else wprintf(L"  OK: at least one glyph carries both colours\n");

    wprintf(L"[unit=1 per glyph]\n");
    Stat s1 = RunCase(base, font, text, edges, 1, W, H);
    wprintf(L"  -> darkOnly=%d lightOnly=%d mixed=%d\n", s1.darkOnly, s1.lightOnly, s1.mixed);
    if (s1.mixed != 0) { wprintf(L"  FAIL: glyph mode must not mix colours inside one glyph\n"); ok = false; }
    if (s1.darkOnly < 1 || s1.lightOnly < 1) { wprintf(L"  FAIL: glyphs should differ from each other across the split\n"); ok = false; }
    if (s1.mixed == 0 && s1.darkOnly >= 1 && s1.lightOnly >= 1)
        wprintf(L"  OK: every glyph keeps one colour, neighbours may differ\n");

    wprintf(L"[unit=2 per item]\n");
    Stat s2 = RunCase(base, font, text, edges, 2, W, H);
    wprintf(L"  -> darkOnly=%d lightOnly=%d mixed=%d totalDark=%d totalLight=%d\n",
            s2.darkOnly, s2.lightOnly, s2.mixed, s2.totalDark, s2.totalLight);
    if (s2.mixed != 0 || (s2.totalDark > 0 && s2.totalLight > 0)) {
        wprintf(L"  FAIL: item mode must use exactly one colour for the whole run\n"); ok = false;
    } else wprintf(L"  OK: whole item shares one colour\n");

    wprintf(L"RESULT: %s\n", ok ? L"PASS" : L"FAIL");
    DeleteObject(font);
    return ok ? 0 : 1;
}
