/*
 * hook_main.c —— PEHIME 钩子版输入法（面向 WinPE）v0.2.1
 *
 * 原理：WH_KEYBOARD_LL 全局键盘钩子拦截按键 → 组字/候选窗 → SendInput
 *       (KEYEVENTF_UNICODE) 把字符注入前台窗口。
 *
 * 中文输入：
 *  - 字母（含 Shift+字母）进入缓冲，最多 32 个字母；Shift 单击切换中/英
 *  - 拼音用"音节权重 + 动态规划"切分；候选按：整串完全匹配词 → 音节候选 → 前缀候选
 *    （zhongg → 二字词优先；zhongguod → 三字词；fangan → 方案）
 *  - 输入不是合法拼音且长度 2~4 时支持简拼（cx→程序、cxy→程序员、cpgl→磁盘管理）
 *  - 中文态标点按默认中文输入法映射（详见 Spec FR-10）；原文候选用于无法解析/首字母大写
 *  - 候选窗两行：第一行拼音 + 闪烁光标，第二行候选；▲/▼ 可点击翻页
 *
 * 英文输入：
 *  - 所有按键原样放行：不组字、不显示候选窗，输入法不出现；Shift 单击切回中文
 *
 * 界面/退出：
 *  - 默认英文态（start_cn=0），避免 PE 菜单/工具按键被吞
 *  - 有托盘时显示托盘图标；无托盘（PE）时右下角显示中/英方框
 *  - 退出：托盘/方框右键菜单"退出"（无退出快捷键）
 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#include "pehime_dict.h"
#include "pehime_pinyin.h"
#include "pehime_config.h"
#include "pehime_log.h"
#include "pehime_candwin.h"
#include "pehime_tray.h"

#define PEHIME_HOOK_VERSION L"0.2.1"
#define PEHIME_MUTEX_NAME   L"PEHIME_HOOK_SINGLE_INSTANCE"
#define WM_PEHIME_INJECT    (WM_APP + 1)
#define PEHIME_PENDING_MAX  64

typedef struct {
    int      raw;      /* 1 = 原文候选 */
    int      early;    /* 1 = 前缀候选：上屏时吃掉整个输入缓冲 */
    PehimeCand cand;     /* pinyin 候选 */
} HItem;

static HHOOK            g_hook;
static HWND             g_candwnd;
static HWND             g_msgwnd;
static int              g_cn = 0;
static int              g_shift_clean;
static int              g_quote_single;
static int              g_quote_double;
static PehimeDict         g_dict;
static int              g_dict_ok;
static PehimeConfig       g_cfg;
static char             g_dir[MAX_PATH];
static HANDLE           g_mutex;


static char             g_typed[PEHIME_MAX_RAW];
static int              g_typed_len;
static PehimeComp         g_comp;
static int              g_parse_rc;

#define PEHIME_PREFIX_MAX 32768
static HItem            g_items[PEHIME_MAX_CAND];
static HItem            g_pref[PEHIME_PREFIX_MAX];
static int              g_nitems;
static int              g_page;

static WCHAR g_pending[PEHIME_PENDING_MAX][PEHIME_MAX_RAW + 2];
static int   g_pending_len[PEHIME_PENDING_MAX];
static int   g_pending_head;
static int   g_pending_count;

static int is_low_integrity(void)
{
    HANDLE tok;
    BYTE buf[256];
    DWORD len = 0;
    DWORD rid = 0;
    int low = 0;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        pehime_log("integrity: OpenProcessToken failed %lu", (unsigned long)GetLastError());
        return 0;
    }
    if (GetTokenInformation(tok, TokenIntegrityLevel, buf, sizeof(buf), &len)) {
        TOKEN_MANDATORY_LABEL *tml = (TOKEN_MANDATORY_LABEL *)buf;
        DWORD *sub = GetSidSubAuthority(tml->Label.Sid,
                       (DWORD)(*GetSidSubAuthorityCount(tml->Label.Sid) - 1));
        if (sub)
            rid = *sub;
        pehime_log("integrity: rid=0x%lx count=%u", (unsigned long)rid,
                 (unsigned)*GetSidSubAuthorityCount(tml->Label.Sid));
    } else {
        pehime_log("integrity: GetTokenInformation failed %lu", (unsigned long)GetLastError());
    }
    CloseHandle(tok);
    low = (rid != 0 && rid <= SECURITY_MANDATORY_LOW_RID);
    return low;
}

/* ------------------------------------------------------------------ */

static void get_dir(void)
{
    char path[MAX_PATH];
    char *p;
    if (!GetModuleFileNameA(NULL, path, MAX_PATH)) {
        g_dir[0] = 0;
        return;
    }
    p = strrchr(path, '\\');
    if (p)
        *p = 0;
    lstrcpynA(g_dir, path, MAX_PATH);
}

static void load_dict(void)
{
    char path[MAX_PATH + 32];
    HANDLE h;
    DWORD size, read;
    void *mem;

    if (!g_dir[0])
        return;
    _snprintf(path, sizeof(path) - 1, "%s\\pehime.dict", g_dir);
    path[sizeof(path) - 1] = 0;
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        pehime_log("ERROR: dict not found: %s", path);
        return;
    }
    size = GetFileSize(h, NULL);
    if (size <= 12 || size > 8 * 1024 * 1024) {
        CloseHandle(h);
        pehime_log("ERROR: dict size bad: %lu", (unsigned long)size);
        return;
    }
    mem = malloc(size);
    if (mem && ReadFile(h, mem, size, &read, NULL) && read == size) {
        if (pehime_dict_open(&g_dict, mem, size) == 0) {
            g_dict_ok = 1;
            pehime_log("dict loaded: %u syllables, %u entries",
                     (unsigned)g_dict.syllable_count, (unsigned)g_dict.entry_count);
        } else {
            free(mem);
            pehime_log("ERROR: dict format bad");
        }
    } else if (mem) {
        free(mem);
    }
    CloseHandle(h);
}

static int page_size(void)
{
    int ps = g_cfg.page_size;
    return (ps >= 3 && ps <= 9) ? ps : 5;
}

static int page_count(void)
{
    int ps = page_size();
    return g_nitems > 0 ? (g_nitems + ps - 1) / ps : 0;
}

static void sync_comp(void)
{
    int i;
    memset(&g_comp, 0, sizeof(g_comp));
    if (g_typed_len > PEHIME_MAX_RAW - 1)
        g_typed_len = PEHIME_MAX_RAW - 1;
    g_comp.raw_len = (uint8_t)g_typed_len;
    for (i = 0; i < g_typed_len; ++i) {
        char c = g_typed[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        g_comp.raw[i] = c;
    }
    g_parse_rc = g_dict_ok ? pehime_comp_parse(&g_dict, &g_comp) : -1;
}

/* 词条拼音串是否以 prefix 开头（字符级） */
static int entry_prefix_match(const PehimeDict *d, const PehimeEntry *e,
                              const char *pre, int plen)
{
    int i, pos = 0;
    for (i = 0; i < e->syl_count; ++i) {
        int len = 0, k;
        const char *nm = pehime_dict_syl_name(d, pehime_entry_syl(e, i), &len);
        if (!nm)
            return 0;
        for (k = 0; k < len; ++k) {
            if (pos >= plen)
                return 1;
            if (nm[k] != pre[pos])
                return 0;
            ++pos;
        }
    }
    return pos == plen;
}

typedef struct {
    const char  *pre;
    int          plen;
    int          max_syl;       /* 前缀候选最多允许多少个音节（见 refresh_cands） */
} PrefixCtx;

static int g_pref_n;

static int prefix_cb(const PehimeDict *d, const PehimeEntry *e, void *vctx)
{
    PrefixCtx *x = (PrefixCtx *)vctx;
    if (g_pref_n >= PEHIME_PREFIX_MAX)
        return 1;
    if (x->max_syl > 0 && e->syl_count > x->max_syl)
        return 0;
    if (!entry_prefix_match(d, e, x->pre, x->plen))
        return 0;
    g_pref[g_pref_n].raw = 0;
    g_pref[g_pref_n].early = 1;
    g_pref[g_pref_n].cand.e = *e;
    g_pref[g_pref_n].cand.consumed = 0;
    g_pref_n++;
    return 0;
}

static int cmp_by_weight(const void *pa, const void *pb)
{
    const HItem *a = (const HItem *)pa;
    const HItem *b = (const HItem *)pb;
    return (int)b->cand.e.weight - (int)a->cand.e.weight;
}

/* 简拼匹配：词条每个音节的首字母依次等于输入，且音节数 == 输入长度 */
static int initials_match(const PehimeDict *d, const PehimeEntry *e, const char *abbr, int alen)
{
    int i;
    if (e->syl_count != alen)
        return 0;
    for (i = 0; i < alen; ++i) {
        int len = 0;
        const char *nm = pehime_dict_syl_name(d, pehime_entry_syl(e, i), &len);
        if (!nm || nm[0] != abbr[i])
            return 0;
    }
    return 1;
}

typedef struct {
    const char *abbr;
    int         len;
} InitialsCtx;

static int initials_cb(const PehimeDict *d, const PehimeEntry *e, void *vctx)
{
    InitialsCtx *x = (InitialsCtx *)vctx;
    if (g_pref_n >= PEHIME_PREFIX_MAX)
        return 1;
    if (!initials_match(d, e, x->abbr, x->len))
        return 0;
    g_pref[g_pref_n].raw = 0;
    g_pref[g_pref_n].early = 1;
    g_pref[g_pref_n].cand.e = *e;
    g_pref[g_pref_n].cand.consumed = 0;
    g_pref_n++;
    return 0;
}

static int all_lower_letters(const char *s, int len)
{
    int i;
    for (i = 0; i < len; ++i)
        if (s[i] < 'a' || s[i] > 'z')
            return 0;
    return 1;
}

/* 词库里存在 n/m/ng/hng 这类"语气词音节"（嗯/呣/哼），
 * 它们会让 nh 被解析成 n + h，从而挡住简拼。这里识别这类非标准音节。 */
static int is_rare_syl_name(const char *nm, int len)
{
    if (len == 1 && (nm[0] == 'm' || nm[0] == 'n'))
        return 1;
    if (len == 2 && (!memcmp(nm, "ng", 2) || !memcmp(nm, "hm", 2)))
        return 1;
    if (len == 3 && !memcmp(nm, "hng", 3))
        return 1;
    return 0;
}

static int all_syllables_rare(void)
{
    int i;
    if (g_comp.nsyl == 0)
        return 0;
    for (i = 0; i < g_comp.nsyl; ++i) {
        int len = 0;
        const char *nm = pehime_dict_syl_name(&g_dict, g_comp.syl[i], &len);
        if (!nm || !is_rare_syl_name(nm, len))
            return 0;
    }
    return 1;
}

/* 词条拼音串总长度（用于判断"整串完全匹配"） */
static int entry_pinyin_len(const PehimeDict *d, const PehimeEntry *e)
{
    int i, sum = 0;
    for (i = 0; i < e->syl_count; ++i) {
        int len = 0;
        if (!pehime_dict_syl_name(d, pehime_entry_syl(e, i), &len))
            return 0;
        sum += len;
    }
    return sum;
}

static int same_entry(const PehimeEntry *a, const PehimeEntry *b)
{
    return a->syl == b->syl && a->text == b->text && a->syl_count == b->syl_count;
}

static void add_item_raw(void)
{
    if (g_nitems >= PEHIME_MAX_CAND)
        return;
    g_items[g_nitems].raw = 1;
    g_items[g_nitems].early = 0;
    memset(&g_items[g_nitems].cand, 0, sizeof(PehimeCand));
    g_nitems++;
}

static void add_item(const HItem *it)
{
    if (g_nitems >= PEHIME_MAX_CAND)
        return;
    g_items[g_nitems] = *it;
    g_nitems++;
}

static int item_has(const PehimeEntry *e)
{
    int i;
    for (i = 0; i < g_nitems; ++i) {
        if (!g_items[i].raw && same_entry(&g_items[i].cand.e, e))
            return 1;
    }
    return 0;
}

static void refresh_cands(void)
{
    int i, np, eng, has_tail, prefix_first;
    PehimeCand pc[PEHIME_MAX_CAND];
    PrefixCtx px;

    g_nitems = 0;
    g_page = 0;
    if (g_typed_len == 0)
        return;

    sync_comp();
    if (!g_dict_ok) {
        add_item_raw();
        return;
    }

    /* 先算音节候选（简拼分支后面也会用到） */
    np = 0;
    if (g_parse_rc == 0 && g_comp.nsyl > 0)
        np = pehime_comp_candidates(&g_dict, &g_comp, pc, PEHIME_MAX_CAND);

    /* 简拼：输入不是合法拼音（ck / cxy / cpgl），
     * 或者虽然能解析但只切出 n/m/ng/hng 这类非标准音节（如 nh = n + h） */
    if ((g_parse_rc < 0 || all_syllables_rare()) &&
        g_typed_len >= 2 && g_typed_len <= 4 &&
        all_lower_letters(g_typed, g_typed_len)) {
        InitialsCtx ix;
        ix.abbr = g_comp.raw;
        ix.len = g_comp.raw_len;
        g_pref_n = 0;
        pehime_dict_each(&g_dict, initials_cb, &ix);
        if (g_pref_n > 1)
            qsort(g_pref, (size_t)g_pref_n, sizeof(HItem), cmp_by_weight);
        if (g_pref_n > 0) {
            for (i = 0; i < g_pref_n; ++i)
                add_item(&g_pref[i]);
            for (i = 0; i < np; ++i) {
                HItem it;
                it.raw = 0;
                it.early = 0;
                it.cand = pc[i];
                if (!item_has(&it.cand.e))
                    add_item(&it);
            }
            add_item_raw();   /* 原文放最后，英文仍可选 */
            return;
        }
    }

    if (g_parse_rc < 0) {
        add_item_raw();
        return;
    }

    eng = (g_typed[0] >= 'A' && g_typed[0] <= 'Z');
    if (eng)
        add_item_raw();

    /* 前缀候选的音节数限制：
     *   有未完成音节（如 zhongg）时，最多到"完整音节数+1"（可出二字词，不出三字词）
     *   没有未完成音节（如 zhongguo）时，最多到完整音节数（不出更长的词）
     * 这样 zhongg 只出二字词，zhongguod 才出三字词 */
    has_tail = (g_comp.tail_pos < g_comp.raw_len);
    memset(&px, 0, sizeof(px));
    px.pre = g_comp.raw;
    px.plen = g_comp.raw_len;
    px.max_syl = g_comp.nsyl + (has_tail ? 1 : 0);
    g_pref_n = 0;
    if (px.plen > 0)
        pehime_dict_each(&g_dict, prefix_cb, &px);
    if (g_pref_n > 1)
        qsort(g_pref, (size_t)g_pref_n, sizeof(HItem), cmp_by_weight);

    prefix_first = eng ? 1 : (np == 0 || has_tail);

    /* 整串完全匹配的词优先（如 fangan -> 方案） */
    for (i = 0; i < g_pref_n; ++i) {
        if (entry_pinyin_len(&g_dict, &g_pref[i].cand.e) == g_comp.raw_len &&
            !item_has(&g_pref[i].cand.e))
            add_item(&g_pref[i]);
    }

    if (prefix_first) {
        for (i = 0; i < g_pref_n; ++i) {
            if (entry_pinyin_len(&g_dict, &g_pref[i].cand.e) == g_comp.raw_len)
                continue;
            if (!item_has(&g_pref[i].cand.e))
                add_item(&g_pref[i]);
        }
        for (i = 0; i < np; ++i) {
            HItem it;
            it.raw = 0;
            it.early = 0;
            it.cand = pc[i];
            if (!item_has(&it.cand.e))
                add_item(&it);
        }
    } else {
        for (i = 0; i < np; ++i) {
            HItem it;
            it.raw = 0;
            it.early = 0;
            it.cand = pc[i];
            if (!item_has(&it.cand.e))
                add_item(&it);
        }
        for (i = 0; i < g_pref_n; ++i) {
            if (entry_pinyin_len(&g_dict, &g_pref[i].cand.e) == g_comp.raw_len)
                continue;
            if (!item_has(&g_pref[i].cand.e))
                add_item(&g_pref[i]);
        }
    }
}

static void clear_all(void)
{
    memset(g_typed, 0, sizeof(g_typed));
    g_typed_len = 0;
    memset(&g_comp, 0, sizeof(g_comp));
    g_parse_rc = 0;
    g_nitems = 0;
    g_page = 0;
}

/* ------------------------------------------------------------------ */
/* 注入                                                                */
/* ------------------------------------------------------------------ */

static void inject_direct(const WCHAR *text, int len)
{
    int i;
    for (i = 0; i < len; ++i) {
        INPUT in[2];
        memset(in, 0, sizeof(in));
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = 0;
        in[0].ki.wScan = (WORD)text[i];
        in[0].ki.dwFlags = KEYEVENTF_UNICODE;
        in[1] = in[0];
        in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
    }
}

static void queue_inject(const WCHAR *text, int len)
{
    int idx;
    if (!text || len <= 0)
        return;
    if (len > PEHIME_MAX_RAW)
        len = PEHIME_MAX_RAW;
    if (g_pending_count >= PEHIME_PENDING_MAX) {
        /* 正常不会满（单次按键最多上屏 32 个音节 + 1 个标点，队列 64 条）；
         * 真满了也只能丢弃，绝不能在低级钩子回调里直接 SendInput（会死锁/乱序）。 */
        pehime_log("ERROR: inject queue full, drop %d chars", len);
        return;
    }
    idx = (g_pending_head + g_pending_count) % PEHIME_PENDING_MAX;
    memcpy(g_pending[idx], text, (size_t)len * sizeof(WCHAR));
    g_pending_len[idx] = len;
    g_pending_count++;
    PostMessageW(g_msgwnd, WM_PEHIME_INJECT, 0, 0);
}

static void queue_inject_ascii_len(const char *s, int len)
{
    WCHAR buf[PEHIME_MAX_RAW + 1];
    int i, n = 0;
    for (i = 0; i < len && n < PEHIME_MAX_RAW; ++i)
        buf[n++] = (WCHAR)(unsigned char)s[i];
    queue_inject(buf, n);
}

static void flush_inject(void)
{
    while (g_pending_count > 0) {
        inject_direct(g_pending[g_pending_head], g_pending_len[g_pending_head]);
        g_pending_head = (g_pending_head + 1) % PEHIME_PENDING_MAX;
        g_pending_count--;
    }
}

/* ------------------------------------------------------------------ */
/* 候选窗                                                              */
/* ------------------------------------------------------------------ */

/* 候选窗锚点：能拿到 Win32 插入符就跟着光标；拿不到就跟随鼠标 */
static void get_anchor(POINT *pt)
{
    GUITHREADINFO gti;
    POINT c;

    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndCaret) {
        c.x = gti.rcCaret.left;
        c.y = gti.rcCaret.bottom;
        if (ClientToScreen(gti.hwndCaret, &c)) {
            c.y += 6;
            *pt = c;
            return;
        }
    }
    if (!GetCursorPos(pt)) {
        pt->x = 100;
        pt->y = 100;
    }
    pt->y += 16;   /* 跟随鼠标：放在指针下方一点 */
}

static void update_window_at(const POINT *anchor)
{
    PehimeCandView v;
    POINT pt;
    int ps, start, n, i, k;

    if (!g_candwnd)
        return;
    if (g_typed_len == 0 && g_nitems == 0) {
        pehime_cand_hide(g_candwnd);
        return;
    }
    if (anchor)
        pt = *anchor;
    else
        get_anchor(&pt);

    memset(&v, 0, sizeof(v));
    v.mode = g_cn;
    v.font_height = g_cfg.page_height;
    v.notify = g_msgwnd;
    memcpy(v.pinyin, g_typed, g_typed_len);
    v.pinyin[g_typed_len] = 0;
    v.page = g_page;
    v.pages = page_count();

    ps = page_size();
    start = g_page * ps;
    n = g_nitems - start;
    if (n < 0)
        n = 0;
    if (n > ps)
        n = ps;
    if (n > PEHIME_CAND_MAX_ITEMS)
        n = PEHIME_CAND_MAX_ITEMS;
    v.ncand = n;
    for (i = 0; i < n; ++i) {
        HItem *it = &g_items[start + i];
        if (it->raw) {
            for (k = 0; k < g_typed_len && k < PEHIME_CAND_MAX_CHARS; ++k)
                v.cands[i][k] = (WCHAR)(unsigned char)g_typed[k];
            v.cands[i][k] = 0;
        } else {
            const PehimeEntry *e = &it->cand.e;
            for (k = 0; k < e->text_len && k < PEHIME_CAND_MAX_CHARS; ++k)
                v.cands[i][k] = (WCHAR)pehime_entry_char(e, k);
            v.cands[i][k] = 0;
        }
    }
    if (g_cfg.log_verbose) {
        char line[256];
        int off = 0, j;
        off += _snprintf(line + off, sizeof(line) - off,
                         "cands raw='%.*s' rc=%d nsyl=%d ->",
                         g_typed_len, g_typed, g_parse_rc, (int)g_comp.nsyl);
        for (j = 0; j < n && off < (int)sizeof(line) - 32; ++j) {
            char mb[64];
            WideCharToMultiByte(CP_UTF8, 0, v.cands[j], -1, mb, sizeof(mb), NULL, NULL);
            off += _snprintf(line + off, sizeof(line) - off, " %s", mb);
        }
        pehime_log("%s", line);
    }
    pehime_cand_show(g_candwnd, &v, pt);
}

static void update_window(void)
{
    update_window_at(NULL);
}

/* 翻页时保持候选窗当前位置，不能因为鼠标在箭头上就跟着鼠标跑 */
static void update_window_keep_pos(void)
{
    RECT r;
    POINT pt;

    if (!g_candwnd) {
        update_window();
        return;
    }
    GetWindowRect(g_candwnd, &r);
    pt.x = r.left;
    pt.y = r.top;
    update_window_at(&pt);
}

static void hide_window(void)
{
    if (g_candwnd)
        pehime_cand_hide(g_candwnd);
}

static void flash_mode(void)
{
    POINT pt;
    if (!g_candwnd)
        return;
    get_anchor(&pt);
    pehime_cand_flash(g_candwnd, g_cn, g_cfg.page_height, pt);
}

/* ------------------------------------------------------------------ */
/* 上屏                                                                */
/* ------------------------------------------------------------------ */

static void commit_raw_typed(void)
{
    if (g_typed_len == 0)
        return;
    queue_inject_ascii_len(g_typed, g_typed_len);
    pehime_log("commit raw '%.*s'", g_typed_len, g_typed);
    clear_all();
    hide_window();
}

static void select_item(int idx)
{
    HItem *it;
    uint16_t text[PEHIME_CAND_MAX_CHARS];
    int n, i, end = 0;

    if (idx < 0 || idx >= g_nitems)
        return;
    it = &g_items[idx];

    if (it->raw) {
        commit_raw_typed();
        return;
    }
    if (it->early) {
        WCHAR out[PEHIME_CAND_MAX_CHARS];
        n = it->cand.e.text_len;
        if (n > PEHIME_CAND_MAX_CHARS)
            n = PEHIME_CAND_MAX_CHARS;
        for (i = 0; i < n; ++i)
            out[i] = (WCHAR)pehime_entry_char(&it->cand.e, i);
        queue_inject(out, n);
        pehime_log("select[%d] early -> %d chars", idx, n);
        clear_all();
        hide_window();
        return;
    }

    if (it->cand.consumed > 0)
        end = (int)g_comp.syl_end[it->cand.consumed - 1];
    n = pehime_comp_commit(&g_dict, &g_comp, &it->cand, text, PEHIME_CAND_MAX_CHARS);
    if (n < 0)
        return;
    {
        WCHAR out[PEHIME_CAND_MAX_CHARS];
        for (i = 0; i < n; ++i)
            out[i] = (WCHAR)text[i];
        queue_inject(out, n);
    }
    pehime_log("select[%d] -> %d chars, consumed=%d, raw_end=%d",
             idx, n, (int)it->cand.consumed, end);
    if (end > 0 && end <= g_typed_len) {
        memmove(g_typed, g_typed + end, (size_t)(g_typed_len - end));
        g_typed_len -= end;
    } else {
        g_typed_len = 0;
    }
    if (g_typed_len > 0) {
        refresh_cands();
        update_window();
    } else {
        clear_all();
        hide_window();
    }
}

/* 中文标点前把当前组字全部上屏，避免标点后残留隐藏的拼音缓冲 */
static void commit_composition_for_punct(void)
{
    int guard = 64;

    while (guard-- > 0 && g_typed_len > 0 && g_nitems > 0) {
        int idx = g_page * page_size();
        if (idx >= g_nitems)
            idx = 0;
        select_item(idx);
    }
    if (g_typed_len > 0)
        commit_raw_typed();
    clear_all();
    hide_window();
}
static void backspace_one(void)
{
    if (g_typed_len <= 0)
        return;
    g_typed_len--;
    g_typed[g_typed_len] = 0;
    if (g_typed_len == 0) {
        clear_all();
        hide_window();
    } else {
        refresh_cands();
        update_window();
    }
}

/* ------------------------------------------------------------------ */
/* 按键处理                                                            */
/* ------------------------------------------------------------------ */

static void toggle_mode(void)
{
    g_cn = !g_cn;
    g_shift_clean = 0;   /* 托盘/方框切换后，松开 Shift 不再触发第二次切换 */
    g_quote_single = 0;
    g_quote_double = 0;
    pehime_log("toggle -> %s", g_cn ? "CN" : "EN");
    clear_all();
    pehime_tray_set_mode(g_cn);
    flash_mode();
}

static int kv_down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static int is_letter_vk(UINT vk)
{
    return vk >= 'A' && vk <= 'Z';
}

/* 中文模式固定标点映射：返回字符串；NULL=英文原样放行 */
static const WCHAR *cn_punct_for_key(UINT vk, int shift, int *out_len)
{
    *out_len = 1;
    switch (vk) {
    case '1':
        if (shift) return L"\xFF01"; /* ！ */
        break;
    case '4':
        if (shift) return L"\xFFE5"; /* ￥ */
        break;
    case '6':
        if (shift) { *out_len = 2; return L"\x2026\x2026"; } /* …… */
        break;
    case '9':
        if (shift) return L"\xFF08"; /* （ */
        break;
    case '0':
        if (shift) return L"\xFF09"; /* ） */
        break;
    case VK_OEM_COMMA:  return shift ? L"\x300A" : L"\xFF0C"; /* 《 ， */
    case VK_OEM_PERIOD: return shift ? L"\x300B" : L"\x3002"; /* 》 。 */
    case VK_OEM_1:      return shift ? L"\xFF1A" : L"\xFF1B"; /* ： ； */
    case VK_OEM_2:
        if (shift) return L"\xFF1F"; /* ？ */
        break;
    case VK_OEM_3:
        if (!shift) return L"\x00B7"; /* · */
        break;
    case VK_OEM_4:
        if (!shift) return L"\x3010"; /* 【 */
        break;
    case VK_OEM_5:
        if (!shift) return L"\x3001"; /* 、 */
        break;
    case VK_OEM_6:
        if (!shift) return L"\x3011"; /* 】 */
        break;
    case VK_OEM_MINUS:
        if (g_nitems > 0)
            break;
        if (shift) { *out_len = 2; return L"\x2014\x2014"; } /* —— */
        break;
    case VK_OEM_7:
        if (shift) {
            const WCHAR *q = g_quote_double ? L"\x201D" : L"\x201C";
            g_quote_double = !g_quote_double;
            return q;
        } else {
            const WCHAR *q = g_quote_single ? L"\x2019" : L"\x2018";
            g_quote_single = !g_quote_single;
            return q;
        }
    default:
        break;
    }
    *out_len = 0;
    return NULL;
}
static int handle_key(UINT vk, int down)
{
    int shift = kv_down(VK_SHIFT);

    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) {
        if (down) {
            g_shift_clean = 1;
            return 0;
        }
        /* 只处理"干净"的单击 Shift：按住 Ctrl/Alt/Win 时释放 Shift 不切模式 */
        if (g_shift_clean &&
            !kv_down(VK_CONTROL) && !kv_down(VK_MENU) &&
            !kv_down(VK_LWIN) && !kv_down(VK_RWIN))
            toggle_mode();
        g_shift_clean = 0;
        return 0;
    }
    if (down)
        g_shift_clean = 0;

    if (!down)
        return 0;


    if (kv_down(VK_CONTROL) || kv_down(VK_MENU) || kv_down(VK_LWIN) || kv_down(VK_RWIN))
        return 0;
    if (!g_cn)
        return 0;   /* 英文态：所有键原样放行，不组字、不显示候选窗 */
    {
        int plen = 0;
        const WCHAR *punct = cn_punct_for_key(vk, shift, &plen);
        if (punct && plen > 0) {
            if (g_typed_len > 0)
                commit_composition_for_punct();
            queue_inject(punct, plen);
            hide_window();
            if (g_cfg.log_verbose)
                pehime_log("punct len=%d first=U+%04X", plen, (unsigned)punct[0]);
            return 1;
        }
    }

    if (is_letter_vk(vk)) {
        char ch;
        if (g_typed_len >= PEHIME_MAX_RAW - 1)
            return 1;
        ch = shift ? (char)vk : (char)(vk - 'A' + 'a');
        g_typed[g_typed_len++] = ch;
        g_typed[g_typed_len] = 0;
        refresh_cands();
        update_window();
        if (g_cfg.log_verbose)
            pehime_log("letter %c raw='%.*s' items=%d", ch, g_typed_len, g_typed, g_nitems);
        return 1;
    }

    if (!g_dict_ok && g_typed_len == 0)
        return 0;

    if (vk >= '1' && vk <= '9') {
        int idx = g_page * page_size() + (int)(vk - '1');
        if (idx < g_nitems) {
            select_item(idx);
            return 1;
        }
        return 0;
    }

    if (vk == VK_SPACE) {
        if (g_nitems > 0) {
            select_item(g_page * page_size());
            return 1;
        }
        if (g_typed_len > 0) {
            commit_raw_typed();
            return 1;
        }
        return 0;
    }

    if (vk == VK_BACK) {
        if (g_typed_len == 0) {
            hide_window();
            return 0;
        }
        backspace_one();
        return 1;
    }

    if (vk == VK_ESCAPE) {
        if (g_typed_len == 0 && g_nitems == 0)
            return 0;
        clear_all();
        hide_window();
        return 1;
    }

    if (vk == VK_RETURN) {
        if (g_typed_len == 0)
            return 0;
        commit_raw_typed();
        return 1;
    }

    if (vk == VK_OEM_MINUS || vk == VK_PRIOR) {
        if (g_nitems == 0)
            return 0;
        if (g_page > 0) {
            g_page--;
            update_window_keep_pos();
        }
        return 1;
    }
    if (vk == VK_OEM_PLUS || vk == VK_NEXT) {
        if (g_nitems == 0)
            return 0;
        if ((g_page + 1) * page_size() < g_nitems) {
            g_page++;
            update_window_keep_pos();
        }
        return 1;
    }

    return 0;
}

static LRESULT CALLBACK ll_proc(int code, WPARAM wparam, LPARAM lparam)
{
    KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lparam;
    int down;

    if (code != HC_ACTION)
        return CallNextHookEx(g_hook, code, wparam, lparam);
    if (!k)
        return CallNextHookEx(g_hook, code, wparam, lparam);

    down = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN);
    if (handle_key(k->vkCode, down))
        return 1;
    return CallNextHookEx(g_hook, code, wparam, lparam);
}


/* ------------------------------------------------------------------ */

static int wstr_has(const WCHAR *s, const WCHAR *sub)
{
    return s && sub && wcsstr(s, sub) != NULL;
}

static LRESULT CALLBACK msg_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wnd; (void)lp;
    if (msg == WM_PEHIME_INJECT) {
        flush_inject();
        return 0;
    }
    if (msg == PEHIME_TRAY_MSG_TOGGLE) {
        toggle_mode();
        return 0;
    }
    if (msg == PEHIME_TRAY_MSG_QUIT) {
        pehime_log("quit from tray");
        PostQuitMessage(0);
        return 0;
    }
    if (msg == PEHIME_CAND_MSG_PAGE) {
        int ps = page_size();
        if ((int)wp < 0) {
            if (g_page > 0)
                g_page--;
        } else {
            if ((g_page + 1) * ps < g_nitems)
                g_page++;
        }
        update_window_keep_pos();
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* 管理员检测 / 提权                                                   */
/* ------------------------------------------------------------------ */

static int is_privileged(void)
{
    BOOL admin = FALSE;
    SID_IDENTIFIER_AUTHORITY nt = { SECURITY_NT_AUTHORITY };
    PSID sid = NULL;
    HANDLE tok = NULL;
    TOKEN_ELEVATION te;
    DWORD len = 0;

    if (AllocateAndInitializeSid(&nt, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &sid)) {
        CheckTokenMembership(NULL, sid, &admin);
        FreeSid(sid);
    }
    if (admin)
        return 1;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        memset(&te, 0, sizeof(te));
        if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &len) &&
            te.TokenIsElevated)
            admin = TRUE;
        CloseHandle(tok);
    }
    return admin ? 1 : 0;
}

static const WCHAR *cmdline_after_exe(const WCHAR *cmd)
{
    const WCHAR *p = cmd;
    if (!p)
        return L"";
    while (*p == L' ' || *p == L'\t')
        ++p;
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"')
            ++p;
        if (*p == L'"')
            ++p;
    } else {
        while (*p && *p != L' ' && *p != L'\t')
            ++p;
    }
    while (*p == L' ' || *p == L'\t')
        ++p;
    return p;
}

/* 0=已启动提权副本；否则返回 Win32 错误码 */
static DWORD relaunch_as_admin(void)
{
    WCHAR exe[2 * MAX_PATH];
    const WCHAR *args;
    WCHAR *params;
    size_t n;
    SHELLEXECUTEINFOW sei;
    DWORD rc;

    if (!GetModuleFileNameW(NULL, exe, (DWORD)(sizeof(exe) / sizeof(exe[0]))))
        return GetLastError();

    args = cmdline_after_exe(GetCommandLineW());
    n = (args && *args) ? wcslen(args) : 0;
    params = (WCHAR *)malloc((n + 32) * sizeof(WCHAR));
    if (!params)
        return ERROR_NOT_ENOUGH_MEMORY;
    params[0] = 0;
    if (args && *args) {
        wcscpy(params, args);
        if (!wcsstr(params, L"--elevated"))
            wcscat(params, L" --elevated");
    } else {
        wcscpy(params, L"--elevated");
    }

    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.lpParameters = params;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        rc = GetLastError();
        pehime_log("elevate: ShellExecuteEx failed %lu", (unsigned long)rc);
        free(params);
        return rc;
    }
    if (sei.hProcess)
        CloseHandle(sei.hProcess);
    pehime_log("elevate: launched elevated instance");
    free(params);
    return 0;
}
int WINAPI WinMain(HINSTANCE hinst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSEXW wc;
    HWND msgwnd;
    MSG msg;

    (void)prev; (void)cmd; (void)show;

    get_dir();
    pehime_config_load(&g_cfg, g_dir);
    if (g_cfg.log)
        pehime_log_open(g_dir);
    g_cn = g_cfg.start_cn ? 1 : 0;
    pehime_log("PEHIME start, dir=%s, start_cn=%d, ver=%ls", g_dir, g_cn, PEHIME_HOOK_VERSION);

    /* 测试模式：不弹窗、不提权、不装钩子；完成自检后直接退出 */
    if (wstr_has(GetCommandLineW(), L"--selftest")) {
        HWND cw;
        load_dict();
        cw = pehime_cand_create(hinst);
        pehime_log("SELFTEST: dict_ok=%d candwnd=%p version=%ls",
                 g_dict_ok, (void *)cw, PEHIME_HOOK_VERSION);
        if (cw)
            pehime_cand_destroy(cw);
        pehime_log("SELFTEST: %s", g_dict_ok ? "OK" : "FAILED (dict missing/bad)");
        pehime_log_close();
        return g_dict_ok ? 0 : 3;
    }

    /* 正常模式：必须是管理员（已提权）；否则主动 UAC 提权后退出本进程 */
    if (!is_privileged()) {
        DWORD er;
        if (wstr_has(GetCommandLineW(), L"--elevated")) {
            pehime_log("ERROR: elevation requested but still not privileged");
            MessageBoxW(NULL,
                        L"PEHIME 需要管理员权限才能运行。\n请右键选择“以管理员身份运行”。",
                        L"PEHIME 输入法", MB_ICONERROR | MB_OK);
            pehime_log_close();
            return 5;
        }
        er = relaunch_as_admin();
        if (er == 0) {
            pehime_log("elevate: parent exits");
            pehime_log_close();
            return 0;
        }
        pehime_log("ERROR: elevation failed: %lu", (unsigned long)er);
        MessageBoxW(NULL,
                    er == ERROR_CANCELLED
                        ? L"需要管理员权限才能使用 PEHIME；已取消提权。"
                        : L"无法获得管理员权限，PEHIME 无法运行。",
                    L"PEHIME 输入法", MB_ICONERROR | MB_OK);
        pehime_log_close();
        return 5;
    }

    /* 单实例：管理员进程之间用命名互斥体 */
    g_mutex = CreateMutexW(NULL, TRUE, PEHIME_MUTEX_NAME);
    if (!g_mutex) {
        if (GetLastError() == ERROR_ACCESS_DENIED) {
            pehime_log("another instance (access denied), exit");
            pehime_log_close();
            return 0;
        }
        pehime_log("ERROR: CreateMutex failed %lu", (unsigned long)GetLastError());
        pehime_log_close();
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        pehime_log("another instance, exit");
        pehime_log_close();
        return 0;
    }

    /* 已提权后通常不会低完整性；保留兜底提示 */
    if (is_low_integrity()) {
        pehime_log("ERROR: running at low integrity, cannot inject into normal apps");
        MessageBoxW(NULL,
                    L"PEHIME 当前以低权限运行，无法向普通程序输入文字。\n"
                    L"请以管理员身份重新启动 PEHIME.EXE。",
                    L"PEHIME 输入法", MB_ICONERROR | MB_OK);
        pehime_log_close();
        return 4;
    }

    load_dict();

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = msg_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = L"PEHIMEHookMsgWnd";
    RegisterClassExW(&wc);
    msgwnd = CreateWindowExW(0, L"PEHIMEHookMsgWnd", L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, hinst, NULL);
    g_msgwnd = msgwnd;
    if (!msgwnd) {
        pehime_log("ERROR: CreateWindow failed %lu", (unsigned long)GetLastError());
        pehime_log_close();
        return 1;
    }

    g_candwnd = pehime_cand_create(hinst);
    pehime_log("candwnd=%p dict_ok=%d", (void *)g_candwnd, g_dict_ok);

    {
        int ui = pehime_tray_init(hinst, msgwnd);
        pehime_tray_set_mode(g_cn);
        pehime_log("UI: %s, mode=%s",
                 ui == 1 ? "tray icon" : ui == 0 ? "corner indicator" : "FAILED",
                 g_cn ? "CN" : "EN");
    }

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, ll_proc, hinst, 0);
    if (!g_hook) {
        pehime_log("ERROR: SetWindowsHookEx failed %lu", (unsigned long)GetLastError());
        pehime_log_close();
        return 1;
    }
    pehime_log("hook installed");

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }

    pehime_tray_destroy();
    pehime_cand_destroy(g_candwnd);
    pehime_log("PEHIME exit");
    pehime_log_close();
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
    }
    return 0;
}