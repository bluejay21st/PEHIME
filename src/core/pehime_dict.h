/*
 * pehime_dict.h —— PEHIME 词库读取（只读、零拷贝、无动态分配）
 *
 * 词库格式 v1（小端）：
 *   u32 magic 0x4D494550（4 字节格式标识，沿用原有二进制格式）
 *   u16 version = 1
 *   u16 syllable_count
 *   u32 entry_count
 *   音节表：syllable_count × (u8 len + ASCII)
 *   词条：按音节序列字典序排列，每条：
 *     u8  syl_count
 *     u16 syl_ids[syl_count]
 *     u16 weight
 *     u8  text_len
 *     u16 text[text_len]   UTF-16LE
 *
 * 注意：词条区起点不保证 2 字节对齐；x86/x64 上非对齐 u16 读取是安全的。
 */
#ifndef PEHIME_DICT_H
#define PEHIME_DICT_H

#include <stdint.h>
#include <stddef.h>

#define PEHIME_DICT_MAGIC   0x4D494550u /* 4 字节格式标识（沿用原有二进制格式） */
#define PEHIME_DICT_VERSION 1

#define PEHIME_MAX_SYLLABLES 512
#define PEHIME_MAX_SYL_INPUT 16   /* 一次最多 16 个音节（32 个字母） */
#define PEHIME_MAX_WORD_SYL  8    /* 单个词条最多 8 个音节 */
#define PEHIME_MAX_TEXT_UNITS 8   /* 单个词条最多 8 个 UTF-16 单元 */

typedef struct {
    uint8_t         syl_count;
    const uint8_t  *syl;       /* syl_count × u16，非对齐 */
    uint16_t        weight;    /* 同音节序列内越大越靠前 */
    uint8_t         text_len;
    const uint8_t  *text;      /* text_len × u16 UTF-16LE，非对齐 */
} PehimeEntry;

typedef struct {
    const uint8_t  *base;
    uint32_t        size;
    uint16_t        syllable_count;
    uint32_t        entry_count;
    const uint8_t  *syl_name[PEHIME_MAX_SYLLABLES]; /* 指向 base，非 NUL 结尾 */
    uint8_t         syl_len[PEHIME_MAX_SYLLABLES];
    const uint8_t  *entries;   /* 词条区起点 */
} PehimeDict;

/* 遍历回调；返回非 0 提前结束 */
typedef int (*PehimeEntryFn)(const PehimeDict *d, const PehimeEntry *e, void *ctx);

int  pehime_dict_open(PehimeDict *d, const void *data, uint32_t size);
const char *pehime_dict_syl_name(const PehimeDict *d, uint16_t id, int *len);
int  pehime_dict_syl_id(const PehimeDict *d, const char *s, int len);   /* -1 = 不是合法音节 */
int  pehime_dict_syl_is_prefix(const PehimeDict *d, const char *s, int len);
int  pehime_dict_each(const PehimeDict *d, PehimeEntryFn fn, void *ctx);
uint16_t pehime_entry_syl(const PehimeEntry *e, int i);
uint16_t pehime_entry_char(const PehimeEntry *e, int i);

#endif /* PEHIME_DICT_H */

