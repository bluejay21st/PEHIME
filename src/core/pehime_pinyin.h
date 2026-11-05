/*
 * pehime_pinyin.h —— 全拼输入状态与候选生成
 */
#ifndef PEHIME_PINYIN_H
#define PEHIME_PINYIN_H

#include "pehime_dict.h"

#define PEHIME_MAX_RAW      33   /* 最多 32 个字母（+1 给结尾 0） */
#define PEHIME_MAX_CAND     200

typedef struct {
    char     raw[PEHIME_MAX_RAW];          /* 已输入字母（小写） */
    uint8_t  raw_len;
    uint16_t syl[PEHIME_MAX_SYL_INPUT];    /* 完整音节 id */
    uint8_t  syl_end[PEHIME_MAX_SYL_INPUT];/* 每个音节在 raw 中的结束位置 */
    uint8_t  nsyl;                       /* 完整音节数 */
    uint8_t  tail_pos;                   /* 未完成音节起点；== raw_len 表示无 tail */
} PehimeComp;

typedef struct {
    PehimeEntry e;       /* 按值保存：词库遍历时给的是栈上的结构体 */
    uint8_t  consumed; /* 该候选覆盖几个音节 */
} PehimeCand;

/* 重新解析 raw。返回 0 成功（可能带未完成 tail），<0 表示不是合法拼音前缀 */
int pehime_comp_parse(const PehimeDict *d, PehimeComp *c);

/* 生成候选：覆盖音节数降序，其次 weight 降序。返回数量 */
int pehime_comp_candidates(const PehimeDict *d, const PehimeComp *c, PehimeCand *out, int cap);

/* 上屏：把候选写入 out（UTF-16），删除 raw 中对应音节并重新解析。
 * 返回写入的 UTF-16 单元数；cap 不足返回 -1。 */
int pehime_comp_commit(const PehimeDict *d, PehimeComp *c, const PehimeCand *cand,
                     uint16_t *out, int cap);


#endif /* PEHIME_PINYIN_H */



