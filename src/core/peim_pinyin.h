/*
 * peim_pinyin.h —— 全拼输入状态与候选生成
 */
#ifndef PEIM_PINYIN_H
#define PEIM_PINYIN_H

#include "peim_dict.h"

#define PEIM_MAX_RAW      33   /* 最多 32 个字母（+1 给结尾 0） */
#define PEIM_MAX_CAND     200

typedef struct {
    char     raw[PEIM_MAX_RAW];          /* 已输入字母（小写） */
    uint8_t  raw_len;
    uint16_t syl[PEIM_MAX_SYL_INPUT];    /* 完整音节 id */
    uint8_t  syl_end[PEIM_MAX_SYL_INPUT];/* 每个音节在 raw 中的结束位置 */
    uint8_t  nsyl;                       /* 完整音节数 */
    uint8_t  tail_pos;                   /* 未完成音节起点；== raw_len 表示无 tail */
} PeimComp;

typedef struct {
    PeimEntry e;       /* 按值保存：词库遍历时给的是栈上的结构体 */
    uint8_t  consumed; /* 该候选覆盖几个音节 */
} PeimCand;

/* 重新解析 raw。返回 0 成功（可能带未完成 tail），<0 表示不是合法拼音前缀 */
int peim_comp_parse(const PeimDict *d, PeimComp *c);

/* 生成候选：覆盖音节数降序，其次 weight 降序。返回数量 */
int peim_comp_candidates(const PeimDict *d, const PeimComp *c, PeimCand *out, int cap);

/* 上屏：把候选写入 out（UTF-16），删除 raw 中对应音节并重新解析。
 * 返回写入的 UTF-16 单元数；cap 不足返回 -1。 */
int peim_comp_commit(const PeimDict *d, PeimComp *c, const PeimCand *cand,
                     uint16_t *out, int cap);


#endif /* PEIM_PINYIN_H */



