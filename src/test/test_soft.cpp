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

static const int W = 460, H = 72, SPLIT = 150, SOFT = 40;

struct Run {
    Bitmap32 hard, soft;
    int diff, minX, maxX;
};

static Run RunCase(bool ramp, HFONT font) {
    const int rampPx = 60;
    Bitmap32 base; base.Resize(W, H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int l;
            if (ramp) {
                float t = (float)(x - (SPLIT - rampPx)) / (float)(rampPx * 2);
                if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
                l = (int)(245.0f * (1.0f - t) + 16.0f * t);
            } else {
                l = (x < SPLIT) ? 245 : 16;
            }
            base.At(x, y) = 0xFF000000u | ((uint32_t)l << 16) | ((uint32_t)l << 8) | (uint32_t)l;
        }
    }
    std::wstring text = L"\u590d\u5236\u7c98\u8d34\u5207\u5260\u5220\u9664";  // 复制粘贴剪切删除
    Run r;
    r.hard = base;
    r.soft = base;
    DrawStringColored(r.hard, 14, 18, text, font, RGB(0, 0, 0), RGB(255, 255, 255), 140, 100, 0, 0, 0, 0);
    DrawStringColored(r.soft, 14, 18, text, font, RGB(0, 0, 0), RGB(255, 255, 255), 140, 100, 0, 0, 0, SOFT);
    r.diff = 0;
    r.minX = W;
    r.maxX = -1;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (r.hard.At(x, y) != r.soft.At(x, y)) {
                ++r.diff;
                if (x < r.minX) r.minX = x;
                if (x > r.maxX) r.maxX = x;
            }
        }
    }
    return r;
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDPIAware();
    HFONT font = CreateFontW(-26, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Microsoft YaHei UI");
    if (!font) { wprintf(L"FAIL: no font\n"); return 1; }

    Run grad = RunCase(true, font);
    Run edge = RunCase(false, font);

    wprintf(L"[gradient backdrop] diff=%d  x range %d..%d\n", grad.diff, grad.minX, grad.maxX);
    wprintf(L"[hard-edge backdrop] diff=%d  x range %d..%d\n", edge.diff, edge.minX, edge.maxX);

    // 硬边界也必须有过渡：这正是“背景是一条线”的现实情况（窗口边缘、截图切线）。
    bool edgeLocalized = edge.diff > 50 && edge.minX >= SPLIT - SOFT - 30 && edge.maxX <= SPLIT + SOFT + 30;
    bool edgeNotGlobal = edge.diff < W * H / 4;
    wprintf(L"edge localized=%d not-global=%d\n", (int)edgeLocalized, (int)edgeNotGlobal);

    SaveBmp(L"out\\soft_edge_hard.bmp", edge.hard);
    SaveBmp(L"out\\soft_edge_soft.bmp", edge.soft);

    bool pass = edgeLocalized && edgeNotGlobal && grad.diff > 20;
    wprintf(L"RESULT: %s\n", pass ? L"PASS" : L"FAIL");
    DeleteObject(font);
    return pass ? 0 : 1;
}
