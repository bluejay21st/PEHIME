#include "peim_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

static void trim(char *s)
{
    char *p = s;
    size_t n;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
}

/* peim.ini 不存在时生成默认配置（UTF-8 BOM + 中文注释，方便在 PE 里用记事本编辑） */
static void write_default_config(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fputs("\xEF\xBB\xBF"
          "; start_mode    0=中文，1=英文（带补全，默认），2=英文原文（无候选）\n"
          "start_mode=1\n"
          "; page_size     每页候选数 3~9\n"
          "page_size=5\n"
          "; font_height   候选窗字体像素高 10~40\n"
          "font_height=16\n"
          "; en_suggest    1=英文模式显示单词补全\n"
          "en_suggest=1\n"
          "; log           1=写 PEIM.log\n"
          "log=1\n"
          "; log_verbose   1=记录每次按键与候选列表（调试）\n"
          "log_verbose=0\n", f);
    fclose(f);
}

void peim_config_load(PeimConfig *cfg, const char *exe_dir)
{
    char path[MAX_PATH * 2];
    FILE *f;
    char line[512];
    int have_start_mode = 0;

    if (!cfg)
        return;
    cfg->start_mode = PEIM_MODE_EN;
    cfg->page_size = 5;
    cfg->font_height = 16;
    cfg->en_suggest = 1;
    cfg->log = 1;
    cfg->log_verbose = 0;

    if (!exe_dir || !exe_dir[0])
        return;

    /* 环境变量 PEIM_INI 可覆盖路径（测试用），默认读 EXE 同目录的 peim.ini */
    {
        const char *ini = getenv("PEIM_INI");
        if (ini && ini[0])
            lstrcpynA(path, ini, (int)(sizeof(path) - 1));
        else
            _snprintf(path, sizeof(path) - 1, "%s\\peim.ini", exe_dir);
        path[sizeof(path) - 1] = 0;
    }

    f = fopen(path, "r");
    if (!f) {
        /* 文件不存在（或不可读）：生成默认配置，然后按默认值运行 */
        write_default_config(path);
        return;
    }

    while (fgets(line, sizeof(line), f)) {
        char *eq, *key, *val;
        /* 跳过自动生成配置的 UTF-8 BOM */
        if ((unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            memmove(line, line + 3, strlen(line + 3) + 1);
        trim(line);
        if (!line[0] || line[0] == ';' || line[0] == '#' || line[0] == '[')
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        key = line;
        val = eq + 1;
        trim(key);
        trim(val);
        if (!_stricmp(key, "start_mode")) {
            int v = atoi(val);
            if (v >= 0 && v < PEIM_MODE_COUNT) {
                cfg->start_mode = v;
                have_start_mode = 1;
            }
        } else if (!_stricmp(key, "start_cn")) {
            /* 兼容旧配置：start_cn=1 -> 中文，0 -> 英文（带补全） */
            if (!have_start_mode)
                cfg->start_mode = atoi(val) ? PEIM_MODE_CN : PEIM_MODE_EN;
        } else if (!_stricmp(key, "page_size")) {
            int v = atoi(val);
            if (v >= 3 && v <= 9)
                cfg->page_size = v;
        } else if (!_stricmp(key, "font_height")) {
            int v = atoi(val);
            if (v >= 10 && v <= 40)
                cfg->font_height = v;
        } else if (!_stricmp(key, "en_suggest"))
            cfg->en_suggest = atoi(val) ? 1 : 0;
        else if (!_stricmp(key, "log"))
            cfg->log = atoi(val) ? 1 : 0;
        else if (!_stricmp(key, "log_verbose"))
            cfg->log_verbose = atoi(val) ? 1 : 0;
    }
    fclose(f);
}
