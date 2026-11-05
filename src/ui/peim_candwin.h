/*
 * peim_candwin.h —— 候选窗（自绘，无边框，不抢焦点）
 */
#ifndef PEIM_CANDWIN_H
#define PEIM_CANDWIN_H

#include <windows.h>

#define PEIM_CAND_MAX_CHARS 32
#define PEIM_CAND_MAX_ITEMS 9

/* 候选窗发出的翻页请求：wParam = -1 上一页 / +1 下一页，发给 view.notify */
#define PEIM_CAND_MSG_PAGE (WM_APP + 21)

typedef struct {
    int   mode;                       /* 1=中文 0=英文 */
    char  pinyin[64];                 /* 已输入拼音（ASCII），可为空 */
    int   ncand;                      /* 本页候选数（<= items） */
    WCHAR cands[PEIM_CAND_MAX_ITEMS][PEIM_CAND_MAX_CHARS + 1];
    int   page;                       /* 0 基 */
    int   pages;
    int   font_height;
    HWND  notify;                     /* 翻页按钮点击时消息发往的窗口 */
} PeimCandView;

HWND peim_cand_create(HINSTANCE hinst);
void peim_cand_destroy(HWND wnd);
/* pt_screen: 候选窗左上角参考点（屏幕坐标） */
void peim_cand_show(HWND wnd, const PeimCandView *view, POINT pt_screen);
void peim_cand_hide(HWND wnd);
void peim_cand_flash(HWND wnd, int mode, int font_height, POINT pt_screen);

#endif /* PEIM_CANDWIN_H */




