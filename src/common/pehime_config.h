/*
 * pehime_config.h —— 极简 INI 配置（pehime.ini 与 EXE 同目录）
 *
 * 支持的键（全部可选，未配置时用默认值）：
 *   start_cn      1=启动即中文，0=启动英文（默认 0）
 *   page_size     每页候选数 3~9（默认 5）
 *   page_height   候选窗字体像素高 10~40（默认 20）
 *   log           1=写 PEHIME.log（默认 0）
 *   log_verbose   1=记录每次按键 + 每次候选列表（默认 0）
 *
 * pehime.ini 不存在时，程序会自动生成一个默认配置文件。
 */
#ifndef PEHIME_CONFIG_H
#define PEHIME_CONFIG_H

typedef struct {
    int start_cn;
    int page_size;
    int page_height;
    int log;
    int log_verbose;
} PehimeConfig;

void pehime_config_load(PehimeConfig *cfg, const char *exe_dir);

#endif /* PEHIME_CONFIG_H */
