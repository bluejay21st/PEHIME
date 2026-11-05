#include "pehime_pinyin.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* 拼音切分：加权 DP，避免"最长优先"造成 ming|o|ng 这类错误切分        */
/* ------------------------------------------------------------------ */

#define PEHIME_MAX_SYL_LEN 6
#define DP_POS (PEHIME_MAX_RAW + 1)
#define DP_SYL (PEHIME_MAX_SYL_INPUT + 1)

/* 每个音节的分值 = 该音节下单字候选的最大权重（近似该音节常用度） */
static uint16_t       g_sylw[2048];
static const PehimeDict *g_sylw_dict;

static int sylw_cb(const PehimeDict *d, const PehimeEntry *e, void *ctx)
{
    uint16_t id;
    (void)d;
    (void)ctx;
    if (e->syl_count != 1)
        return 0;
    id = pehime_entry_syl(e, 0);
    if (id < 2048 && e->weight > g_sylw[id])
        g_sylw[id] = e->weight;
    return 0;
}

static void sylw_init(const PehimeDict *d)
{
    uint16_t i;
    if (g_sylw_dict == d)
        return;
    memset(g_sylw, 0, sizeof(g_sylw));
    pehime_dict_each(d, sylw_cb, NULL);
    for (i = 0; i < d->syllable_count && i < 2048; ++i)
        if (g_sylw[i] == 0)
            g_sylw[i] = 1;
    g_sylw_dict = d;
}

int pehime_comp_parse(const PehimeDict *d, PehimeComp *c)
{
    static uint32_t dp_sum[DP_POS][DP_SYL];
    static uint8_t  dp_ok[DP_POS][DP_SYL];
    static uint8_t  dp_len[DP_POS][DP_SYL];
    static uint16_t dp_syl[DP_POS][DP_SYL];
    int n, pos, len, k, cut, best_cut = -1, best_k = -1;
    uint64_t best_num = 0, best_den = 1;

    if (!d || !c)
        return -1;
    if (c->raw_len >= PEHIME_MAX_RAW)
        c->raw_len = PEHIME_MAX_RAW - 1;
    c->raw[c->raw_len] = 0;
    c->nsyl = 0;
    c->tail_pos = c->raw_len;
    n = (int)c->raw_len;
    if (n == 0)
        return 0;

    sylw_init(d);
    memset(dp_sum, 0, sizeof(dp_sum));
    memset(dp_ok, 0, sizeof(dp_ok));
    memset(dp_len, 0, sizeof(dp_len));
    memset(dp_syl, 0, sizeof(dp_syl));
    dp_ok[0][0] = 1;

    /* 正向 DP：dp[pos][k] = 用 k 个完整音节覆盖 raw[0..pos) 的最大总分 */
    for (pos = 1; pos <= n; ++pos) {
        for (len = 1; len <= PEHIME_MAX_SYL_LEN && len <= pos; ++len) {
            int id = pehime_dict_syl_id(d, c->raw + pos - len, len);
            uint32_t w;
            if (id < 0)
                continue;
            w = g_sylw[id] ? g_sylw[id] : 1;
            for (k = 1; k <= PEHIME_MAX_SYL_INPUT; ++k) {
                uint32_t sum;
                if (!dp_ok[pos - len][k - 1])
                    continue;
                sum = dp_sum[pos - len][k - 1] + w;
                if (!dp_ok[pos][k] || sum > dp_sum[pos][k]) {
                    dp_ok[pos][k] = 1;
                    dp_sum[pos][k] = sum;
                    dp_len[pos][k] = (uint8_t)len;
                    dp_syl[pos][k] = (uint16_t)id;
                }
            }
        }
    }

    /* 选择切分：先保证覆盖尽可能多的字母（整串能切完就整串切完），
     * 在同一个切分终点里再选"平均音节权重"最高的那一种。 */
    {
        int have_full = 0;
        for (k = 0; k <= PEHIME_MAX_SYL_INPUT; ++k)
            if (dp_ok[n][k])
                have_full = 1;

        if (have_full) {
            for (k = 0; k <= PEHIME_MAX_SYL_INPUT; ++k) {
                uint64_t num, den;
                if (!dp_ok[n][k])
                    continue;
                num = dp_sum[n][k];
                den = (k > 0) ? (uint64_t)k : 1;
                /* 先少音节（=优先长音节，符合拼音习惯），再比平均权重 */
                if (best_cut < 0 || k < best_k ||
                    (k == best_k && num * best_den > best_num * den)) {
                    best_cut = n;
                    best_k = k;
                    best_num = num;
                    best_den = den;
                }
            }
        } else {
            /* 整串切不完：从后往前找最长的完整前缀，剩余部分作为未完成音节 */
            for (cut = n - 1; cut >= 0 && best_cut < 0; --cut) {
                if (!pehime_dict_syl_is_prefix(d, c->raw + cut, n - cut))
                    continue;
                for (k = 0; k <= PEHIME_MAX_SYL_INPUT; ++k) {
                    uint64_t num, den;
                    if (!dp_ok[cut][k])
                        continue;
                    num = dp_sum[cut][k];
                    den = (k > 0) ? (uint64_t)k : 1;
                    if (best_cut < 0 || cut > best_cut || k < best_k ||
                        (cut == best_cut && k == best_k &&
                         num * best_den > best_num * den)) {
                        best_cut = cut;
                        best_k = k;
                        best_num = num;
                        best_den = den;
                    }
                }
            }
        }
    }

    if (best_cut < 0) {
        c->nsyl = 0;
        c->tail_pos = (uint8_t)c->raw_len;
        return -1;
    }

    pos = best_cut;
    k = best_k;
    while (k > 0 && pos > 0) {
        len = dp_len[pos][k];
        c->syl[k - 1] = dp_syl[pos][k];
        c->syl_end[k - 1] = (uint8_t)pos;
        pos -= len;
        --k;
    }
    c->nsyl = (uint8_t)best_k;
    c->tail_pos = (uint8_t)best_cut;
    return 0;
}
typedef struct {
    const PehimeComp *c;
    PehimeCand       *out;
    int             cap;
    int             n;
} CandCtx;

static int cand_cb(const PehimeDict *d, const PehimeEntry *e, void *vctx)
{
    CandCtx *x = (CandCtx *)vctx;
    int i, k;

    (void)d;
    if (e->syl_count == 0 || e->syl_count > x->c->nsyl)
        return 0;
    k = e->syl_count;
    for (i = 0; i < k; ++i) {
        if (pehime_entry_syl(e, i) != x->c->syl[i])
            return 0;
    }
    if (x->n < x->cap) {
        x->out[x->n].e = *e;
        x->out[x->n].consumed = (uint8_t)k;
        x->n++;
    }
    return 0;
}

int pehime_comp_candidates(const PehimeDict *d, const PehimeComp *c, PehimeCand *out, int cap)
{
    CandCtx x;
    int i, j;

    if (!d || !c || !out || cap <= 0 || c->nsyl == 0)
        return 0;

    x.c = c;
    x.out = out;
    x.cap = cap;
    x.n = 0;
    pehime_dict_each(d, cand_cb, &x);

    /* 覆盖音节多者优先；同覆盖长度按 weight 降序 */
    for (i = 1; i < x.n; ++i) {
        PehimeCand key = x.out[i];
        j = i - 1;
        while (j >= 0) {
            const PehimeCand *cur = &x.out[j];
            int better = 0;
            if (key.consumed > cur->consumed)
                better = 1;
            else if (key.consumed == cur->consumed && key.e.weight > cur->e.weight)
                better = 1;
            if (!better)
                break;
            x.out[j + 1] = x.out[j];
            --j;
        }
        x.out[j + 1] = key;
    }
    return x.n;
}

int pehime_comp_commit(const PehimeDict *d, PehimeComp *c, const PehimeCand *cand,
                     uint16_t *out, int cap)
{
    int i, n, end;

    if (!d || !c || !cand || !out)
        return -1;
    n = cand->e.text_len;
    if (n > cap)
        return -1;
    for (i = 0; i < n; ++i)
        out[i] = pehime_entry_char(&cand->e, i);

    end = (int)c->syl_end[cand->consumed - 1];
    if (end > (int)c->raw_len)
        return -1;
    memmove(c->raw, c->raw + end, (size_t)(c->raw_len - end));
    c->raw_len = (uint8_t)(c->raw_len - end);
    pehime_comp_parse(d, c);
    return n;
}






