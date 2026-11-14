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
// Same card, but on a transparent premultiplied layer: everything outside the rounded rect
// becomes see-through and the drop shadow is real translucent black. Feed the result to
// UpdateLayeredWindow so the window never shows a stale screenshot around the card - that is
// what made the animation leave a faint square behind.
void CompositeMenuLayer(const Bitmap32& acrylic, int cx, int cy, int cw, int ch, int corner, int shadow, Bitmap32& out);
void ResamplePremultiplied(const Bitmap32& src, int w, int h, Bitmap32& out);
size_t MeasureTextWidth(HFONT font, const std::wstring& text);
SIZE MeasureTextSize(HFONT font, const std::wstring& text);
// softness > 0 cross-fades the two text colours across roughly 2*softness pixels around the
// luminance split instead of switching abruptly in the middle of a stroke. Anything below 8
// is lifted to 8, because a shorter band looks like a smudge rather than a fade.
// haloWidth > 0 puts a thin outline of the *opposite* colour behind every glyph, which is
// what keeps text readable across a busy backdrop without making the switch look harsh.
// colorUnit picks how much of the run shares one colour:
//   0 = per pixel  - a single stroke may hold both text colours (default)
//   1 = per glyph  - every character gets one colour from its own backdrop box, so a
//                    word across the split turns black letter by letter and a
//                    menu line can be black on the left, white on the right
//   2 = per item   - the whole run uses one colour from the average backdrop
// Units 1 and 2 still honour softness for the hand-off between neighbours.
void DrawStringColored(Bitmap32& dst, int x, int y, const std::wstring& text, HFONT font,
                       COLORREF dark, COLORREF light, int threshold, int alphaPct,
                       int shadowOffsetX, int shadowOffsetY, int shadowAlpha, int softness = 0,
                       int haloWidth = 0, int haloAlpha = 0, int colorUnit = 0);
void DrawGlyphColored(Bitmap32& dst, int x, int y, wchar_t glyph, HFONT iconFont,
                      COLORREF dark, COLORREF light, int threshold, int alphaPct, int softness = 0,
                      int haloWidth = 0, int haloAlpha = 0, int colorUnit = 0);
void FillRoundRect(Bitmap32& dst, int x, int y, int w, int h, int radius, uint32_t color, int alphaPct);
void DrawHLine(Bitmap32& dst, int x, int y, int w, uint32_t color, int alphaPct);
