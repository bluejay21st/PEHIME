/*
 * peim_tray.c —— 托盘图标 / PE 右下角状态方框（实现见 peim_tray.h）
 *
 * 只做两件事：显示当前中/英状态；把"切换/退出"请求发给主程序的消息窗口。
 */
#include "peim_tray.h"

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
static int         g_cn = 1;
static int         g_mode;   /* 0=无 1=托盘 2=右下角方框 */

/* 生成一个带"中/英"字样的 16x16 图标 */
static HICON make_icon(int cn)
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
    HFONT font;
    HGDIOBJ oldfont;
    WCHAR txt[2];

    FillRect(dc, &rc, bg);
    font = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"宋体");
    oldfont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    txt[0] = cn ? 0x4E2D : 0x82F1;
    txt[1] = 0;
    DrawTextW(dc, txt, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldfont);

    SelectObject(dc, old);
    DeleteObject(bg);
    DeleteObject(font);
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

static void show_menu(HWND owner, HWND notify)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;
    int cmd;

    AppendMenuW(menu, MF_STRING, MENU_TOGGLE, L"切换中/英文");
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
    nid.hIcon = g_cn ? g_icon_cn : g_icon_en;
    lstrcpynW(nid.szTip, g_cn ? L"PEIM 输入法 - 中文" : L"PEIM 输入法 - 英文", 64);
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

    g_icon_cn = make_icon(1);
    g_icon_en = make_icon(0);

    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_tray_wnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = TRAY_CALLBACK;
    nid.hIcon = g_cn ? g_icon_cn : g_icon_en;
    lstrcpynW(nid.szTip, g_cn ? L"PEIM 输入法 - 中文" : L"PEIM 输入法 - 英文", 64);
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

    font = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"宋体");
    oldfont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    txt[0] = g_cn ? 0x4E2D : 0x82F1;
    txt[1] = 0;
    DrawTextW(dc, txt, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldfont);
    DeleteObject(font);
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
    g_mode = 0;

    /* 调试用：设置 PEIM_INDICATOR=1 可强制走"右下角方框"模式 */
    if (getenv("PEIM_INDICATOR")) {
        if (init_indicator())
            g_mode = 2;
    } else if (init_tray()) {
        g_mode = 1;
    } else if (init_indicator()) {
        g_mode = 2;
    }
    return g_mode == 1 ? 1 : (g_mode == 2 ? 0 : -1);
}

void peim_tray_set_mode(int cn)
{
    g_cn = cn ? 1 : 0;
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

}



