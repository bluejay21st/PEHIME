# PEIM —— WinPE 极简中文输入法

版本：v0.2

PEIM 是给 **WinPE 10.0.19041 x64** 用的极简中文输入法。WinPE 不加载第三方 IMM32/TSF
输入法，因此采用「全局键盘钩子 + Unicode 注入」实现，纯 C99、单线程、静态链接：

- 不写注册表、不装驱动/服务
- 复制文件即可运行，删除文件即卸载
- 需要管理员权限（启动时会自动请求 UAC 提权）

## 功能

- 全拼输入（最多 32 个字母）：整串匹配词优先，另含音节、前缀、简拼、原文等候选
- 简拼：`cx`→程序、`cxy`→程序员、`cpgl`→磁盘管理、`nh`→你好、`ck`→词库
- 中文标点：`，。、；：？！……——“”《》【】` 等按常用中文输入法映射
- 候选窗两行（拼音 + 候选）：数字/空格选词，鼠标或键盘翻页，不抢焦点
- 托盘图标 / PE 右下角状态方框；默认英文态，单击 Shift 切换中/英
- 英文态所有按键原样直通，不吞键、不弹候选窗

## 运行

1. 将 `PEIMH.EXE`、`peim.dict`、`peim.ini`（后两个可缺省）放在同一目录
2. 双击或命令行运行 `PEIMH.EXE`（首次运行会请求管理员权限）
3. 单击 Shift 切换中/英；右键托盘图标或右下角方框可切换、退出

说明：

- `peim.ini` 缺失时会自动生成默认配置
- 自检：`PEIMH.EXE --selftest`（不装钩子、不弹窗、不提权）

## 配置 `peim.ini`

| 键 | 默认 | 说明 |
|---|---|---|
| `start_cn` | 0 | 1=启动中文，0=启动英文 |
| `page_size` | 5 | 每页候选数（3~9） |
| `page_height` | 20 | 候选窗字体像素高（10~40） |
| `log` | 0 | 1=写 `PEIM.log` |
| `log_verbose` | 0 | 1=记录按键与候选列表（需 `log=1`） |

## 构建

需要 Python 3.8+，并安装 `jieba`、`pypinyin`（必需）和 `wordfreq`（可选）：

```cmd
pip install jieba pypinyin wordfreq
build_all.cmd
```

- `build_all.cmd` 会重新生成中文词库，并编译 `build\PEIMH.EXE`、`build\ptest.exe`
- 首次构建会自动下载 w64devkit（GCC 16.2.0）；静态链接，无需 Visual Studio
- 测试：

```cmd
build\ptest.exe build\peim.dict zhongguo nihao
build\PEIMH.EXE --selftest
```

## 目录

```
src/hook/     主程序（键盘钩子、状态机、候选、上屏）
src/core/     词库读取、全拼解析与候选
src/ui/       候选窗、托盘 / 状态方框
src/common/   配置、日志
tools/        词库生成器、拼音测试、第三方许可
build/        构建产物
```

## 词库

运行时的 `peim.dict` 由 `tools/words2dict.py` 从 jieba 词表生成（415 音节 / 70,130 条），
领域补充词在 `tools/words/`。改词后运行 `build_all.cmd` 重新生成即可。

## 已知限制

- 面向 WinPE 10.0.19041 x64；仅支持 x64
- XAML/TSF 界面（如新版任务栏、开始菜单）无法注入；部分自绘控件可能无效
- 托盘图标在 Explorer 重启后不会自动恢复（重启输入法即可）

## 许可

本项目以 **Apache License 2.0** 开源，完整条款见 [LICENSE](LICENSE)。

词库生成使用 jieba（MIT）、pypinyin（MIT）、wordfreq（Apache-2.0）、w64devkit（宽松许可），
许可全文在 `tools/licenses/`；分发词库数据时请保留该目录。
