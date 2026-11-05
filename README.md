# PEHIME —— WinPE 极简中文输入法

版本：v0.2

PEHIME 是给 **WinPE 10.0.19041 x64/x86** 用的极简中文输入法。WinPE 不加载第三方 IMM32/TSF
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

## 使用示例

中文态下直接输入全拼，按空格或数字上屏：`zhongguo`→中国、`nihao`→你好、`jisuanji`→计算机、
`cx`→程序（简拼）。

- 输入过程中用 `1`~`9` 选词、`-`/`=` 翻页；回车直接上屏拼音原文
- 首字母大写，或拼音无法解析且未命中简拼时，首候选即原文，可直接上屏英文

## 运行

1. 将 `PEHIME.EXE`、`pehime.dict`、`pehime.ini`（后两个可缺省）放在同一目录
2. 双击或命令行运行 `PEHIME.EXE`（首次运行会请求管理员权限）
3. 单击 Shift 切换中/英；右键托盘图标或右下角方框可切换、退出

说明：

- `pehime.ini` 缺失时会自动生成默认配置
- 自检：`PEHIME.EXE --selftest`（不装钩子、不弹窗、不提权）

## 卸载

退出 PEHIME 后删除 `PEHIME.EXE`、`pehime.dict`、`pehime.ini`（以及可能生成的 `PEHIME.log`）即可。
输入法不写注册表、不装服务/驱动，删除文件即完成卸载。

## 配置 `pehime.ini`

| 键 | 默认 | 说明 |
|---|---|---|
| `start_cn` | 0 | 1=启动中文，0=启动英文 |
| `page_size` | 5 | 每页候选数（3~9） |
| `page_height` | 20 | 候选窗字体像素高（10~40） |
| `log` | 0 | 1=写 `PEHIME.log` |
| `log_verbose` | 0 | 1=记录按键与候选列表（需 `log=1`） |

## 构建

需要 Python 3.8+，并安装 `jieba`、`pypinyin`（必需）和 `wordfreq`（可选）：

```cmd
pip install jieba pypinyin wordfreq
build_all.cmd
```

- `build_all.cmd`：x64 构建，会重新生成中文词库，并编译 `build\PEHIME.EXE`、`build\ptest.exe`
  （词库产物为 `build\pehime.dict`）
- 首次构建会自动下载 w64devkit（GCC 16.2.0）；静态链接，无需 Visual Studio
- 测试：

```cmd
build\ptest.exe build\pehime.dict zhongguo nihao
build\PEHIME.EXE --selftest
```

## 32 位构建

`build_x86.cmd` 使用 w64devkit 自带的 `i686-w64-mingw32-gcc` 编译 32 位版本，输出
`build\x86\PEHIME.EXE` 与 `build\x86\ptest.exe`。它需要先由 `build_all.cmd` 生成
`build\pehime.dict`，构建后会把可用的 `build\pehime.dict`、`build\pehime.ini` 复制到 `build\x86\`：

```cmd
build_all.cmd
build_x86.cmd
build\x86\ptest.exe build\x86\pehime.dict zhongguo nihao
build\x86\PEHIME.EXE --selftest
```

x64 与 x86 两个版本共用同一份 `pehime.dict` / `pehime.ini` 和同一套配置格式。

## 目录

```
src/hook/     主程序（键盘钩子、状态机、候选、上屏）
src/core/     词库读取、全拼解析与候选
src/ui/       候选窗、托盘 / 状态方框
src/common/   配置、日志
tools/        词库生成器、拼音测试、第三方许可
build/        构建产物（x64；32 位在 build/x86/）
```

## 词库

运行时的 `pehime.dict` 由 `tools/words2dict.py` 从 jieba 词表生成（415 音节 / 70,130 条），
领域补充词在 `tools/words/`。改词后运行 `build_all.cmd` 重新生成即可（32 位版随后重跑
`build_x86.cmd` 复制新词库）。

## 已知限制

- 支持 x64 与 x86 两种构建，目标 WinPE 10.0.19041 x64/x86 与现代 Windows（NT6+）；
  不承诺 NT5/XP 兼容
- XAML/TSF 界面（如新版任务栏、开始菜单）无法注入；部分自绘控件可能无效
- 托盘图标在 Explorer 重启后不会自动恢复（重启输入法即可）

## 许可

本项目以 **Apache License 2.0** 开源，完整条款见 [LICENSE](LICENSE)。

词库生成使用 jieba（MIT）、pypinyin（MIT）、wordfreq（Apache-2.0）、w64devkit（宽松许可），
许可全文在 `tools/licenses/`；分发词库数据时请保留该目录。
