/*
 * peim_tray.c —— 托盘图标 / PE 右下角状态方框（实现见 peim_tray.h）
 *
 * 只做两件事：显示当前输入模式（中/英/键盘）；把"切换/退出"请求发给主程序的消息窗口。
 */
#include "peim_tray.h"
#include "peim_config.h"

#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRAY_ID        1
#define TRAY_CALLBACK  (WM_APP + 32)
#define MENU_TOGGLE    1
#define MENU_QUIT      2
#define IND_SIZE       30

static HWND        g_notify;
static HINSTANCE   g_hinst;
static HWND        g_tray_wnd;
static HWND        g_ind_wnd;
static HICON       g_icon_cn;
static HICON       g_icon_en;
static HICON       g_icon_raw;
static int         g_input_mode = PEIM_MODE_EN;
static int         g_ui_mode;   /* 0=无 1=托盘 2=右下角方框 */

/* 16x16/30x30 共用的键盘图形（白线，蓝底由调用方填充） */
static void draw_keyboard(HDC dc, const RECT *rc)
{
    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    int x0 = rc->left + w / 8;
    int y0 = rc->top + h / 4;
    int x1 = rc->right - w / 8;
    int y1 = rc->bottom - h / 4;
    int row, col;
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ oldpen = SelectObject(dc, pen);
    HGDIOBJ oldbrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    Rectangle(dc, x0, y0, x1, y1);
    for (row = 1; row <= 2; ++row) {
        int ky = y0 + row * (y1 - y0) / 3;
        for (col = 1; col <= 4; ++col) {
            int kx = x0 + col * (x1 - x0) / 5;
            Rectangle(dc, kx, ky, kx + 2, ky + 2);
        }
    }
    SelectObject(dc, oldbrush);
    SelectObject(dc, oldpen);
    DeleteObject(pen);
}

/* 生成 16x16 图标：0=中，1=英，2=键盘 */
static HICON make_icon(int mode)
{
    static const BYTE zero_mask[16 * 2] = { 0 };
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 16, 16);
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, zero_mask);
    ICONINFO ii;
    HICON icon;
    HGDIOBJ old = SelectObject(dc, color);
    RECT rc = { 0, 0, 16, 16 };
    HBRUSH bg = CreateSolidBrush(RGB(0, 96, 172));

    FillRect(dc, &rc, bg);
    if (mode == PEIM_MODE_RAW) {
        draw_keyboard(dc, &rc);
    } else {
        HFONT font;
        HGDIOBJ oldfont;
        WCHAR txt[2];
        font = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"宋体");
        oldfont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        txt[0] = mode == PEIM_MODE_CN ? 0x4E2D : 0x82F1;
        txt[1] = 0;
        DrawTextW(dc, txt, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldfont);
        DeleteObject(font);
    }

    SelectObject(dc, old);
    DeleteObject(bg);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);

    memset(&ii, 0, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

static HICON mode_icon(void)
{
    if (g_input_mode == PEIM_MODE_CN)
        return g_icon_cn;
    if (g_input_mode == PEIM_MODE_RAW)
        return g_icon_raw;
    return g_icon_en;
}

static const WCHAR *mode_tip(void)
{
    if (g_input_mode == PEIM_MODE_CN)
        return L"PEIM 输入法 - 中文";
    if (g_input_mode == PEIM_MODE_RAW)
        return L"PEIM 输入法 - 英文原文";
    return L"PEIM 输入法 - 英文（补全）";
}
static void show_menu(HWND owner, HWND notify)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;
    int cmd;

    AppendMenuW(menu, MF_STRING, MENU_TOGGLE, L"切换输入模式（中/英/原文）");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_QUIT, L"退出");

    GetCursorPos(&pt);
    SetForegroundWindow(owner);
    cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                              pt.x, pt.y, 0, owner, NULL);
    DestroyMenu(menu);
    PostMessageW(owner, WM_NULL, 0, 0);

    if (cmd == MENU_TOGGLE)
        PostMessageW(notify, PEIM_TRAY_MSG_TOGGLE, 0, 0);
    else if (cmd == MENU_QUIT)
        PostMessageW(notify, PEIM_TRAY_MSG_QUIT, 0, 0);
}

/* ---------------- 托盘模式 ---------------- */

static void update_tray(void)
{
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_tray_wnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = mode_icon();
    lstrcpynW(nid.szTip, mode_tip(), 64);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static LRESULT CALLBACK tray_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == TRAY_CALLBACK) {
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
            PostMessageW(g_notify, PEIM_TRAY_MSG_TOGGLE, 0, 0);
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            show_menu(wnd, g_notify);
            return 0;
        default:
            return 0;
        }
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static int init_tray(void)
{
    WNDCLASSEXW wc;
    NOTIFYICONDATAW nid;
    WCHAR cls[] = L"PEIMTrayWnd";

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = tray_proc;
    wc.hInstance = g_hinst;
    wc.lpszClassName = cls;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;

    g_tray_wnd = CreateWindowExW(0, cls, L"", WS_POPUP, 0, 0, 0, 0,
                                 NULL, NULL, g_hinst, NULL);
    if (!g_tray_wnd)
        return 0;

    g_icon_cn = make_icon(PEIM_MODE_CN);
    g_icon_en = make_icon(PEIM_MODE_EN);
    g_icon_raw = make_icon(PEIM_MODE_RAW);

    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_tray_wnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = TRAY_CALLBACK;
    nid.hIcon = mode_icon();
    lstrcpynW(nid.szTip, mode_tip(), 64);
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        DestroyWindow(g_tray_wnd);
        g_tray_wnd = NULL;
        return 0;
    }
    return 1;
}

/* ---------------- 无托盘：右下角方框 ---------------- */

static void ind_place(void)
{
    RECT wa;
    int x, y;

    if (!g_ind_wnd)
        return;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
        wa.left = 0; wa.top = 0;
        wa.right = GetSystemMetrics(SM_CXSCREEN);
        wa.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    x = wa.right - IND_SIZE - 8;
    y = wa.bottom - IND_SIZE - 8;
    SetWindowPos(g_ind_wnd, HWND_TOPMOST, x, y, IND_SIZE, IND_SIZE,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void ind_paint(HWND wnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(wnd, &ps);
    RECT rc;
    HBRUSH bg;
    HGDIOBJ old;
    HPEN pen;
    HGDIOBJ oldpen;
    HFONT font;
    HGDIOBJ oldfont;
    WCHAR txt[2];

    GetClientRect(wnd, &rc);
    bg = CreateSolidBrush(RGB(0, 96, 172));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    pen = CreatePen(PS_SOLID, 1, RGB(250, 250, 250));
    oldpen = SelectObject(dc, pen);
    old = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, rc.right, rc.bottom);
    SelectObject(dc, old);
    SelectObject(dc, oldpen);
    DeleteObject(pen);

    if (g_input_mode == PEIM_MODE_RAW) {
        draw_keyboard(dc, &rc);
    } else {
        font = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"宋体");
        oldfont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        txt[0] = g_input_mode == PEIM_MODE_CN ? 0x4E2D : 0x82F1;
        txt[1] = 0;
        DrawTextW(dc, txt, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldfont);
        DeleteObject(font);
    }
    EndPaint(wnd, &ps);
}

static LRESULT CALLBACK ind_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp; (void)lp;
    switch (msg) {
    case WM_PAINT:
        ind_paint(wnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
        PostMessageW(g_notify, PEIM_TRAY_MSG_TOGGLE, 0, 0);
        return 0;
    case WM_RBUTTONDOWN:
        show_menu(wnd, g_notify);
        return 0;
    case WM_DISPLAYCHANGE:
        ind_place();
        return 0;
    default:
        return DefWindowProcW(wnd, msg, wp, lp);
    }
}

static int init_indicator(void)
{
    WNDCLASSEXW wc;
    WCHAR cls[] = L"PEIMIndWnd";

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ind_proc;
    wc.hInstance = g_hinst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = cls;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;

    g_ind_wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                cls, L"PEIM", WS_POPUP,
                                0, 0, IND_SIZE, IND_SIZE, NULL, NULL, g_hinst, NULL);
    if (!g_ind_wnd)
        return 0;
    ind_place();
    return 1;
}

/* ---------------- 对外接口 ---------------- */

int peim_tray_init(HINSTANCE hinst, HWND notify)
{
    g_hinst = hinst;
    g_notify = notify;
    g_ui_mode = 0;

    /* 调试用：设置 PEIM_INDICATOR=1 可强制走"右下角方框"模式 */
    if (getenv("PEIM_INDICATOR")) {
        if (init_indicator())
            g_ui_mode = 2;
    } else if (init_tray()) {
        g_ui_mode = 1;
    } else if (init_indicator()) {
        g_ui_mode = 2;
    }
    return g_ui_mode == 1 ? 1 : (g_ui_mode == 2 ? 0 : -1);
}

void peim_tray_set_mode(int mode)
{
    if (mode < PEIM_MODE_CN || mode > PEIM_MODE_RAW)
        mode = PEIM_MODE_EN;
    g_input_mode = mode;
    if (g_tray_wnd) {
        update_tray();
    } else if (g_ind_wnd) {
        InvalidateRect(g_ind_wnd, NULL, FALSE);
        UpdateWindow(g_ind_wnd);
        ind_place();
    }
}

void peim_tray_destroy(void)
{
    if (g_tray_wnd) {
        NOTIFYICONDATAW nid;
        memset(&nid, 0, sizeof(nid));
        nid.cbSize = sizeof(nid);
        nid.hWnd = g_tray_wnd;
        nid.uID = TRAY_ID;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        DestroyWindow(g_tray_wnd);
        g_tray_wnd = NULL;
    }
    if (g_ind_wnd) {
        DestroyWindow(g_ind_wnd);
        g_ind_wnd = NULL;
    }
    if (g_icon_cn) {
        DestroyIcon(g_icon_cn);
        g_icon_cn = NULL;
    }
    if (g_icon_en) {
        DestroyIcon(g_icon_en);
        g_icon_en = NULL;
    }
    if (g_icon_raw) {
        DestroyIcon(g_icon_raw);
        g_icon_raw = NULL;
    }

}



