#include "pehime_candwin.h"

#include <string.h>

#define PEHIME_CAND_CLASS L"PEHIMECandWnd"

#define CPAD_X   6
#define CPAD_Y   5
#define CGAP     10
#define BORDER   1
#define FLASH_MS 900
#define CARET_MS 500
#define ROW_GAP  6
#define CARET_W  2
#define CARET_GAP 3

static HWND            g_wnd;
static PehimeCandView    g_view;
static HFONT           g_font;
static int             g_font_h = 16;
static UINT_PTR        g_timer;        /* 中/英提示自动隐藏 */
static UINT_PTR        g_caret_timer;  /* 拼音后的闪烁光标 */
static int             g_caret_on = 1;
static RECT            g_up_rect;
static RECT            g_dn_rect;
static int             g_show_arrows;

static const COLORREF CLR_BG     = RGB(255, 255, 255);
static const COLORREF CLR_BORDER = RGB(150, 150, 150);
static const COLORREF CLR_TEXT   = RGB(0, 0, 0);
static const COLORREF CLR_GRAY   = RGB(110, 110, 110);
static const COLORREF CLR_PAGE   = RGB(60, 100, 180);
static const COLORREF CLR_CARET  = RGB(0, 0, 0);

static HFONT make_font(int height)
{
    return CreateFontW(-height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"宋体");
}

static void ensure_font(int height)
{
    if (g_font && g_font_h == height)
        return;
    if (g_font)
        DeleteObject(g_font);
    g_font_h = height;
    g_font = make_font(height);
}

static void mode_text(WCHAR *buf, int mode)
{
    buf[0] = mode ? 0x4E2D : 0x82F1; /* 中 / 英 */
    buf[1] = 0;
}

/* 当前是否是"组字显示"（第一行显示拼音） */
static int has_pinyin_row(const PehimeCandView *v)
{
    return v->pinyin[0] != 0;
}

/* 需要几行：拼音行 + 候选行；没有候选时只显示拼音行 */
static int row_count(const PehimeCandView *v)
{
    if (!has_pinyin_row(v))
        return 1;
    return v->ncand > 0 ? 2 : 1;
}

static int pinyin_wide(const PehimeCandView *v, WCHAR *wp, int cap)
{
    int n = MultiByteToWideChar(CP_ACP, 0, v->pinyin, -1, wp, cap) - 1;
    return n > 0 ? n : 0;
}

/* 计算窗口宽度：取"拼音行"与"候选行"中较宽的一行 */
static int measure_width(HDC dc, const PehimeCandView *v)
{
    SIZE sz;
    int w1 = 0, w2 = 0, i, first = 1;

    if (has_pinyin_row(v)) {
        WCHAR wp[64];
        int n = pinyin_wide(v, wp, 64);
        if (n > 0) {
            GetTextExtentPoint32W(dc, wp, n, &sz);
            w1 = sz.cx + CARET_GAP + CARET_W;
        }
    }

    for (i = 0; i < v->ncand; ++i) {
        WCHAR item[PEHIME_CAND_MAX_CHARS + 4];
        int n = 0;
        item[n++] = (WCHAR)('1' + i);
        item[n++] = L'.';
        {
            int k = 0;
            while (v->cands[i][k] && k < PEHIME_CAND_MAX_CHARS)
                item[n++] = v->cands[i][k++];
        }
        GetTextExtentPoint32W(dc, item, n, &sz);
        w2 += (first ? 0 : CGAP) + sz.cx;
        first = 0;
    }

    if (v->pages > 1) {
        WCHAR page[32];
        int n = wsprintfW(page, L"%d/%d", v->page + 1, v->pages);
        GetTextExtentPoint32W(dc, page, n, &sz);
        w2 += CGAP + sz.cx;
        w2 += 8 + (v->font_height + 6 + 2) * 2;   /* ▲ ▼ */
    }

    {
        int w = (w1 > w2 ? w1 : w2) + CPAD_X * 2;
        if (w < 40)
            w = 40;
        return w;
    }
}

static void draw_page_arrows(HDC mem, int x, int y)
{
    int bw = g_view.font_height + 6;
    int bh = g_view.font_height;
    int tx = x;

    /* ▲ 上一页 */
    g_up_rect.left = tx;
    g_up_rect.top = y;
    g_up_rect.right = tx + bw;
    g_up_rect.bottom = y + bh;
    {
        HBRUSH b = CreateSolidBrush(RGB(238, 238, 238));
        HPEN p = CreatePen(PS_SOLID, 1, CLR_BORDER);
        HGDIOBJ ob = SelectObject(mem, b);
        HGDIOBJ op = SelectObject(mem, p);
        Rectangle(mem, tx, y, tx + bw, y + bh);
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(b);
        DeleteObject(p);
    }
    {
        POINT tri[3];
        tri[0].x = tx + bw / 2;     tri[0].y = y + 4;
        tri[1].x = tx + bw / 2 - 5; tri[1].y = y + bh - 5;
        tri[2].x = tx + bw / 2 + 5; tri[2].y = y + bh - 5;
        SetTextColor(mem, CLR_GRAY);
        Polygon(mem, tri, 3);
    }

    tx = x + bw + 2;
    /* ▼ 下一页 */
    g_dn_rect.left = tx;
    g_dn_rect.top = y;
    g_dn_rect.right = tx + bw;
    g_dn_rect.bottom = y + bh;
    {
        HBRUSH b = CreateSolidBrush(RGB(238, 238, 238));
        HPEN p = CreatePen(PS_SOLID, 1, CLR_BORDER);
        HGDIOBJ ob = SelectObject(mem, b);
        HGDIOBJ op = SelectObject(mem, p);
        Rectangle(mem, tx, y, tx + bw, y + bh);
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(b);
        DeleteObject(p);
    }
    {
        POINT tri[3];
        tri[0].x = tx + bw / 2;     tri[0].y = y + bh - 4;
        tri[1].x = tx + bw / 2 - 5; tri[1].y = y + 5;
        tri[2].x = tx + bw / 2 + 5; tri[2].y = y + 5;
        SetTextColor(mem, CLR_GRAY);
        Polygon(mem, tri, 3);
    }
    g_show_arrows = 1;
}

static void paint(HWND wnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(wnd, &ps);
    RECT rc;
    HDC mem;
    HBITMAP bmp, oldbmp;
    int x, y, y1, y2, i;
    HBRUSH br;

    GetClientRect(wnd, &rc);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1);
    oldbmp = (HBITMAP)SelectObject(mem, bmp);

    br = CreateSolidBrush(CLR_BG);
    FillRect(mem, &rc, br);
    DeleteObject(br);

    {
        HPEN pen = CreatePen(PS_SOLID, BORDER, CLR_BORDER);
        HGDIOBJ oldpen = SelectObject(mem, pen);
        HGDIOBJ oldbr = SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, 0, 0, rc.right, rc.bottom);
        SelectObject(mem, oldbr);
        SelectObject(mem, oldpen);
        DeleteObject(pen);
    }

    ensure_font(g_view.font_height);
    SelectObject(mem, g_font);
    SetBkMode(mem, TRANSPARENT);
    y1 = CPAD_Y + BORDER;
    y2 = y1 + g_font_h + ROW_GAP;
    x = CPAD_X + BORDER;
    g_show_arrows = 0;

    if (has_pinyin_row(&g_view)) {
        WCHAR wp[64];
        int n = pinyin_wide(&g_view, wp, 64);
        SIZE sz;

        /* 第一行：拼音 */
        SetTextColor(mem, CLR_TEXT);
        if (n > 0)
            TextOutW(mem, x, y1, wp, n);
        GetTextExtentPoint32W(mem, wp, n, &sz);

        /* 拼音后面的闪烁输入符 */
        if (g_caret_on) {
            RECT cr;
            cr.left = x + sz.cx + CARET_GAP;
            cr.top = y1 + 2;
            cr.right = cr.left + CARET_W;
            cr.bottom = y1 + g_font_h - 2;
            {
                HBRUSH cb = CreateSolidBrush(CLR_CARET);
                FillRect(mem, &cr, cb);
                DeleteObject(cb);
            }
        }
    }

    if (g_view.ncand > 0) {
        y = has_pinyin_row(&g_view) ? y2 : y1;
        x = CPAD_X + BORDER;
        for (i = 0; i < g_view.ncand; ++i) {
            WCHAR item[PEHIME_CAND_MAX_CHARS + 4];
            int n = 0;
            SIZE sz;
            item[n++] = (WCHAR)('1' + i);
            item[n++] = L'.';
            {
                int k = 0;
                while (g_view.cands[i][k] && k < PEHIME_CAND_MAX_CHARS)
                    item[n++] = g_view.cands[i][k++];
            }
            if (i > 0)
                x += CGAP;
            SetTextColor(mem, CLR_TEXT);
            TextOutW(mem, x, y, item, n);
            GetTextExtentPoint32W(mem, item, n, &sz);
            x += sz.cx;
        }

        if (g_view.pages > 1) {
            WCHAR page[32];
            int n = wsprintfW(page, L"%d/%d", g_view.page + 1, g_view.pages);
            SIZE sz;
            x += CGAP;
            SetTextColor(mem, CLR_PAGE);
            TextOutW(mem, x, y, page, n);
            GetTextExtentPoint32W(mem, page, n, &sz);
            x += sz.cx + 6;
            draw_page_arrows(mem, x, y);
        }
    } else if (!has_pinyin_row(&g_view)) {
        /* 中/英提示：单行居中显示（不再是候选行开头） */
        WCHAR m[2];
        SIZE sz;
        mode_text(m, g_view.mode);
        SetTextColor(mem, CLR_TEXT);
        GetTextExtentPoint32W(mem, m, 1, &sz);
        x = (rc.right - sz.cx) / 2;
        if (x < CPAD_X)
            x = CPAD_X;
        TextOutW(mem, x, y1, m, 1);
    }

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldbmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(wnd, &ps);
}

static void stop_timers(HWND wnd)
{
    if (g_timer) {
        KillTimer(wnd, g_timer);
        g_timer = 0;
    }
    if (g_caret_timer) {
        KillTimer(wnd, g_caret_timer);
        g_caret_timer = 0;
    }
}

static LRESULT CALLBACK cand_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        paint(wnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        if (g_caret_timer && wp == g_caret_timer) {
            g_caret_on = !g_caret_on;
            InvalidateRect(wnd, NULL, FALSE);
            return 0;
        }
        if (g_timer && wp == g_timer) {
            KillTimer(wnd, g_timer);
            g_timer = 0;
            ShowWindow(wnd, SW_HIDE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (g_show_arrows) {
            POINT p;
            p.x = (short)LOWORD(lp);
            p.y = (short)HIWORD(lp);
            if (PtInRect(&g_up_rect, p)) {
                PostMessageW(g_view.notify, PEHIME_CAND_MSG_PAGE, (WPARAM)-1, 0);
                return 0;
            }
            if (PtInRect(&g_dn_rect, p)) {
                PostMessageW(g_view.notify, PEHIME_CAND_MSG_PAGE, (WPARAM)+1, 0);
                return 0;
            }
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

HWND pehime_cand_create(HINSTANCE hinst)
{
    WNDCLASSEXW wc;

    if (g_wnd)
        return g_wnd;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = cand_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = PEHIME_CAND_CLASS;
    RegisterClassExW(&wc);

    g_wnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        PEHIME_CAND_CLASS, L"", WS_POPUP,
        0, 0, 10, 10, NULL, NULL, hinst, NULL);
    return g_wnd;
}

void pehime_cand_destroy(HWND wnd)
{
    if (wnd) {
        stop_timers(wnd);
        DestroyWindow(wnd);
        g_wnd = NULL;
    }
    if (g_font) {
        DeleteObject(g_font);
        g_font = NULL;
    }
}

void pehime_cand_show(HWND wnd, const PehimeCandView *view, POINT pt_screen)
{
    HDC dc;
    int w, h, rows;
    int x, y;
    RECT wa;

    if (!wnd || !view)
        return;
    g_view = *view;
    stop_timers(wnd);
    ensure_font(g_view.font_height);

    /* 组字时启动闪烁光标；中/英提示时不需要 */
    if (has_pinyin_row(&g_view)) {
        g_caret_on = 1;
        g_caret_timer = SetTimer(wnd, 2, CARET_MS, NULL);
    }

    dc = GetDC(wnd);
    {
        HGDIOBJ oldf = SelectObject(dc, g_font);
        w = measure_width(dc, &g_view);
        SelectObject(dc, oldf);
    }
    ReleaseDC(wnd, dc);

    rows = row_count(&g_view);
    h = rows * g_font_h + (rows - 1) * ROW_GAP + CPAD_Y * 2 + BORDER * 2;

    x = pt_screen.x;
    y = pt_screen.y;

    wa.left = 0;
    wa.top = 0;
    wa.right = GetSystemMetrics(SM_CXSCREEN);
    wa.bottom = GetSystemMetrics(SM_CYSCREEN);

    if (x + w > wa.right - 2)
        x = wa.right - w - 2;
    if (x < 2)
        x = 2;
    if (y + h > wa.bottom - 2)
        y = pt_screen.y - h - 20; /* 光标在屏幕底部下方时翻到上面 */
    if (y < 2)
        y = 2;

    SetWindowPos(wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(wnd, NULL, FALSE);
    UpdateWindow(wnd);
}

void pehime_cand_hide(HWND wnd)
{
    if (!wnd)
        return;
    stop_timers(wnd);
    ShowWindow(wnd, SW_HIDE);
}

void pehime_cand_flash(HWND wnd, int mode, int font_height, POINT pt_screen)
{
    PehimeCandView v;
    memset(&v, 0, sizeof(v));
    v.mode = mode;
    v.font_height = font_height > 0 ? font_height : g_font_h;
    pehime_cand_show(wnd, &v, pt_screen);
    stop_timers(wnd);
    g_timer = SetTimer(wnd, 1, FLASH_MS, NULL);
}


