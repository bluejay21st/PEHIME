/*
 * peim_dict.h —— PEIM 词库读取（只读、零拷贝、无动态分配）
 *
 * 词库格式 v1（小端）：
 *   u32 magic 'PEIM'
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
#ifndef PEIM_DICT_H
#define PEIM_DICT_H

#include <stdint.h>
#include <stddef.h>

#define PEIM_DICT_MAGIC   0x4D494550u /* 'P','E','I','M' */
#define PEIM_DICT_VERSION 1

#define PEIM_MAX_SYLLABLES 512
#define PEIM_MAX_SYL_INPUT 16   /* 一次最多 16 个音节（32 个字母） */

typedef struct {
    uint8_t         syl_count;
    const uint8_t  *syl;       /* syl_count × u16，非对齐 */
    uint16_t        weight;    /* 同音节序列内越大越靠前 */
    uint8_t         text_len;
    const uint8_t  *text;      /* text_len × u16 UTF-16LE，非对齐 */
} PeimEntry;

typedef struct {
    const uint8_t  *base;
    uint32_t        size;
    uint16_t        syllable_count;
    uint32_t        entry_count;
    const uint8_t  *syl_name[PEIM_MAX_SYLLABLES]; /* 指向 base，非 NUL 结尾 */
    uint8_t         syl_len[PEIM_MAX_SYLLABLES];
    const uint8_t  *entries;   /* 词条区起点 */
} PeimDict;

/* 遍历回调；返回非 0 提前结束 */
typedef int (*PeimEntryFn)(const PeimDict *d, const PeimEntry *e, void *ctx);

int  peim_dict_open(PeimDict *d, const void *data, uint32_t size);
const char *peim_dict_syl_name(const PeimDict *d, uint16_t id, int *len);
int  peim_dict_syl_id(const PeimDict *d, const char *s, int len);   /* -1 = 不是合法音节 */
int  peim_dict_syl_is_prefix(const PeimDict *d, const char *s, int len);
int  peim_dict_each(const PeimDict *d, PeimEntryFn fn, void *ctx);
uint16_t peim_entry_syl(const PeimEntry *e, int i);
uint16_t peim_entry_char(const PeimEntry *e, int i);

#endif /* PEIM_DICT_H */

