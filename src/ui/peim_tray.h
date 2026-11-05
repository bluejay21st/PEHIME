/*
 * peim_tray.h —— 托盘图标 / PE 右下角状态方框
 *
 * 有托盘（存在外壳）：显示托盘图标，右键菜单切换中/英、退出。
 * 无托盘（WinPE 没有 Explorer 外壳）：在屏幕右下角显示一个小方框，
 *   显示当前"中/英"，左键单击切换，右键菜单切换/退出。
 *
 * 两种界面都通过消息通知主程序：
 *   PEIM_TRAY_MSG_TOGGLE -> 切换中/英
 *   PEIM_TRAY_MSG_QUIT   -> 退出
 */
#ifndef PEIM_TRAY_H
#define PEIM_TRAY_H

#include <windows.h>

#define PEIM_TRAY_MSG_TOGGLE (WM_APP + 30)
#define PEIM_TRAY_MSG_QUIT   (WM_APP + 31)

int  peim_tray_init(HINSTANCE hinst, HWND notify);  /* 1=托盘 0=右下角方框 -1=失败 */
void peim_tray_set_mode(int cn);
void peim_tray_destroy(void);

#endif /* PEIM_TRAY_H */

