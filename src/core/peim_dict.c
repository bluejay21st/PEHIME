#include "peim_dict.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int peim_dict_open(PeimDict *d, const void *data, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t need, i;

    if (!d || !p || size < 12)
        return -1;
    if (rd32(p) != PEIM_DICT_MAGIC)
        return -1;
    if (rd16(p + 4) != PEIM_DICT_VERSION)
        return -1;

    memset(d, 0, sizeof(*d));
    d->base = p;
    d->size = size;
    d->syllable_count = rd16(p + 6);
    d->entry_count = rd32(p + 8);

    if (d->syllable_count == 0 || d->syllable_count > PEIM_MAX_SYLLABLES)
        return -1;

    /* 音节表 */
    p += 12;
    for (i = 0; i < d->syllable_count; ++i) {
        uint8_t len;
        if ((uint32_t)(p - d->base) + 1 > size)
            return -1;
        len = *p;
        ++p;
        if (len == 0 || (uint32_t)(p - d->base) + len > size)
            return -1;
        d->syl_name[i] = p;
        d->syl_len[i] = len;
        p += len;
    }
    d->entries = p;
    need = (uint32_t)(p - d->base);
    (void)need;
    return 0;
}

const char *peim_dict_syl_name(const PeimDict *d, uint16_t id, int *len)
{
    if (!d || id >= d->syllable_count)
        return NULL;
    if (len)
        *len = d->syl_len[id];
    return (const char *)d->syl_name[id];
}

/* 音节名比较：返回 <0/0/>0（字典序，短名在前） */
static int syl_cmp(const char *name, int nlen, const char *s, int slen)
{
    int m = nlen < slen ? nlen : slen;
    int c = memcmp(name, s, (size_t)m);
    if (c)
        return c;
    if (nlen < slen)
        return -1;
    if (nlen > slen)
        return 1;
    return 0;
}

int peim_dict_syl_id(const PeimDict *d, const char *s, int len)
{
    int lo, hi;
    if (!d || len <= 0)
        return -1;
    lo = 0;
    hi = (int)d->syllable_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = syl_cmp((const char *)d->syl_name[mid], d->syl_len[mid], s, len);
        if (c == 0)
            return mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

int peim_dict_syl_is_prefix(const PeimDict *d, const char *s, int len)
{
    int lo, hi, pos;
    if (!d || len <= 0)
        return 0;
    /* 找到第一个 >= s 的音节 */
    lo = 0;
    hi = (int)d->syllable_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (syl_cmp((const char *)d->syl_name[mid], d->syl_len[mid], s, len) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    pos = lo;
    if (pos >= (int)d->syllable_count)
        return 0;
    return d->syl_len[pos] >= (uint8_t)len &&
           memcmp(d->syl_name[pos], s, (size_t)len) == 0;
}

/* 解析一条词条，返回下一条的指针；d 用于边界检查 */
static const uint8_t *entry_next(const PeimDict *d, const uint8_t *p, PeimEntry *e)
{
    uint8_t n;
    n = *p++;
    e->syl_count = n;
    e->syl = p;
    p += (size_t)n * 2;
    e->weight = rd16(p);
    p += 2;
    e->text_len = *p++;
    e->text = p;
    p += (size_t)e->text_len * 2;
    if (p > d->base + d->size)
        return NULL;
    return p;
}

int peim_dict_each(const PeimDict *d, PeimEntryFn fn, void *ctx)
{
    const uint8_t *p;
    uint32_t i;
    if (!d || !d->entries)
        return 0;
    p = d->entries;
    for (i = 0; i < d->entry_count; ++i) {
        PeimEntry e;
        const uint8_t *next;
        if (p >= d->base + d->size)
            break;
        memset(&e, 0, sizeof(e));
        next = entry_next(d, p, &e);
        if (!next)
            break;
        if (fn && fn(d, &e, ctx))
            return 1;
        p = next;
    }
    return 0;
}

/* 提供安全的非对齐 16 位读取 */
uint16_t peim_entry_syl(const PeimEntry *e, int i)
{
    return rd16(e->syl + (size_t)i * 2);
}

uint16_t peim_entry_char(const PeimEntry *e, int i)
{
    return rd16(e->text + (size_t)i * 2);
}


