/*
 * peim_log.h —— 极简文件日志（PE 里没有调试器，只能靠日志）
 */
#ifndef PEIM_LOG_H
#define PEIM_LOG_H

void peim_log_open(const char *exe_dir);
void peim_log(const char *fmt, ...);
void peim_log_close(void);

#endif /* PEIM_LOG_H */

