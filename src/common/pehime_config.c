#include "pehime_config.h"

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

/* pehime.ini 不存在时生成默认配置（UTF-8 BOM + 中文注释，方便在 PE 里用记事本编辑） */
static void write_default_config(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fputs("\xEF\xBB\xBF"
          "; 是否默认启动中文：0=关闭，1=开启\n"
          "start_cn=0\n"
          "\n"
          "; 每页候选词数量：3~9\n"
          "page_size=5\n"
          "\n"
          "; 候选窗字体像素高度：10~40\n"
          "page_height=20\n"
          "\n"
          "; 是否写日志：0=关闭，1=开启\n"
          "log=0\n"
          "\n"
          "; 日志是否记录每次按键与候选列表（调试）：0=关闭，1=开启\n"
          "log_verbose=0\n", f);
    fclose(f);
}

void pehime_config_load(PehimeConfig *cfg, const char *exe_dir)
{
    char path[MAX_PATH * 2];
    FILE *f;
    char line[512];

    if (!cfg)
        return;
    cfg->start_cn = 0;
    cfg->page_size = 5;
    cfg->page_height = 20;
    cfg->log = 0;
    cfg->log_verbose = 0;

    if (!exe_dir || !exe_dir[0])
        return;

    /* 环境变量 PEHIME_INI 可覆盖路径（测试用），默认读 EXE 同目录的 pehime.ini */
    {
        const char *ini = getenv("PEHIME_INI");
        if (ini && ini[0])
            lstrcpynA(path, ini, (int)(sizeof(path) - 1));
        else
            _snprintf(path, sizeof(path) - 1, "%s\\pehime.ini", exe_dir);
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
        if (!_stricmp(key, "start_cn"))
            cfg->start_cn = atoi(val) ? 1 : 0;
        else if (!_stricmp(key, "page_size")) {
            int v = atoi(val);
            if (v >= 3 && v <= 9)
                cfg->page_size = v;
        } else if (!_stricmp(key, "page_height")) {
            int v = atoi(val);
            if (v >= 10 && v <= 40)
                cfg->page_height = v;
        } else if (!_stricmp(key, "log"))
            cfg->log = atoi(val) ? 1 : 0;
        else if (!_stricmp(key, "log_verbose"))
            cfg->log_verbose = atoi(val) ? 1 : 0;
    }
    fclose(f);
}
