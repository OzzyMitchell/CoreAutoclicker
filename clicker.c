#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <CommCtrl.h>
#include <windowsx.h>
#include <dwmapi.h>

#define MAX_CPS 1000
#define RATE_MASK 1023
#define BUTTON_MASK (3 << 10)
#define LEFT (1 << 10)
#define RIGHT (2 << 10)
#define JITTER (1 << 12)
#define SEND_INPUT (1 << 13)
#define QUIT (1 << 14)
#define ENGINE_ERROR (WM_APP + 1)
#define HK_LEFT 1
#define HK_RIGHT 2
#define HK_EXIT 3
#define CLIENT_W 420
#define CLIENT_H 298

enum { LEFT_LABEL, LEFT_KEY, RIGHT_LABEL, RIGHT_KEY, RATE_LABEL, RATE, SLIDER,
       MODE_BOX, JITTER_BOX, APPLY, STATUS, FOOTER, CONTROL_COUNT };
static HWND g_window, g_focus, g_control[CONTROL_COUNT];
static HANDLE g_wake, g_timer, g_font_resource;
static volatile LONG g_config = MAX_CPS | SEND_INPUT;
static HFONT g_font;
static HBRUSH g_background, g_field_brush;
static HDC g_thumb_dc;
static HBITMAP g_thumb_bitmap;
static COLORREF g_bg, g_field, g_text, g_muted, g_edge, g_accent;
static UINT g_dpi = 96;
static int g_scale = 96, g_font_height, g_thumb_size, g_thumb_scale, g_sync, g_drag, g_capturing;
static WORD g_keys[2] = { VK_F6, VK_F7 }, g_pending[2] = { VK_F6, VK_F7 };
static RECT g_fields[3];
static const int field_ids[3] = { RATE, LEFT_KEY, RIGHT_KEY };
static UINT (WINAPI *window_dpi)(HWND);
static BOOL (WINAPI *adjust_dpi)(LPRECT, DWORD, BOOL, DWORD, UINT);

static LONG config(void) { return InterlockedCompareExchange(&g_config, 0, 0); }
static int px(int n) { return MulDiv(n, g_scale, 96); }
static int clamp(int n, int low, int high) { return n < low ? low : n > high ? high : n; }

static void change_config(LONG mask, LONG value) {
    LONG old, next;
    do { old = config(); next = (old & ~mask) | value; }
    while (InterlockedCompareExchange(&g_config, next, old) != old);
    if (next != old) SetEvent(g_wake);
}

static DWORD random32(DWORD *state) {
    DWORD x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}

static BOOL post_click(BOOL left) {
    POINT p;
    HWND target;
    if (!GetCursorPos(&p) || !(target = WindowFromPoint(p)) || !ScreenToClient(target, &p)) return FALSE;
    return PostMessageW(target, left ? WM_LBUTTONDOWN : WM_RBUTTONDOWN,
                        left ? MK_LBUTTON : MK_RBUTTON, MAKELPARAM(p.x, p.y)) &&
           PostMessageW(target, left ? WM_LBUTTONUP : WM_RBUTTONUP, 0, MAKELPARAM(p.x, p.y));
}

/* One worker, one cancellable timer. No polling, spin tail, catch-up burst or UI timer. */
static DWORD WINAPI click_thread(void *unused) {
    LARGE_INTEGER frequency, now, due;
    LONGLONG next = 0, base = 0, remainder = 0, carry = 0;
    HANDLE waits[2] = { g_wake, g_timer }; /* Settings/stop win if both are signaled. */
    INPUT batch[3] = {0};
    LONG previous = 0;
    DWORD rng = GetTickCount() ^ GetCurrentThreadId() ^ 0x9e3779b9u;
    (void)unused;
    if (!rng) rng = 1;
    QueryPerformanceFrequency(&frequency);
    batch[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    for (;;) {
        LONG settings = config(), rate = settings & RATE_MASK;
        BOOL ok, left = (settings & BUTTON_MASK) == LEFT;
        if (settings & QUIT) break;
        if (!(settings & BUTTON_MASK)) {
            CancelWaitableTimer(g_timer);
            previous = 0;
            WaitForSingleObject(g_wake, INFINITE);
            continue;
        }
        QueryPerformanceCounter(&now);
        if (settings != previous) {
            base = frequency.QuadPart / rate;
            remainder = frequency.QuadPart % rate;
            carry = 0;
            next = now.QuadPart + (previous ? base : 0);
            previous = settings;
        }
        if (next > now.QuadPart) {
            due.QuadPart = -((next - now.QuadPart) * 10000000 + frequency.QuadPart - 1) / frequency.QuadPart;
            if (!SetWaitableTimer(g_timer, &due, 0, 0, 0, FALSE)) {
                PostMessageW(g_window, ENGINE_ERROR, 1, 0);
                return 1;
            }
            {
                DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (result == WAIT_OBJECT_0) continue;
                if (result != WAIT_OBJECT_0 + 1) {
                    PostMessageW(g_window, ENGINE_ERROR, 1, 0);
                    return 1;
                }
            }
        }
        if (config() != settings) continue;
        batch[1].mi.dwFlags = left ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_RIGHTDOWN;
        batch[2].mi.dwFlags = left ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_RIGHTUP;
        if (settings & JITTER) {
            batch[0].mi.dx = (LONG)(((ULONGLONG)random32(&rng) * 3) >> 32) - 1;
            batch[0].mi.dy = (LONG)(((ULONGLONG)random32(&rng) * 3) >> 32) - 1;
        }
        if (settings & SEND_INPUT) {
            UINT count = (settings & JITTER) ? 3u : 2u;
            ok = SendInput(count, batch + (3 - count), sizeof(INPUT)) == count;
        } else {
            ok = !(settings & JITTER) || SendInput(1, batch, sizeof(INPUT)) == 1;
            if (ok) ok = post_click(left);
        }
        if (!ok) {
            /* Do not overwrite settings that the UI changed during input delivery. */
            InterlockedCompareExchange(&g_config, settings & ~BUTTON_MASK, settings);
            PostMessageW(g_window, ENGINE_ERROR, 0, 0);
        }
        next += base;
        carry += remainder;
        if (carry >= rate) { ++next; carry -= rate; }
        QueryPerformanceCounter(&now);
        if (next <= now.QuadPart) { next = now.QuadPart + base; carry = 0; }
    }
    CancelWaitableTimer(g_timer);
    return 0;
}

static void status(LPCWSTR message) {
    LONG button = config() & BUTTON_MASK;
    SetWindowTextW(g_control[STATUS], button == LEFT ? L"Left on" : button == RIGHT ? L"Right on" : L"Paused");
    if (message) SetWindowTextW(g_control[FOOTER], message);
}

static void set_rate(int rate, BOOL edit) {
    rate = clamp(rate, 1, MAX_CPS);
    change_config(RATE_MASK, rate);
    g_sync = 1;
    if (SendMessageW(g_control[SLIDER], TBM_GETPOS, 0, 0) != rate)
        SendMessageW(g_control[SLIDER], TBM_SETPOS, FALSE, rate);
    InvalidateRect(g_control[SLIDER], 0, FALSE);
    if (edit) SetDlgItemInt(g_window, 100 + RATE, (UINT)rate, FALSE);
    g_sync = 0;
}

/* Invalid/empty intermediate edits leave the running rate alone. Commit clamps. */
static void read_rate(BOOL commit) {
    WCHAR text[16];
    int i, value = 0, length = GetWindowTextW(g_control[RATE], text, 16);
    for (i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') break;
        value = clamp(value * 10 + text[i] - L'0', 0, MAX_CPS + 1);
    }
    if (length && i == length && (commit || (value >= 1 && value <= MAX_CPS))) set_rate(value, commit);
    else if (commit) set_rate(config() & RATE_MASK, TRUE);
}

static UINT modifiers(WORD key) {
    BYTE flags = HIBYTE(key);
    return MOD_NOREPEAT | ((flags & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) |
           ((flags & HOTKEYF_SHIFT) ? MOD_SHIFT : 0) | ((flags & HOTKEYF_ALT) ? MOD_ALT : 0);
}

static BOOL bind_keys(WORD left, WORD right) {
    UnregisterHotKey(g_window, HK_LEFT);
    UnregisterHotKey(g_window, HK_RIGHT);
    if (RegisterHotKey(g_window, HK_LEFT, modifiers(left), LOBYTE(left)) &&
        RegisterHotKey(g_window, HK_RIGHT, modifiers(right), LOBYTE(right))) return TRUE;
    UnregisterHotKey(g_window, HK_LEFT);
    UnregisterHotKey(g_window, HK_RIGHT);
    return FALSE;
}

static void key_text(int index) {
    WCHAR text[96], name[64];
    WORD key = g_pending[index];
    LONG scan = (LONG)(MapVirtualKeyW(LOBYTE(key), MAPVK_VK_TO_VSC) << 16);
    text[0] = 0;
    if (HIBYTE(key) & HOTKEYF_CONTROL) lstrcatW(text, L"Ctrl + ");
    if (HIBYTE(key) & HOTKEYF_SHIFT) lstrcatW(text, L"Shift + ");
    if (HIBYTE(key) & HOTKEYF_ALT) lstrcatW(text, L"Alt + ");
    if (HIBYTE(key) & HOTKEYF_EXT) scan |= 1 << 24;
    if (!GetKeyNameTextW(scan, name, 64)) lstrcpyW(name, L"?");
    lstrcatW(text, name);
    SetWindowTextW(g_control[index ? RIGHT_KEY : LEFT_KEY], text);
}

static void apply_keys(void) {
    WORD left = g_pending[0], right = g_pending[1];
    change_config(BUTTON_MASK, 0);
    if ((LOBYTE(left) == LOBYTE(right) && modifiers(left) == modifiers(right)) ||
        LOBYTE(left) == VK_F9 || LOBYTE(right) == VK_F9) {
        status(L"Use different hotkeys. F9 is reserved.");
    } else if (bind_keys(left, right)) {
        g_keys[0] = left; g_keys[1] = right;
        status(L"Hotkeys applied.");
    } else {
        BOOL restored = bind_keys(g_keys[0], g_keys[1]);
        status(restored ? L"Hotkey in use. Kept previous keys." : L"Hotkeys in use. Choose others.");
    }
}

static LRESULT CALLBACK field_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    int index = id == RIGHT_KEY;
    (void)data;
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        if (msg == WM_SETFOCUS) g_focus = hwnd;
        InvalidateRect(g_window, &g_fields[id == RATE ? 0 : index + 1], FALSE);
        if (id == RATE) { if (msg == WM_KILLFOCUS) read_rate(TRUE); }
        else {
            g_capturing = msg == WM_SETFOCUS;
            if (g_capturing) {
                change_config(BUTTON_MASK, 0);
                UnregisterHotKey(g_window, HK_LEFT);
                UnregisterHotKey(g_window, HK_RIGHT);
                status(L"Press keys, then Apply.");
            } else if (!bind_keys(g_keys[0], g_keys[1])) status(L"Hotkeys in use. Choose others.");
        }
    }
    if (msg == WM_GETDLGCODE && lp && ((MSG *)lp)->wParam != VK_TAB)
        return DLGC_WANTALLKEYS;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (id == RATE && wp == VK_RETURN) { read_rate(TRUE); return 0; }
        if (id != RATE) {
            BYTE mods = (GetKeyState(VK_CONTROL) < 0 ? HOTKEYF_CONTROL : 0) |
                        (GetKeyState(VK_SHIFT) < 0 ? HOTKEYF_SHIFT : 0) |
                        (GetKeyState(VK_MENU) < 0 ? HOTKEYF_ALT : 0) |
                        ((lp & (1 << 24)) ? HOTKEYF_EXT : 0);
            if (wp == VK_ESCAPE) g_pending[index] = g_keys[index];
            else if (wp == VK_F9 || wp == VK_CONTROL || wp == VK_SHIFT || wp == VK_MENU ||
                     wp == VK_LWIN || wp == VK_RWIN || wp == VK_TAB) return 0;
            else g_pending[index] = MAKEWORD((BYTE)wp, mods);
            key_text(index);
            return 0;
        }
    }
    if ((msg == WM_CHAR || msg == WM_SYSCHAR) && (id != RATE || wp == VK_RETURN)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void slider_point(HWND hwnd, int x) {
    RECT r;
    GetClientRect(hwnd, &r);
    set_rate(1 + MulDiv(clamp(x - px(10), 0, r.right - px(20)), MAX_CPS - 1, r.right - px(20)), TRUE);
}

static LRESULT CALLBACK slider_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    (void)id; (void)data;
    switch (msg) {
    case WM_LBUTTONDOWN:
        SetFocus(hwnd); SetCapture(hwnd); g_drag = 1;
        slider_point(hwnd, GET_X_LPARAM(lp)); return 0;
    case WM_MOUSEMOVE:
        if (g_drag) { slider_point(hwnd, GET_X_LPARAM(lp)); return 0; }
        break;
    case WM_LBUTTONUP:
        if (g_drag) { slider_point(hwnd, GET_X_LPARAM(lp)); g_drag = 0; ReleaseCapture(); return 0; }
        break;
    case WM_CAPTURECHANGED: g_drag = 0; break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: InvalidateRect(hwnd, 0, FALSE); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void rounded(HDC dc, RECT r, COLORREF fill, COLORREF edge, int radius) {
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, fill); SetDCPenColor(dc, edge);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, px(radius), px(radius));
}

/* Two cached, premultiplied-alpha circles. 8x8 coverage is computed only on resize/theme changes. */
static BOOL prepare_thumb(HDC dc) {
    BITMAPINFO info = {0};
    DWORD *pixels;
    HBITMAP bitmap;
    int size = 2 * (px(10) + 1), face = px(6) * 16, inner = px(8) * 16, outer = px(9) * 16;
    int focus, x, y, sx, sy;
    if (g_thumb_size == size && g_thumb_scale == g_scale) return TRUE;
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size * 2; info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&pixels, 0, 0);
    if (!bitmap) return FALSE;
    if (!g_thumb_dc) g_thumb_dc = CreateCompatibleDC(dc);
    if (!g_thumb_dc) { DeleteObject(bitmap); return FALSE; }
    face *= face; inner *= inner; outer *= outer;
    for (focus = 0; focus < 2; ++focus) for (y = 0; y < size; ++y) for (x = 0; x < size; ++x) {
        unsigned accent = 0, background = 0;
        for (sy = 0; sy < 8; ++sy) for (sx = 0; sx < 8; ++sx) {
            int dx = (2 * x - size) * 8 + 2 * sx + 1;
            int dy = (2 * y - size) * 8 + 2 * sy + 1;
            int d = dx * dx + dy * dy;
            if (d < face || (focus && d >= inner && d < outer)) ++accent;
            else if (focus && d < inner) ++background;
        }
        pixels[y * size * 2 + focus * size + x] =
            (((accent + background) * 255 / 64) << 24) |
            (((GetRValue(g_accent) * accent + GetRValue(g_bg) * background) / 64) << 16) |
            (((GetGValue(g_accent) * accent + GetGValue(g_bg) * background) / 64) << 8) |
            ((GetBValue(g_accent) * accent + GetBValue(g_bg) * background) / 64);
    }
    SelectObject(g_thumb_dc, bitmap);
    if (g_thumb_bitmap) DeleteObject(g_thumb_bitmap);
    g_thumb_bitmap = bitmap; g_thumb_size = size; g_thumb_scale = g_scale;
    return TRUE;
}

static LRESULT draw_control(NMCUSTOMDRAW *draw) {
    HWND hwnd = draw->hdr.hwndFrom;
    RECT r = draw->rc, shape;
    HDC dc = draw->hdc;
    int saved;
    if (draw->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    if (hwnd != g_control[SLIDER] && hwnd != g_control[APPLY] &&
        hwnd != g_control[JITTER_BOX] && hwnd != g_control[MODE_BOX]) return CDRF_DODEFAULT;
    saved = SaveDC(dc);
    GetClientRect(hwnd, &r);
    FillRect(dc, &r, g_background);
    if (hwnd == g_control[SLIDER]) {
        int y = (r.bottom + r.top) / 2, x = px(10) + MulDiv((int)SendMessageW(hwnd, TBM_GETPOS, 0, 0) - 1, r.right - px(20), MAX_CPS - 1);
        shape.left = px(10); shape.right = r.right - px(10); shape.top = y - px(2); shape.bottom = y + px(2);
        rounded(dc, shape, g_edge, g_edge, 6);
        shape.right = x + 1;
        rounded(dc, shape, g_accent, g_accent, 6);
        if (prepare_thumb(dc)) {
            BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(dc, x - g_thumb_size / 2, y - g_thumb_size / 2, g_thumb_size, g_thumb_size,
                       g_thumb_dc, GetFocus() == hwnd ? g_thumb_size : 0, 0, g_thumb_size, g_thumb_size, blend);
        } else {
            SelectObject(dc, GetStockObject(DC_BRUSH)); SelectObject(dc, GetStockObject(DC_PEN));
            SetDCBrushColor(dc, g_accent); SetDCPenColor(dc, g_accent);
            Ellipse(dc, x - px(6), y - px(6), x + px(6), y + px(6));
        }
    } else {
        WCHAR text[96];
        BOOL button = hwnd == g_control[APPLY];
        GetWindowTextW(hwnd, text, 96);
        SelectObject(dc, g_font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_text);
        if (button) {
            rounded(dc, r, (draw->uItemState & CDIS_SELECTED) ? g_edge : g_field,
                    (draw->uItemState & CDIS_HOT) ? g_accent : g_edge, 4);
            DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            BOOL checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
            shape.left = 0; shape.right = px(16); shape.top = (r.bottom - px(16)) / 2; shape.bottom = shape.top + px(16);
            rounded(dc, shape, checked ? g_accent : g_field,
                    (draw->uItemState & CDIS_HOT) ? g_accent : g_edge, 2);
            if (checked) {
                POINT tick[6] = { {px(3),shape.top+px(8)}, {px(6),shape.top+px(11)},
                    {px(12),shape.top+px(4)}, {px(13),shape.top+px(5)},
                    {px(6),shape.top+px(13)}, {px(2),shape.top+px(9)} };
                SelectObject(dc, GetStockObject(NULL_PEN)); SetDCBrushColor(dc, g_bg);
                Polygon(dc, tick, 6);
            }
            r.left += px(26);
            DrawTextW(dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        if (GetFocus() == hwnd) { InflateRect(&r, -px(4), -px(4)); DrawFocusRect(dc, &r); }
    }
    RestoreDC(dc, saved);
    return CDRF_SKIPDEFAULT;
}

static void update_font(void) {
    int i, height = px(13);
    HFONT old = g_font, font;
    if (height == g_font_height && g_font) return;
    font = CreateFontW(-height, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                       0, 0, CLEARTYPE_QUALITY, 0, g_font_resource ? L"Core Lexend" : L"Segoe UI");
    if (!font) return;
    g_font = font; g_font_height = height;
    for (i = 0; i < CONTROL_COUNT; ++i) SendMessageW(g_control[i], WM_SETFONT, (WPARAM)g_font, FALSE);
    if (old) DeleteObject(old);
}

static void layout(void) {
    RECT r;
    int i, x, y, width_scale, height_scale;
    static const short positions[CONTROL_COUNT][4] = {
        {18,25,120,22}, {156,23,240,21}, {18,65,120,22}, {156,63,240,21},
        {18,105,120,22}, {156,103,68,21}, {14,138,392,28}, {18,179,192,26},
        {214,179,188,26}, {18,219,140,30}, {240,105,162,22}, {18,260,384,34}
    };
    static const RECT frames[3] = { {150,98,230,128}, {150,18,402,48}, {150,58,402,88} };
    GetClientRect(g_window, &r);
    width_scale = r.right * 96 / CLIENT_W; height_scale = r.bottom * 96 / CLIENT_H;
    g_scale = max(1, min(width_scale, height_scale));
    x = (r.right - px(CLIENT_W)) / 2; y = (r.bottom - px(CLIENT_H)) / 2;
    update_font();
    for (i = 0; i < CONTROL_COUNT; ++i)
        MoveWindow(g_control[i], x + px(positions[i][0]), y + px(positions[i][1]),
                   px(positions[i][2]), px(positions[i][3]), FALSE);
    for (i = 0; i < 3; ++i) {
        const RECT *f = &frames[i];
        SetRect(&g_fields[i], x + px(f->left), y + px(f->top), x + px(f->right), y + px(f->bottom));
    }
    RedrawWindow(g_window, 0, 0, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static void appearance(void) {
    HIGHCONTRASTW hc = { sizeof(hc), 0, 0 };
    BOOL dark;
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
    dark = !(hc.dwFlags & HCF_HIGHCONTRASTON);
    g_bg = dark ? RGB(24, 25, 28) : GetSysColor(COLOR_WINDOW);
    g_field = dark ? RGB(33, 34, 38) : GetSysColor(COLOR_WINDOW);
    g_text = dark ? RGB(227, 228, 232) : GetSysColor(COLOR_WINDOWTEXT);
    g_muted = dark ? RGB(164, 166, 174) : GetSysColor(COLOR_WINDOWTEXT);
    g_edge = dark ? RGB(66, 68, 76) : GetSysColor(COLOR_WINDOWTEXT);
    g_accent = dark ? RGB(147, 176, 207) : GetSysColor(COLOR_HIGHLIGHT);
    if (g_background) DeleteObject(g_background);
    if (g_field_brush) DeleteObject(g_field_brush);
    g_background = CreateSolidBrush(g_bg); g_field_brush = CreateSolidBrush(g_field);
    g_thumb_size = 0;
    update_font();
    DwmSetWindowAttribute(g_window, 20, &dark, sizeof(dark)); /* Unsupported systems ignore this. */
}

static void window_rect(RECT *r, int width, int height) {
    SetRect(r, 0, 0, MulDiv(width, (int)g_dpi, 96), MulDiv(height, (int)g_dpi, 96));
    if (adjust_dpi) adjust_dpi(r, WS_OVERLAPPEDWINDOW, FALSE, 0, g_dpi);
    else AdjustWindowRectEx(r, WS_OVERLAPPEDWINDOW, FALSE, 0);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SETFOCUS:
        if (g_control[RATE]) SetFocus(IsWindow(g_focus) ? g_focus : g_control[RATE]);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && IsChild(hwnd, GetFocus())) g_focus = GetFocus();
        break;
    case WM_COMMAND: {
        int id = LOWORD(wp) - 100;
        if (id == APPLY && HIWORD(wp) == BN_CLICKED) apply_keys();
        else if (id == RATE && HIWORD(wp) == EN_CHANGE && !g_sync) read_rate(FALSE);
        else if ((id == JITTER_BOX || id == MODE_BOX) && HIWORD(wp) == BN_CLICKED) {
            LONG flag = id == JITTER_BOX ? JITTER : SEND_INPUT;
            change_config(flag, SendMessageW(g_control[id], BM_GETCHECK, 0, 0) == BST_CHECKED ? flag : 0);
        }
        return 0;
    }
    case WM_HOTKEY:
        if (wp == HK_EXIT) DestroyWindow(hwnd);
        else if (!g_capturing && (wp == HK_LEFT || wp == HK_RIGHT)) {
            LONG button = wp == HK_LEFT ? LEFT : RIGHT;
            change_config(BUTTON_MASK, (config() & BUTTON_MASK) == button ? 0 : button);
            status(L"F9: quit");
        }
        return 0;
    case WM_HSCROLL:
        if ((HWND)lp == g_control[SLIDER]) set_rate((int)SendMessageW((HWND)lp, TBM_GETPOS, 0, 0), TRUE);
        return 0;
    case WM_NOTIFY:
        if (((NMHDR *)lp)->code == NM_CUSTOMDRAW) return draw_control((NMCUSTOMDRAW *)lp);
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        BOOL field = (HWND)lp == g_control[RATE] || (HWND)lp == g_control[LEFT_KEY] || (HWND)lp == g_control[RIGHT_KEY];
        BOOL muted = (HWND)lp == g_control[FOOTER];
        SetTextColor(dc, (HWND)lp == g_control[STATUS] && (config() & BUTTON_MASK) ? g_accent : muted ? g_muted : g_text);
        SetBkColor(dc, field ? g_field : g_bg);
        return (LRESULT)(field ? g_field_brush : g_background);
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        int i;
        FillRect(dc, &ps.rcPaint, g_background);
        for (i = 0; i < 3; ++i) rounded(dc, g_fields[i], g_field, GetFocus() == g_control[field_ids[i]] ? g_accent : g_edge, 4);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: if (g_control[FOOTER] && wp != SIZE_MINIMIZED) layout(); return 0;
    case WM_GETMINMAXINFO: {
        RECT r; window_rect(&r, (CLIENT_W * 3 + 3) / 4, (CLIENT_H * 3 + 3) / 4);
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = r.right - r.left;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_DPICHANGED:
        g_dpi = HIWORD(wp);
        SetWindowPos(hwnd, 0, ((RECT *)lp)->left, ((RECT *)lp)->top,
                     ((RECT *)lp)->right - ((RECT *)lp)->left, ((RECT *)lp)->bottom - ((RECT *)lp)->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout(); return 0;
    case WM_SETTINGCHANGE: appearance(); layout(); return 0;
    case ENGINE_ERROR:
        status(L"Input failed. Check permissions.");
        if (wp) {
            change_config(BUTTON_MASK, 0);
            MessageBoxW(hwnd, L"Timer failed. Restart the app.", L"CoreAutoclicker", MB_OK | MB_ICONERROR);
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        change_config(QUIT | BUTTON_MASK, QUIT);
        UnregisterHotKey(hwnd, HK_LEFT); UnregisterHotKey(hwnd, HK_RIGHT); UnregisterHotKey(hwnd, HK_EXIT);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int app_main(void) {
    HINSTANCE instance = GetModuleHandleW(0);
    HMODULE user = GetModuleHandleW(L"user32.dll");
    INITCOMMONCONTROLSEX common = { sizeof(common), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    WNDCLASSW wc = {0};
    HRSRC resource;
    DWORD fonts = 0;
    HANDLE thread = 0;
    MSG msg;
    RECT r;
    int i, result = 1;
    static const struct { LPCWSTR type, text; DWORD style; } controls[] = {
        { L"STATIC", L"Left hotkey", 0 }, { L"EDIT", L"F6", WS_TABSTOP | ES_READONLY | ES_CENTER },
        { L"STATIC", L"Right hotkey", 0 }, { L"EDIT", L"F7", WS_TABSTOP | ES_READONLY | ES_CENTER },
        { L"STATIC", L"Clicks/sec", 0 }, { L"EDIT", L"1000", WS_TABSTOP | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL },
        { TRACKBAR_CLASSW, L"Click rate", WS_TABSTOP | TBS_NOTICKS },
        { L"BUTTON", L"SendInput", WS_TABSTOP | BS_AUTOCHECKBOX },
        { L"BUTTON", L"Cursor jitter", WS_TABSTOP | BS_AUTOCHECKBOX },
        { L"BUTTON", L"Apply", WS_TABSTOP | BS_PUSHBUTTON },
        { L"STATIC", L"Paused", SS_RIGHT },
        { L"STATIC", L"F9: quit", 0 }
    };
    window_dpi = (UINT (WINAPI *)(HWND))GetProcAddress(user, "GetDpiForWindow");
    adjust_dpi = (BOOL (WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT))GetProcAddress(user, "AdjustWindowRectExForDpi");
    InitCommonControlsEx(&common);
    g_wake = CreateEventW(0, FALSE, FALSE, 0);
    g_timer = CreateWaitableTimerExW(0, 0, 2 /* HIGH_RESOLUTION: Windows 10 1803+ */, TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!g_timer) g_timer = CreateWaitableTimerW(0, FALSE, 0);
    if (!g_wake || !g_timer) goto cleanup;
    resource = FindResourceW(instance, MAKEINTRESOURCEW(2), MAKEINTRESOURCEW(10));
    if (resource) g_font_resource = AddFontMemResourceEx(LockResource(LoadResource(instance, resource)), SizeofResource(instance, resource), 0, &fonts);
    wc.lpfnWndProc = window_proc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(0, MAKEINTRESOURCEW(32512)); wc.lpszClassName = L"CoreAutoclicker";
    if (!RegisterClassW(&wc)) goto cleanup;
    g_window = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"CoreAutoclicker",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                              CLIENT_W, CLIENT_H, 0, 0, instance, 0);
    if (!g_window) goto cleanup;
    if (window_dpi) g_dpi = window_dpi(g_window);
    else { HDC dc = GetDC(g_window); g_dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(g_window, dc); }
    g_scale = (int)g_dpi;
    for (i = 0; i < CONTROL_COUNT; ++i) {
        g_control[i] = CreateWindowExW(0, controls[i].type, controls[i].text, WS_CHILD | WS_VISIBLE | controls[i].style,
                                      0, 0, 0, 0, g_window, (HMENU)(INT_PTR)(100 + i), instance, 0);
        if (!g_control[i]) goto cleanup;
    }
    for (i = 0; i < 3; ++i) SetWindowSubclass(g_control[field_ids[i]], field_proc, (UINT_PTR)field_ids[i], 0);
    SetWindowSubclass(g_control[SLIDER], slider_proc, 0, 0);
    SendMessageW(g_control[RATE], EM_SETLIMITTEXT, 4, 0);
    SendMessageW(g_control[MODE_BOX], BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_control[SLIDER], TBM_SETRANGE, FALSE, MAKELPARAM(1, MAX_CPS));
    SendMessageW(g_control[SLIDER], TBM_SETPAGESIZE, 0, 10);
    set_rate(MAX_CPS, TRUE);
    appearance(); window_rect(&r, CLIENT_W, CLIENT_H);
    SetWindowPos(g_window, 0, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
    layout();
    if (!RegisterHotKey(g_window, HK_EXIT, MOD_NOREPEAT, VK_F9)) {
        MessageBoxW(g_window, L"F9 is in use. Close the other app, then retry.", L"CoreAutoclicker", MB_OK | MB_ICONERROR);
        goto cleanup;
    }
    if (!bind_keys(g_keys[0], g_keys[1])) status(L"Hotkeys in use. Choose others.");
    thread = CreateThread(0, 0, click_thread, 0, 0, 0);
    if (!thread) goto cleanup;
    ShowWindow(g_window, SW_SHOWNORMAL);
    while ((result = (int)GetMessageW(&msg, 0, 0, 0)) > 0) {
        if (!IsDialogMessageW(g_window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    result = result < 0 ? 1 : 0;
cleanup:
    change_config(QUIT | BUTTON_MASK, QUIT);
    if (IsWindow(g_window)) DestroyWindow(g_window);
    if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
    if (g_timer) CloseHandle(g_timer);
    if (g_wake) CloseHandle(g_wake);
    if (g_font) DeleteObject(g_font);
    if (g_background) DeleteObject(g_background);
    if (g_field_brush) DeleteObject(g_field_brush);
    if (g_font_resource) RemoveFontMemResourceEx(g_font_resource);
    if (g_thumb_dc) DeleteDC(g_thumb_dc);
    if (g_thumb_bitmap) DeleteObject(g_thumb_bitmap);
    return result;
}

__declspec(noreturn) void WINAPI win_main_crt_startup(void) { ExitProcess((UINT)app_main()); }
