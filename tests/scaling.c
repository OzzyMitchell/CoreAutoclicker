#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include "../clicker.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void) {
    HDC dc = CreateCompatibleDC(0);
    DWORD steady = 0;
    int scale, focus, x, y, partial = 0, cases = 0;
    g_bg = RGB(24,25,28); g_accent = RGB(147,176,207);
    CHECK(dc);
    /* Includes adjacent scales to catch stale sprite geometry when rounded sizes coincide. */
    for (scale = 72; scale <= 384; ++scale) {
        DIBSECTION dib;
        DWORD *pixels;
        HBITMAP cached;
        int size;
        g_scale = scale;
        CHECK(prepare_thumb(dc));
        cached = g_thumb_bitmap;
        CHECK(prepare_thumb(dc) && g_thumb_bitmap == cached);
        CHECK(GetObjectW(g_thumb_bitmap, sizeof(dib), &dib) == sizeof(dib));
        pixels = (DWORD *)dib.dsBm.bmBits; size = g_thumb_size;
        CHECK(dib.dsBm.bmWidth == size * 2 && dib.dsBm.bmHeight == size);
        for (focus = 0; focus < 2; ++focus) {
            int min_x = size, min_y = size, max_x = -1, max_y = -1;
            for (y = 0; y < size; ++y) for (x = 0; x < size; ++x) {
                DWORD pixel = pixels[y * size * 2 + focus * size + x];
                unsigned a = pixel >> 24;
                CHECK(pixel == pixels[y * size * 2 + focus * size + size - x - 1]);
                CHECK(pixel == pixels[(size - y - 1) * size * 2 + focus * size + x]);
                CHECK(pixel == pixels[x * size * 2 + focus * size + y]);
                CHECK((pixel & 255) <= a && ((pixel >> 8) & 255) <= a && ((pixel >> 16) & 255) <= a);
                if (a && a != 255) ++partial;
                if (a) { min_x = min(min_x,x); min_y = min(min_y,y); max_x = max(max_x,x); max_y = max(max_y,y); }
            }
            CHECK(max_x - min_x + 1 == 2 * px(focus ? 9 : 6));
            CHECK(max_y - min_y == max_x - min_x);
            ++cases;
        }
        if (!steady) steady = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        CHECK(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == steady);
    }
    CHECK(partial > 0);
    g_accent = RGB(0,255,255); g_thumb_size = 0;
    CHECK(prepare_thumb(dc));
    DeleteDC(g_thumb_dc); DeleteObject(g_thumb_bitmap); DeleteDC(dc);
    printf("PASS: %d circle renders, 75%%-400%% scale; symmetric bounds, antialiasing, premultiplied alpha, stable GDI object count.\n", cases);
    return 0;
}
