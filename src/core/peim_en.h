/*
 * peim_en.h —— 英文联想词库（只读、零拷贝）
 *
 * 文件格式 v1（小端）：
 *   u32 magic 'PEEN'
 *   u16 version = 1
 *   u32 count
 *   u32 offsets[count]      相对词条区起点的偏移
 *   词条区（按词字典序升序）：
 *     u8  len
 *     u8  word[len]         ASCII 小写
 *     u16 weight            越大越靠前
 */
#ifndef PEIM_EN_H
#define PEIM_EN_H

#include <stdint.h>

#define PEIM_EN_MAX_WORD 16
#define PEIM_EN_MAX_CAND 9

typedef struct {
    const uint8_t *word;   /* 指向词库内，非 NUL 结尾 */
    uint8_t        len;
    uint16_t       weight;
} PeimEnWord;

typedef struct {
    const uint8_t *base;
    uint32_t       size;
    uint32_t       count;
    const uint8_t *entries;   /* 词条区起点 */
} PeimEnDict;

/* 返回 0 成功 */
int peim_en_open(PeimEnDict *d, const void *data, uint32_t size);

/* 前缀匹配；结果按 weight 降序（完全匹配优先），返回候选数 */
int peim_en_prefix(const PeimEnDict *d, const char *prefix, int plen,
                   PeimEnWord *out, int cap);

#endif /* PEIM_EN_H */
