#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

struct Bitmap32 {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    void Resize(int nw, int nh) {
        w = nw; h = nh;
        px.assign((size_t)w * h, 0u);
    }
    uint32_t* Row(int y) { return px.data() + (size_t)y * w; }
    const uint32_t* Row(int y) const { return px.data() + (size_t)y * w; }
    uint32_t& At(int x, int y) { return px[(size_t)y * w + x]; }
    uint32_t At(int x, int y) const { return px[(size_t)y * w + x]; }
    bool Has(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
};

uint32_t BlendArgb(uint32_t dst, uint32_t src, int a255);
int Luma255(uint32_t argb);
uint32_t ToArgb(COLORREF c);

bool CaptureScreenRect(int x, int y, int w, int h, Bitmap32& out);
void BuildAcrylicBackdrop(const Bitmap32& capture, int blurRadius, const std::wstring& tintHex,
                          const std::wstring& lightTintHex, int opacityPct, int noiseAmt,
                          bool autoTint, Bitmap32& out);
void CompositeMenuCover(const Bitmap32& capture, const Bitmap32& acrylic, int cx, int cy, int cw, int ch, int corner, int shadow, Bitmap32& out);
size_t MeasureTextWidth(HFONT font, const std::wstring& text);
SIZE MeasureTextSize(HFONT font, const std::wstring& text);
// softness > 0 cross-fades the two text colours across a band of +-softness pixels
// around the luminance split instead of switching abruptly in the middle of a stroke.
// haloWidth > 0 puts a thin outline of the *opposite* colour behind every glyph, which is
// what keeps text readable across a busy backdrop without making the switch look harsh.
void DrawStringColored(Bitmap32& dst, int x, int y, const std::wstring& text, HFONT font,
                       COLORREF dark, COLORREF light, int threshold, int alphaPct,
                       int shadowOffsetX, int shadowOffsetY, int shadowAlpha, int softness = 0,
                       int haloWidth = 0, int haloAlpha = 0);
void DrawGlyphColored(Bitmap32& dst, int x, int y, wchar_t glyph, HFONT iconFont,
                      COLORREF dark, COLORREF light, int threshold, int alphaPct, int softness = 0,
                      int haloWidth = 0, int haloAlpha = 0);
void FillRoundRect(Bitmap32& dst, int x, int y, int w, int h, int radius, uint32_t color, int alphaPct);
void DrawHLine(Bitmap32& dst, int x, int y, int w, uint32_t color, int alphaPct);
