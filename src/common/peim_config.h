/*
 * peim_config.h —— 极简 INI 配置（peim.ini 与 EXE 同目录）
 *
 * 支持的键（全部可选，未配置时用默认值）：
 *   start_mode    0=中文，1=英文（带补全，默认 1）
 *   page_size     每页候选数 3~9（默认 5）
 *   ui_height     候选窗字体像素高 10~40（默认 16）
 *   en_suggest    1=英文模式显示单词补全（默认 1）
 *   log           1=写 PEIM.log（默认 1）
 *   log_verbose   1=记录每次按键 + 每次候选列表（默认 0）
 *
 * peim.ini 不存在时，程序会自动生成一个默认配置文件。
 */
#ifndef PEIM_CONFIG_H
#define PEIM_CONFIG_H

#define PEIM_MODE_CN   0
#define PEIM_MODE_EN   1
#define PEIM_MODE_COUNT 2

typedef struct {
    int start_mode;
    int page_size;
    int ui_height;
    int en_suggest;
    int log;
    int log_verbose;
} PeimConfig;

void peim_config_load(PeimConfig *cfg, const char *exe_dir);

#endif /* PEIM_CONFIG_H */
