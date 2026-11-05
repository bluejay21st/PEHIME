#include "pehime_log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define PEHIME_LOG_MAX_BYTES (512 * 1024)

static FILE *g_file;
static char  g_path[MAX_PATH];

void pehime_log_open(const char *exe_dir)
{
    const char *dir = getenv("PEHIME_LOG_DIR");

    if (!exe_dir || !exe_dir[0])
        return;

    /* 宿主测试时可用 PEHIME_LOG_DIR 指定日志目录（PE 里用 DLL 同目录） */
    if (!dir || !dir[0])
        dir = exe_dir;

    CreateDirectoryA(dir, NULL);
    _snprintf(g_path, sizeof(g_path) - 1, "%s\\PEHIME.log", dir);
    g_path[sizeof(g_path) - 1] = 0;

    /* 超过上限就轮转一次，避免 PE 的 RAM 盘被日志吃掉 */
    {
        FILE *t = fopen(g_path, "rb");
        if (t) {
            long sz;
            fseek(t, 0, SEEK_END);
            sz = ftell(t);
            fclose(t);
            if (sz > PEHIME_LOG_MAX_BYTES) {
                char old[MAX_PATH];
                _snprintf(old, sizeof(old) - 1, "%s.old", g_path);
                old[sizeof(old) - 1] = 0;
                DeleteFileA(old);
                MoveFileA(g_path, old);
            }
        }
    }

    g_file = fopen(g_path, "a");
    if (g_file) {
        time_t now = time(NULL);
        struct tm *lt = localtime(&now);
        fprintf(g_file, "\n===== PEHIME start %04d-%02d-%02d %02d:%02d:%02d =====\n",
                lt ? lt->tm_year + 1900 : 0, lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
                lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
        fflush(g_file);
    }
}

void pehime_log(const char *fmt, ...)
{
    va_list ap;
    SYSTEMTIME st;

    if (!g_file)
        return;
    GetLocalTime(&st);
    fprintf(g_file, "%02d:%02d:%02d.%03d [%lu] ", st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, (unsigned long)GetCurrentThreadId());
    va_start(ap, fmt);
    vfprintf(g_file, fmt, ap);
    va_end(ap);
    fputc('\n', g_file);
    fflush(g_file);
}

void pehime_log_close(void)
{
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
}


