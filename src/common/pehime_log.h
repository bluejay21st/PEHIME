/*
 * pehime_log.h —— 极简文件日志（PE 里没有调试器，只能靠日志）
 */
#ifndef PEHIME_LOG_H
#define PEHIME_LOG_H

void pehime_log_open(const char *exe_dir);
void pehime_log(const char *fmt, ...);
void pehime_log_close(void);

#endif /* PEHIME_LOG_H */

