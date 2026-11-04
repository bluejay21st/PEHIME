#include "peim_en.h"

#include <stdlib.h>
#include <string.h>

#define PEIM_EN_MAGIC 0x4E454550u   /* 'P','E','E','N' */
#define PEIM_EN_QUERY_MAX 4096      /* 一次前缀查询最多收集的匹配数 */

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int peim_en_open(PeimEnDict *d, const void *data, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t count;

    if (!d || !p || size < 10)
        return -1;
    if (rd32(p) != PEIM_EN_MAGIC)
        return -1;
    if (rd16(p + 4) != 1)
        return -1;
    count = rd32(p + 6);
    if (count == 0 || count > 200000)
        return -1;
    if ((uint64_t)10 + (uint64_t)count * 4 > size)
        return -1;

    memset(d, 0, sizeof(*d));
    d->base = p;
    d->size = size;
    d->count = count;
    d->entries = p + 10 + count * 4;
    return 0;
}

/* 取第 idx 个词条；越界/损坏返回 -1 */
static int get_word(const PeimEnDict *d, uint32_t idx,
                    const uint8_t **word, int *len, uint16_t *weight)
{
    uint32_t off;
    const uint8_t *p;

    if (idx >= d->count)
        return -1;
    off = rd32(d->base + 10 + (size_t)idx * 4);
    p = d->entries + off;
    if (p + 3 > d->base + d->size)
        return -1;
    *len = p[0];
    if (*len < 1 || *len > PEIM_EN_MAX_WORD)
        return -1;
    if (p + 1 + *len + 2 > d->base + d->size)
        return -1;
    *word = p + 1;
    *weight = rd16(p + 1 + *len);
    return 0;
}

/* 比较词与前缀：<0 词在前缀之前，0 词以该前缀开头，>0 词在前缀之后 */
static int cmp_word_prefix(const uint8_t *w, int wl, const char *pre, int plen)
{
    int m = wl < plen ? wl : plen;
    int c = memcmp(w, pre, (size_t)m);
    if (c)
        return c;
    if (wl < plen)
        return -1;
    return 0;
}

/* 短词优先：长度升序；同长度按词频降序 */
static int cmp_short_first(const void *a, const void *b)
{
    const PeimEnWord *x = (const PeimEnWord *)a;
    const PeimEnWord *y = (const PeimEnWord *)b;
    if (x->len != y->len)
        return (int)x->len - (int)y->len;
    return (int)y->weight - (int)x->weight;
}

int peim_en_prefix(const PeimEnDict *d, const char *prefix, int plen,
                   PeimEnWord *out, int cap)
{
    static PeimEnWord buf[PEIM_EN_QUERY_MAX];
    uint32_t lo = 0, hi, i;
    int n = 0;

    if (!d || !prefix || plen <= 0 || !out || cap <= 0 || d->count == 0)
        return 0;

    hi = d->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const uint8_t *w;
        int wl;
        uint16_t weight;
        if (get_word(d, mid, &w, &wl, &weight) != 0)
            break;
        if (cmp_word_prefix(w, wl, prefix, plen) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }

    for (i = lo; i < d->count && n < PEIM_EN_QUERY_MAX; ++i) {
        const uint8_t *w;
        int wl;
        uint16_t weight;
        if (get_word(d, i, &w, &wl, &weight) != 0)
            break;
        if (cmp_word_prefix(w, wl, prefix, plen) != 0)
            break;
        buf[n].word = w;
        buf[n].len = (uint8_t)wl;
        buf[n].weight = weight;
        ++n;
    }
    if (n == 0)
        return 0;

    qsort(buf, (size_t)n, sizeof(PeimEnWord), cmp_short_first);


    if (n > cap)
        n = cap;
    memcpy(out, buf, (size_t)n * sizeof(PeimEnWord));
    return n;
}


