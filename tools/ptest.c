/*
 * ptest.c —— 拼音核心的宿主端测试程序（不进 PE，仅开发期使用）
 *
 * 用法：
 *   ptest <词库文件> [-s N] <拼音串> [<拼音串> ...]
 *   -s N   选中第 N 个候选后，显示上屏结果与剩余输入
 *
 * 输出同时写 stdout 和 build/ptest_out.txt（UTF-8）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <windows.h>

#include "pehime_dict.h"
#include "pehime_pinyin.h"

static FILE *g_log;
static PehimeDict g_dict;

static void put_utf8(FILE *f, const uint16_t *text, int n)
{
    char buf[256];
    int i = 0;
    WCHAR wbuf[128];

    if (n > 120)
        n = 120;
    for (i = 0; i < n; ++i)
        wbuf[i] = (WCHAR)text[i];
    i = WideCharToMultiByte(CP_UTF8, 0, wbuf, n, buf, (int)sizeof(buf) - 1, NULL, NULL);
    if (i < 0)
        i = 0;
    fwrite(buf, 1, (size_t)i, f);
}

static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    if (g_log) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
    }
}

static void out_chars(const uint16_t *text, int n)
{
    put_utf8(stdout, text, n);
    if (g_log)
        put_utf8(g_log, text, n);
}

static void dump(const PehimeComp *c, const PehimeCand *cands, int n)
{
    uint16_t text[32];
    int i, page, pages;

    out("  parse:");
    for (i = 0; i < c->nsyl; ++i) {
        int len = 0;
        const char *name = pehime_dict_syl_name(&g_dict, c->syl[i], &len);
        if (i)
            out("|");
        out("%.*s", len, name);
    }
    if (c->nsyl == 0)
        out(" (none)");
    if (c->tail_pos < c->raw_len)
        out("  tail='%.*s'", (int)(c->raw_len - c->tail_pos), c->raw + c->tail_pos);
    out("  raw='%.*s'\n", (int)c->raw_len, c->raw);

    out("  candidates: %d\n", n);
    pages = (n + 4) / 5;
    for (page = 0; page < pages; ++page) {
        out("  p%d:", page + 1);
        for (i = page * 5; i < (page + 1) * 5 && i < n; ++i) {
            const PehimeEntry *e = &cands[i].e;
            int t, tl = e->text_len;
            if (tl > 30)
                tl = 30;
            for (t = 0; t < tl; ++t)
                text[t] = pehime_entry_char(e, t);
            out(" %d.", i + 1);
            out_chars(text, tl);
            out("[%d]", cands[i].consumed);
        }
        out("\n");
    }
}

int main(int argc, char **argv)
{
    FILE *fp;
    long fsize;
    uint8_t *data;
    const char *dict_path;
    int select = 0;
    int i;

    SetConsoleOutputCP(CP_UTF8);
    g_log = fopen("build/ptest_out.txt", "w");

    if (argc < 3) {
        printf("usage: ptest <dict> [-s N] <pinyin> [...]\n");
        return 2;
    }
    dict_path = argv[1];
    fp = fopen(dict_path, "rb");
    if (!fp) {
        printf("cannot open dict: %s\n", dict_path);
        return 2;
    }
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    data = (uint8_t *)malloc((size_t)fsize);
    if (!data || fread(data, 1, (size_t)fsize, fp) != (size_t)fsize) {
        printf("cannot read dict\n");
        return 2;
    }
    fclose(fp);

    if (pehime_dict_open(&g_dict, data, (uint32_t)fsize) != 0) {
        printf("bad dict format\n");
        return 2;
    }
    out("dict: %u syllables, %u entries, %ld bytes\n",
        (unsigned)g_dict.syllable_count, (unsigned)g_dict.entry_count, fsize);

    for (i = 2; i < argc; ++i) {
        PehimeComp comp;
        PehimeCand cands[PEHIME_MAX_CAND];
        int n, j;

        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            select = atoi(argv[++i]);
            continue;
        }
        memset(&comp, 0, sizeof(comp));
        for (j = 0; argv[i][j] && j < PEHIME_MAX_RAW - 1; ++j) {
            char ch = argv[i][j];
            if (ch >= 'A' && ch <= 'Z')
                ch = (char)(ch - 'A' + 'a');
            if (ch < 'a' || ch > 'z')
                break;
            comp.raw[comp.raw_len++] = ch;
        }
        out("==== %.*s\n", (int)comp.raw_len, comp.raw);
        if (pehime_comp_parse(&g_dict, &comp) < 0) {
            out("  [invalid pinyin prefix]\n");
            continue;
        }
        n = pehime_comp_candidates(&g_dict, &comp, cands, PEHIME_MAX_CAND);
        dump(&comp, cands, n);

        if (select > 0 && select <= n) {
            uint16_t text[32];
            int tl = pehime_comp_commit(&g_dict, &comp, &cands[select - 1], text, 32);
            if (tl < 0) {
                out("  [commit failed]\n");
                continue;
            }
            out("  commit -> ");
            out_chars(text, tl);
            out("  remaining raw='%.*s'\n", (int)comp.raw_len, comp.raw);
            n = pehime_comp_candidates(&g_dict, &comp, cands, PEHIME_MAX_CAND);
            if (n > 0)
                dump(&comp, cands, n);
            select = 0;
        }
    }

    free(data);
    if (g_log)
        fclose(g_log);
    return 0;
}
