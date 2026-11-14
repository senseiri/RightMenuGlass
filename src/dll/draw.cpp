#include "draw.h"
#include "util.h"
#include "config.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

uint32_t ToArgb(COLORREF c) {
    return 0xFF000000u | ((uint32_t)GetRValue(c) << 16) | ((uint32_t)GetGValue(c) << 8) | (uint32_t)GetBValue(c);
}

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

uint32_t BlendArgb(uint32_t dst, uint32_t src, int a255) {
    if (a255 <= 0) return dst;
    if (a255 >= 255) return src;
    int da = (int)((dst >> 24) & 0xFF);
    int dr = (int)((dst >> 16) & 0xFF), dg = (int)((dst >> 8) & 0xFF), db = (int)(dst & 0xFF);
    int sr = (int)((src >> 16) & 0xFF), sg = (int)((src >> 8) & 0xFF), sb = (int)(src & 0xFF);
    int r = (sr * a255 + dr * (255 - a255)) / 255;
    int g = (sg * a255 + dg * (255 - a255)) / 255;
    int b = (sb * a255 + db * (255 - a255)) / 255;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

int Luma255(uint32_t argb) {
    int r = (int)((argb >> 16) & 0xFF);
    int g = (int)((argb >> 8) & 0xFF);
    int b = (int)(argb & 0xFF);
    return (r * 299 + g * 587 + b * 114) / 1000;
}

bool CaptureScreenRect(int x, int y, int w, int h, Bitmap32& out) {
    if (w <= 0 || h <= 0) return false;
    out.Resize(w, h);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(NULL);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); ReleaseDC(NULL, screen); return false; }
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, bmp);
    BOOL ok = BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);
    GdiFlush();
    if (ok) memcpy(out.px.data(), bits, (size_t)w * h * 4);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(NULL, screen);
    return ok != FALSE;
}

// Simple separable box blur done on a downsampled copy; good enough for acrylic.
static void BoxBlur(Bitmap32& bmp, int radius) {
    if (radius <= 0 || bmp.w <= 0 || bmp.h <= 0) return;
    std::vector<uint32_t> tmp(bmp.px.size());
    int div = radius * 2 + 1;
    // horizontal
    for (int y = 0; y < bmp.h; ++y) {
        const uint32_t* row = bmp.Row(y);
        uint32_t* outRow = &tmp[(size_t)y * bmp.w];
        int r = 0, g = 0, b = 0, a = 0;
        int cnt = 0;
        for (int k = -radius; k <= radius; ++k) {
            int x = std::clamp(k, 0, bmp.w - 1);
            uint32_t p = row[x];
            r += (p >> 16) & 0xFF; g += (p >> 8) & 0xFF; b += p & 0xFF; a += (p >> 24) & 0xFF; ++cnt;
        }
        for (int x = 0; x < bmp.w; ++x) {
            outRow[x] = ((uint32_t)(a / cnt) << 24) | ((uint32_t)(r / cnt) << 16) | ((uint32_t)(g / cnt) << 8) | (uint32_t)(b / cnt);
            int addX = std::clamp(x + radius + 1, 0, bmp.w - 1);
            int subX = std::clamp(x - radius, 0, bmp.w - 1);
            uint32_t pa = row[addX], ps = row[subX];
            r += (int)((pa >> 16) & 0xFF) - (int)((ps >> 16) & 0xFF);
            g += (int)((pa >> 8) & 0xFF) - (int)((ps >> 8) & 0xFF);
            b += (int)(pa & 0xFF) - (int)(ps & 0xFF);
            a += (int)((pa >> 24) & 0xFF) - (int)((ps >> 24) & 0xFF);
        }
    }
    // vertical
    for (int x = 0; x < bmp.w; ++x) {
        int r = 0, g = 0, b = 0, a = 0, cnt = 0;
        for (int k = -radius; k <= radius; ++k) {
            int y = std::clamp(k, 0, bmp.h - 1);
            uint32_t p = tmp[(size_t)y * bmp.w + x];
            r += (p >> 16) & 0xFF; g += (p >> 8) & 0xFF; b += p & 0xFF; a += (p >> 24) & 0xFF; ++cnt;
        }
        for (int y = 0; y < bmp.h; ++y) {
            bmp.At(x, y) = ((uint32_t)(a / cnt) << 24) | ((uint32_t)(r / cnt) << 16) | ((uint32_t)(g / cnt) << 8) | (uint32_t)(b / cnt);
            int addY = std::clamp(y + radius + 1, 0, bmp.h - 1);
            int subY = std::clamp(y - radius, 0, bmp.h - 1);
            uint32_t pa = tmp[(size_t)addY * bmp.w + x], ps = tmp[(size_t)subY * bmp.w + x];
            r += (int)((pa >> 16) & 0xFF) - (int)((ps >> 16) & 0xFF);
            g += (int)((pa >> 8) & 0xFF) - (int)((ps >> 8) & 0xFF);
            b += (int)(pa & 0xFF) - (int)(ps & 0xFF);
            a += (int)((pa >> 24) & 0xFF) - (int)((ps >> 24) & 0xFF);
        }
    }
}

void BuildAcrylicBackdrop(const Bitmap32& capture, int blurRadius, const std::wstring& tintHex,
                          const std::wstring& lightTintHex, int opacityPct, int noiseAmt,
                          bool autoTint, Bitmap32& out) {
    out = capture;
    if (blurRadius <= 0) return;
    // downsample
    int down = 4;
    int sw = std::max(1, capture.w / down), sh = std::max(1, capture.h / down);
    Bitmap32 small;
    small.Resize(sw, sh);
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int dy = 0; dy < down; ++dy) {
                for (int dx = 0; dx < down; ++dx) {
                    int sx = x * down + dx, sy = y * down + dy;
                    if (sx >= capture.w || sy >= capture.h) continue;
                    uint32_t p = capture.At(sx, sy);
                    r += (p >> 16) & 0xFF; g += (p >> 8) & 0xFF; b += p & 0xFF; ++n;
                }
            }
            if (!n) n = 1;
            small.At(x, y) = 0xFF000000u | ((uint32_t)(r / n) << 16) | ((uint32_t)(g / n) << 8) | (uint32_t)(b / n);
        }
    }
    int r = std::max(1, blurRadius / down);
    BoxBlur(small, r);
    BoxBlur(small, r);
    BoxBlur(small, std::max(1, r / 2));
    // bilinear upsample
    for (int y = 0; y < capture.h; ++y) {
        for (int x = 0; x < capture.w; ++x) {
            float fx = (x + 0.5f) / down - 0.5f;
            float fy = (y + 0.5f) / down - 0.5f;
            int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
            float tx = fx - x0, ty = fy - y0;
            auto samp = [&](int sx, int sy) -> uint32_t {
                sx = std::clamp(sx, 0, small.w - 1);
                sy = std::clamp(sy, 0, small.h - 1);
                return small.At(sx, sy);
            };
            uint32_t p00 = samp(x0, y0), p10 = samp(x0 + 1, y0), p01 = samp(x0, y0 + 1), p11 = samp(x0 + 1, y0 + 1);
            auto lerp = [&](int a, int b, int c, int d) -> int {
                float v = (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
                return clamp255((int)(v + 0.5f));
            };
            int rr = lerp((p00 >> 16) & 0xFF, (p10 >> 16) & 0xFF, (p01 >> 16) & 0xFF, (p11 >> 16) & 0xFF);
            int gg = lerp((p00 >> 8) & 0xFF, (p10 >> 8) & 0xFF, (p01 >> 8) & 0xFF, (p11 >> 8) & 0xFF);
            int bb = lerp(p00 & 0xFF, p10 & 0xFF, p01 & 0xFF, p11 & 0xFF);
            out.At(x, y) = 0xFF000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
        }
    }
    // tint overlay (optionally adapted to the captured backdrop brightness)
    COLORREF tint = ParseColorHex(tintHex, RGB(32, 32, 32));
    int ta = std::clamp(opacityPct, 0, 100) * 255 / 100;
    if (autoTint && capture.w > 0 && capture.h > 0) {
        long long sum = 0;
        int cnt = 0;
        for (int y = 0; y < capture.h; y += 3) {
            for (int x = 0; x < capture.w; x += 3) {
                sum += Luma255(capture.At(x, y));
                ++cnt;
            }
        }
        int avg = cnt ? (int)(sum / cnt) : 0;
        // 0 = dark backdrop, 1 = light backdrop
        float w = std::clamp((float)(avg - 105) / 75.0f, 0.0f, 1.0f);
        COLORREF lt = ParseColorHex(lightTintHex, RGB(243, 243, 245));
        auto mix = [&](int a, int b) { return (int)(a + (b - a) * w); };
        tint = RGB(mix(GetRValue(tint), GetRValue(lt)),
                   mix(GetGValue(tint), GetGValue(lt)),
                   mix(GetBValue(tint), GetBValue(lt)));
        ta = (int)(ta * (0.92f + 0.12f * w));
        if (ta > 255) ta = 255;
        LogW(L"acrylic auto tint: avgLuma=%d w=%.2f tint=%02X%02X%02X alpha=%d",
             avg, w, GetRValue(tint), GetGValue(tint), GetBValue(tint), ta);
    }
    if (ta > 0) {
        for (auto& p : out.px) p = BlendArgb(p, ToArgb(tint), ta);
    }
    // noise
    if (noiseAmt > 0) {
        srand((unsigned)GetTickCount());
        for (auto& p : out.px) {
            int n = (rand() % (noiseAmt * 2 + 1)) - noiseAmt;
            int r = clamp255((int)((p >> 16) & 0xFF) + n);
            int g = clamp255((int)((p >> 8) & 0xFF) + n);
            int b = clamp255((int)(p & 0xFF) + n);
            p = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

// signed distance to rounded rect (negative inside)
static float SdRoundRect(float px, float py, float w, float h, float r) {
    float qx = fabsf(px - w * 0.5f) - (w * 0.5f - r);
    float qy = fabsf(py - h * 0.5f) - (h * 0.5f - r);
    float ax = std::max(qx, 0.0f), ay = std::max(qy, 0.0f);
    return sqrtf(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.0f) - r;
}

void CompositeMenuCover(const Bitmap32& capture, const Bitmap32& acrylic, int cx, int cy, int cw, int ch, int corner, int shadow, Bitmap32& out) {
    out = capture;
    int r = std::max(0, corner);
    int sh = std::max(1, shadow);
    for (int y = 0; y < out.h; ++y) {
        for (int x = 0; x < out.w; ++x) {
            float d = SdRoundRect((float)(x - cx) + 0.5f, (float)(y - cy) + 0.5f, (float)cw, (float)ch, (float)r);
            float inside = std::clamp(-d + 0.5f, 0.0f, 1.0f);
            uint32_t c = capture.At(x, y);
            if (inside >= 0.999f) {
                out.At(x, y) = acrylic.At(x, y);
            } else {
                float t = std::clamp((d - inside) / (float)sh, 0.0f, 1.0f);
                float dark = 1.0f - 0.55f * (1.0f - t) * (1.0f - t);
                int rr = clamp255((int)(((c >> 16) & 0xFF) * dark));
                int gg = clamp255((int)(((c >> 8) & 0xFF) * dark));
                int bb = clamp255((int)((c & 0xFF) * dark));
                uint32_t shadowed = 0xFF000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
                if (inside > 0.0f) shadowed = BlendArgb(shadowed, acrylic.At(x, y), (int)(inside * 255));
                out.At(x, y) = shadowed;
            }
        }
    }
}

void CompositeMenuLayer(const Bitmap32& acrylic, int cx, int cy, int cw, int ch, int corner, int shadow, Bitmap32& out) {
    int r = std::max(0, corner);
    int sh = std::max(1, shadow);
    out.Resize(acrylic.w, acrylic.h);
    for (int y = 0; y < out.h; ++y) {
        for (int x = 0; x < out.w; ++x) {
            float d = SdRoundRect((float)(x - cx) + 0.5f, (float)(y - cy) + 0.5f, (float)cw, (float)ch, (float)r);
            float inside = std::clamp(-d + 0.5f, 0.0f, 1.0f);
            uint32_t c = acrylic.At(x, y);
            if (inside >= 0.999f) {
                out.At(x, y) = c | 0xFF000000u;
                continue;
            }
            if (inside > 0.0f) {
                // Edge pixel: premultiply the card colour by its coverage so the rounded corner
                // fades into the desktop instead of showing a hard stair step.
                int a = (int)(inside * 255.0f + 0.5f);
                int rr = (int)((c >> 16) & 0xFF) * a / 255;
                int gg = (int)((c >> 8) & 0xFF) * a / 255;
                int bb = (int)(c & 0xFF) * a / 255;
                out.At(x, y) = ((uint32_t)a << 24) | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
                continue;
            }
            float t = std::clamp(d / (float)sh, 0.0f, 1.0f);
            float k = 0.55f * (1.0f - t) * (1.0f - t);
            int a = (int)(k * 255.0f + 0.5f);
            if (a > 255) a = 255;
            if (a < 0) a = 0;
            out.At(x, y) = ((uint32_t)a << 24);   // premultiplied black
        }
    }
}

void ResamplePremultiplied(const Bitmap32& src, int w, int h, Bitmap32& out) {
    if (w <= 0 || h <= 0 || src.w <= 0 || src.h <= 0) {
        out.w = 0; out.h = 0; out.px.clear();
        return;
    }
    out.Resize(w, h);
    float rx = (float)src.w / (float)w;
    float ry = (float)src.h / (float)h;
    for (int y = 0; y < h; ++y) {
        float fy = (y + 0.5f) * ry - 0.5f;
        int y0 = (int)floorf(fy);
        float ty = fy - (float)y0;
        int y1 = y0 + 1;
        if (y0 < 0) y0 = 0;
        if (y0 > src.h - 1) y0 = src.h - 1;
        if (y1 < 0) y1 = 0;
        if (y1 > src.h - 1) y1 = src.h - 1;
        const uint32_t* r0 = src.Row(y0);
        const uint32_t* r1 = src.Row(y1);
        uint32_t* dstRow = out.Row(y);
        for (int x = 0; x < w; ++x) {
            float fx = (x + 0.5f) * rx - 0.5f;
            int x0 = (int)floorf(fx);
            float tx = fx - (float)x0;
            int x1 = x0 + 1;
            if (x0 < 0) x0 = 0;
            if (x0 > src.w - 1) x0 = src.w - 1;
            if (x1 < 0) x1 = 0;
            if (x1 > src.w - 1) x1 = src.w - 1;
            uint32_t c00 = r0[x0], c10 = r0[x1], c01 = r1[x0], c11 = r1[x1];
            uint32_t o = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                float v0 = (float)((c00 >> sh) & 0xFF) * (1.0f - tx) + (float)((c10 >> sh) & 0xFF) * tx;
                float v1 = (float)((c01 >> sh) & 0xFF) * (1.0f - tx) + (float)((c11 >> sh) & 0xFF) * tx;
                int v = (int)(v0 * (1.0f - ty) + v1 * ty + 0.5f);
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                o |= (uint32_t)v << sh;
            }
            dstRow[x] = o;
        }
    }
}

size_t MeasureTextWidth(HFONT font, const std::wstring& text) {
    SIZE s = MeasureTextSize(font, text);
    return (size_t)s.cx;
}

SIZE MeasureTextSize(HFONT font, const std::wstring& text) {
    SIZE sz = {0, 0};
    if (text.empty()) return sz;
    HDC dc = GetDC(NULL);
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, font);
    if (text.find(L'\n') != std::wstring::npos) {
        RECT rc = {0, 0, 4096, 4096};
        DrawTextW(mem, text.c_str(), (int)text.size(), &rc, DT_CALCRECT | DT_NOPREFIX);
        sz.cx = rc.right; sz.cy = rc.bottom;
    } else {
        GetTextExtentPoint32W(mem, text.c_str(), (int)text.size(), &sz);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, dc);
    return sz;
}

void DrawStringColored(Bitmap32& dst, int x, int y, const std::wstring& text, HFONT font,
                       COLORREF dark, COLORREF light, int threshold, int alphaPct,
                       int shadowOffsetX, int shadowOffsetY, int shadowAlpha, int softness,
                       int haloWidth, int haloAlpha, int colorUnit) {
    if (text.empty()) return;
    SIZE sz = MeasureTextSize(font, text);
    if (sz.cx <= 0 || sz.cy <= 0) return;
    int pad = 2;
    int mw = sz.cx + pad * 2, mh = sz.cy + pad * 2;
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = mw;
    bi.bmiHeader.biHeight = -mh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = GetDC(NULL);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); ReleaseDC(NULL, dc); return; }
    memset(bits, 0, (size_t)mw * mh * 4);
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    HGDIOBJ oldFont = SelectObject(mem, font);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));
    UINT fmt = DT_NOPREFIX | DT_SINGLELINE;
    RECT rc = {pad, pad, pad + sz.cx, pad + sz.cy};
    DrawTextW(mem, text.c_str(), (int)text.size(), &rc, fmt);
    GdiFlush();

    const uint32_t* src = (const uint32_t*)bits;
    int alphaScale = alphaPct < 0 ? 0 : (alphaPct > 100 ? 100 : alphaPct);

    // Soft colour hand-off needs a *local average* background luminance, not the single
    // pixel under the glyph: on a hard edge (an unblurred window border, a screenshot cut)
    // every pixel is either fully light or fully dark, so a per-pixel threshold would still
    // snap from black to white in one column. Averaging over a (2r+1)^2 box lets the two
    // text colours slide into each other across the boundary. Summed-area table keeps it O(1)
    // per glyph pixel.
    std::vector<int> lumSat;
    // softness is the full width of the cross-fade band in pixels, and the averaging box uses
    // the same radius. A half-width box (the first take) tracked the backdrop too closely: on
    // a photo wallpaper the averaged luminance still swung across the threshold inside a single
    // stroke, so the "fade" collapsed into a two-pixel smudge instead of sliding.
    if (colorUnit == 2) softness = 0;   // one colour for the whole run: nothing to cross-fade to
    if (softness > 0 && softness < 8) softness = 8;   // see below: a tiny band looks dirty
    int radius = softness > 0 ? (softness > 120 ? 120 : softness) : 0;
    int boxX = 0, boxY = 0, boxW = 0, boxH = 0;
    if (radius > 0) {
        boxX = std::max(0, x - pad - radius);
        boxY = std::max(0, y - pad - radius);
        int bx1 = std::min(dst.w, x - pad + mw + radius);
        int by1 = std::min(dst.h, y - pad + mh + radius);
        boxW = bx1 - boxX;
        boxH = by1 - boxY;
        if (boxW > 0 && boxH > 0) {
            lumSat.assign((size_t)(boxW + 1) * (boxH + 1), 0);
            for (int yy = 0; yy < boxH; ++yy) {
                int rowSum = 0;
                int* prevRow = &lumSat[(size_t)yy * (boxW + 1)];
                int* curRow = &lumSat[(size_t)(yy + 1) * (boxW + 1)];
                for (int xx = 0; xx < boxW; ++xx) {
                    rowSum += Luma255(dst.At(boxX + xx, boxY + yy));
                    curRow[xx + 1] = rowSum + prevRow[xx + 1];
                }
            }
        } else {
            radius = 0;
        }
    }

    // colorUnit == 2: sample the backdrop once for the whole run and reuse that value for
    // every glyph pixel, so a word that straddles the split stays one colour.
    int globalLum = -1;
    if (colorUnit == 2) {
        long long sum = 0;
        int n = 0;
        int bx0 = std::max(0, x - pad), by0 = std::max(0, y - pad);
        int bx1 = std::min(dst.w, x - pad + mw), by1 = std::min(dst.h, y - pad + mh);
        for (int py = by0; py < by1; ++py) {
            for (int px = bx0; px < bx1; ++px) { sum += Luma255(dst.At(px, py)); ++n; }
        }
        if (n > 0) globalLum = (int)(sum / n);
    }

    // colorUnit == 1: one colour per character. The backdrop luminance is averaged over each
    // character's own box, so a line can be black on its left half and white on its right
    // while no single stroke is ever split between the two colours.
    std::vector<int> glyphEdge;
    std::vector<int> glyphLum;
    if (colorUnit == 1) {
        glyphEdge.push_back(0);
        for (size_t i = 0; i < text.size(); ++i) {
            glyphEdge.push_back((int)MeasureTextWidth(font, text.substr(0, i + 1)));
        }
        int gy0 = std::max(0, y);
        int gy1 = std::min(dst.h, y + (int)sz.cy);
        for (size_t i = 0; i < text.size(); ++i) {
            int gx0 = std::max(0, x + glyphEdge[i]);
            int gx1 = std::min(dst.w, x + glyphEdge[i + 1]);
            long long sum = 0;
            int n = 0;
            for (int syp = gy0; syp < gy1; syp += 2) {
                for (int sxp = gx0; sxp < gx1; sxp += 2) { sum += Luma255(dst.At(sxp, syp)); ++n; }
            }
            glyphLum.push_back(n > 0 ? (int)(sum / n) : -1);
        }
    }
    auto glyphLumAt = [&](int dxPos) -> int {
        if (glyphLum.empty()) return -1;
        int rel = dxPos - x;
        for (size_t i = 0; i < glyphLum.size(); ++i) {
            if (rel >= glyphEdge[i] && rel < glyphEdge[i + 1]) return glyphLum[i];
        }
        return glyphLum.back();
    };

    // Average luminance of the (2*radius+1)^2 box around one pixel, via the summed-area table.
    auto avgLum = [&](int px, int py) -> int {
        int bx0 = std::max(boxX, px - radius);
        int by0 = std::max(boxY, py - radius);
        int bx1 = std::min(boxX + boxW, px + radius + 1);
        int by1 = std::min(boxY + boxH, py + radius + 1);
        int ix0 = bx0 - boxX, iy0 = by0 - boxY;
        int ix1 = bx1 - boxX, iy1 = by1 - boxY;
        int n = (ix1 - ix0) * (iy1 - iy0);
        if (n <= 0) return -1;
        const int* r0 = &lumSat[(size_t)iy0 * (boxW + 1)];
        const int* r1 = &lumSat[(size_t)iy1 * (boxW + 1)];
        return (r1[ix1] - r0[ix1] - r1[ix0] + r0[ix0]) / n;
    };

    // Where the local average actually crosses the threshold decides whether a fade is
    // meaningful at all, and its two ends give the exact luminance the two colours must be
    // reached at. Normalising each side against them makes the extreme pixels fully black or
    // fully white (readability first) while still spreading the hand-off over the whole band.
    // A flat backdrop, or one that never crosses the threshold, keeps the crisp switch.
    bool fade = false;
    int spanUp = 0, spanDown = 0;
    if (radius > 0 && boxW > 0 && boxH > 0) {
        int lumMin = 255, lumMax = 0;
        int stepX = boxW / 24; if (stepX < 1) stepX = 1;
        int stepY = boxH / 24; if (stepY < 1) stepY = 1;
        for (int sy = 0; sy < boxH; sy += stepY) {
            for (int sx = 0; sx < boxW; sx += stepX) {
                int a = avgLum(boxX + sx, boxY + sy);
                if (a < 0) continue;
                if (a < lumMin) lumMin = a;
                if (a > lumMax) lumMax = a;
            }
        }
        if (lumMin < threshold && lumMax > threshold && (lumMax - lumMin) >= 40) {
            fade = true;
            spanUp = lumMax - threshold;
            spanDown = threshold - lumMin;
            // A backdrop that barely pokes past the split (lumMax = threshold + 4) used to
            // finish the whole hand-off in four pixels - the short, dirty fade that reads as
            // noise inside a glyph. Keep every hand-off at least one averaging box wide.
            int minSpan = radius * 2;
            if (minSpan < 24) minSpan = 24;
            if (spanUp < minSpan) spanUp = minSpan;
            if (spanDown < minSpan) spanDown = minSpan;
        }
    }

    // optional drop shadow first
    if (shadowAlpha > 0) {
        for (int py = 0; py < mh; ++py) {
            for (int px = 0; px < mw; ++px) {
                uint32_t p = src[(size_t)py * mw + px];
                int a = (((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF)) / 3;
                if (a == 0) continue;
                int dx = x + px - pad + shadowOffsetX;
                int dy = y + py - pad + shadowOffsetY;
                if (!dst.Has(dx, dy)) continue;
                dst.At(dx, dy) = BlendArgb(dst.At(dx, dy), 0xFF000000u, a * shadowAlpha / 255);
            }
        }
    }

    // Outline first: every pixel just outside a glyph gets a wash of the opposite extremecolour,
    // so a white glyph on a mid-tone backdrop keeps a dark rim (and vice versa) instead of
    // dissolving into it. The rim is what buys readability on a busy blurred wallpaper; the
    // cross-fade above only has to keep the two colours from snapping, not carry legibility.
    int hw = haloWidth;
    if (hw > 0 && haloAlpha > 0) {
        if (hw > 3) hw = 3;
        if (pad < hw + 1) hw = pad > 0 ? pad : 0;
        auto maskAt = [&](int px, int py) -> int {
            if (px < 0 || py < 0 || px >= mw || py >= mh) return 0;
            uint32_t q = src[(size_t)py * mw + px];
            return (int)((((q >> 16) & 0xFF) + ((q >> 8) & 0xFF) + (q & 0xFF)) / 3);
        };
        for (int py = 0; py < mh; ++py) {
            for (int px = 0; px < mw; ++px) {
                if (maskAt(px, py) > 0) continue;   // the glyph itself is painted later
                int best = 0;
                for (int oy = -hw; oy <= hw; ++oy) {
                    for (int ox = -hw; ox <= hw; ++ox) {
                        int m = maskAt(px + ox, py + oy);
                        if (m > best) best = m;
                    }
                }
                if (best == 0) continue;
                int dx = x + px - pad;
                int dy = y + py - pad;
                if (!dst.Has(dx, dy)) continue;
                uint32_t bg = dst.At(dx, dy);
                int hl = Luma255(bg);
                if (colorUnit == 2) {
                    if (globalLum >= 0) hl = globalLum;
                } else if (colorUnit == 1) {
                    int gl = glyphLumAt(dx);
                    if (gl >= 0) hl = gl;
                } else if (radius > 0) {
                    int avg = avgLum(dx, dy);
                    if (avg >= 0) hl = avg;
                }
                COLORREF haloCol = (hl < threshold) ? dark : light;
                // The rim is only there to rescue low contrast. On plain white paper a
                // white rim around black text has nothing to rescue - it just washes over
                // the anti-aliased edge and the glyph reads as blurry. Fade the rim out
                // as the backdrop moves away from the split point.
                int dist = (hl >= threshold) ? (hl - threshold) : (threshold - hl);
                int need = 0;
                if (dist < 110) {
                    need = (110 - dist) * 255 / 70;
                    if (need > 255) need = 255;
                }
                if (need <= 0) continue;
                int ha = best * haloAlpha / 100;
                ha = ha * alphaScale / 100;
                ha = ha * need / 255;
                if (ha <= 0) continue;
                dst.At(dx, dy) = BlendArgb(bg, ToArgb(haloCol), ha);
            }
        }
    }

    for (int py = 0; py < mh; ++py) {
        for (int px = 0; px < mw; ++px) {
            uint32_t p = src[(size_t)py * mw + px];
            int a = (((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF)) / 3;
            if (a == 0) continue;
            int dx = x + px - pad;
            int dy = y + py - pad;
            if (!dst.Has(dx, dy)) continue;
            uint32_t bg = dst.At(dx, dy);
            int lum = Luma255(bg);
            if (colorUnit == 2) {
                if (globalLum >= 0) lum = globalLum;
            } else if (colorUnit == 1) {
                int gl = glyphLumAt(dx);
                if (gl >= 0) lum = gl;
            } else if (radius > 0) {
                int avg = avgLum(dx, dy);
                if (avg >= 0) lum = avg;
            }
            int aa = a * alphaScale / 100;
            if (!fade) {
                COLORREF useCol = (lum < threshold) ? light : dark;
                dst.At(dx, dy) = BlendArgb(bg, ToArgb(useCol), aa);
            } else {
                // Cross-fade instead of a hard switch: a stroke that runs from a dark
                // backdrop into a light one slides through the blended colour rather than
                // snapping to the other extreme on a single pixel.
                float t;
                if (lum >= threshold) t = 0.5f + 0.5f * (float)(lum - threshold) / (float)spanUp;
                else                  t = 0.5f - 0.5f * (float)(threshold - lum) / (float)spanDown;
                if (t < 0.0f) t = 0.0f;
                else if (t > 1.0f) t = 1.0f;
                t = t * t * (3.0f - 2.0f * t);  // smoothstep
                uint32_t useCol = BlendArgb(ToArgb(light), ToArgb(dark), (int)(t * 255.0f));
                dst.At(dx, dy) = BlendArgb(bg, useCol, aa);
            }
        }
    }
    SelectObject(mem, oldFont);
    SelectObject(mem, oldBmp);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(NULL, dc);
}

void DrawGlyphColored(Bitmap32& dst, int x, int y, wchar_t glyph, HFONT iconFont,
                      COLORREF dark, COLORREF light, int threshold, int alphaPct, int softness,
                      int haloWidth, int haloAlpha, int colorUnit) {
    std::wstring s(1, glyph);
    DrawStringColored(dst, x, y, s, iconFont, dark, light, threshold, alphaPct, 0, 0, 0, softness,
                      haloWidth, haloAlpha, colorUnit);
}

void FillRoundRect(Bitmap32& dst, int x, int y, int w, int h, int radius, uint32_t color, int alphaPct) {
    if (w <= 0 || h <= 0) return;
    int rx = x, ry = y, rw = w, rh = h;
    int r = std::min(std::min(radius, w / 2), h / 2);
    int a = std::clamp(alphaPct, 0, 100) * 255 / 100;
    for (int py = ry; py < ry + rh; ++py) {
        for (int px = rx; px < rx + rw; ++px) {
            if (!dst.Has(px, py)) continue;
            float d = SdRoundRect((float)(px - rx) + 0.5f, (float)(py - ry) + 0.5f, (float)rw, (float)rh, (float)r);
            int cov = (int)(std::clamp(-d + 0.5f, 0.0f, 1.0f) * 255.0f);
            if (cov == 0) continue;
            dst.At(px, py) = BlendArgb(dst.At(px, py), color, a * cov / 255);
        }
    }
}

void DrawHLine(Bitmap32& dst, int x, int y, int w, uint32_t color, int alphaPct) {
    if (y < 0 || y >= dst.h) return;
    int a = std::clamp(alphaPct, 0, 100) * 255 / 100;
    for (int i = x; i < x + w && i < dst.w; ++i) {
        if (i < 0) continue;
        dst.At(i, y) = BlendArgb(dst.At(i, y), color, a);
    }
}
