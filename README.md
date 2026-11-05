# PEHIME —— WinPE 极简中文输入法

版本：v0.2.1

## 核心功能

- 全拼输入（最多 32 个字母）：整串匹配词优先，另含音节、前缀、简拼、原文等候选
- 简拼：`cx`→程序、`cxy`→程序员、`cpgl`→磁盘管理、`nh`→你好、`ck`→词库
- 中文标点：`，。、；：？！……——“”《》【】` 等按常用中文输入法映射
- 候选窗两行（拼音 + 候选）：数字/空格选词，鼠标或键盘翻页，不抢焦点
- 托盘图标 / PE 右下角状态方框；默认英文态，单击 Shift 切换中/英
- 英文态所有按键原样直通，不吞键、不弹候选窗

## 设计

- WinPE 不加载第三方 IMM32/TSF 输入法，因此用 `WH_KEYBOARD_LL` 全局键盘钩子拦截按键，
  文本经队列用 `SendInput` 注入 Unicode
- 纯 C99、单线程、静态链接；不写注册表、不装驱动/服务，复制文件即可运行，删除文件即卸载
- 词库与架构无关，x64/x86 共用同一份 `pehime.dict`
- 候选窗为自绘 `WS_EX_NOACTIVATE` 弹窗，不抢焦点、不进任务栏、始终置顶

## 环境

- WinPE 10.0.19041 x64/x86，以及现代 Windows（NT6+）；不承诺 NT5/XP 兼容
- 需要管理员权限，普通用户启动时自动请求 UAC 提权
- 运行时文件 `PEHIME.EXE`、`pehime.dict`（可缺省）、`pehime.ini`（可缺省）放在同一目录

## 依赖

- 运行时只依赖 Windows 系统 DLL，静态链接，无第三方运行时，EXE 不依赖 VCRuntime
- 构建需要 w64devkit 2.11.0（GCC 16.2.0）：脚本首次运行时自动下载，无需 Visual Studio
- 生成词库需要 Python 3.8+ 及 `jieba`、`pypinyin`、`wordfreq`；`build_words.cmd` 检测到缺失时
  会提示「按任意键安装」并执行 `python -m pip install jieba pypinyin wordfreq`

## 构建

先运行 `build_words.cmd` 生成词库（词库变化后需重新运行），再按需运行 `build_x64.cmd` / `build_x86.cmd`：

```cmd
rem 生成词库并复制到 build\x64\、build\x86\
build_words.cmd
rem 分别编译 x64 / x86 可执行文件
build_x64.cmd
build_x86.cmd
```

- `build_words.cmd`：检查 Python 3.8+（缺失或版本过低则报错、等待按键后退出）和 `jieba`/`pypinyin`/`wordfreq`，
  缺少依赖时提示按任意键执行 `python -m pip install jieba pypinyin wordfreq` 并复查；
  运行 `tools\words2dict.py` 在 `.tmp\` 生成 `pehime.dict`（调试导出 `.tmp\pehime_cn_dump.txt`），
  再把词库复制到 `build\x64\`、`build\x86\`，复制后删除 `.tmp\pehime.dict`
- `build_x64.cmd` / `build_x86.cmd`：只编译本架构的 `PEHIME.EXE` 与 `ptest.exe`，
  不生成也不复制词库；缺少 w64devkit 时自动下载并保留在 `tools\w64devkit\`
  （该目录已被 `.gitignore` 忽略）；x86 使用 `i686-w64-mingw32-gcc`
- 产物布局：`build\x64\`、`build\x86\` 各含 `PEHIME.EXE`、`ptest.exe`、`pehime.dict`
- 临时文件统一放在 `.tmp\`（词库调试导出、ptest 调试输出等，已被 `.gitignore` 忽略）
- 测试：

```cmd
build\x64\ptest.exe build\x64\pehime.dict zhongguo nihao
build\x64\PEHIME.EXE --selftest
build\x86\ptest.exe build\x86\pehime.dict zhongguo nihao
build\x86\PEHIME.EXE --selftest
```
