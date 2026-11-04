# PEIM —— WinPE / Windows 极简输入法

版本标识：v0.1
本文说明项目由哪些部分组成、每部分负责什么、关键流程和修改方法。
实现规格/验收指标见 [docs/AI_REPRODUCTION_GUIDE.md](docs/AI_REPRODUCTION_GUIDE.md)。

## 1. 项目是什么

PEIM 是给 **WinPE 10.0.19041 x64** 用的极简中文输入法，同时兼容 Windows 10/11 桌面。

WinPE 不加载第三方 IMM32/TSF 输入法，所以采用：

```
全局键盘钩子（WH_KEYBOARD_LL）
  → 自己维护输入缓冲
  → 自己算候选、画候选窗
  → SendInput(KEYEVENTF_UNICODE) 把文字注入前台窗口
```

纯 C（C99）、单线程、静态链接、不写注册表、不装驱动/服务。
**需要管理员权限**：普通用户启动时会自动弹 UAC 提权，取消则退出。
默认英文态（避免 PE 菜单/工具的单键热键被吞），单击 Shift 切中文。

## 2. 目录结构

```
PEIM/
├─ .gitignore                  忽略构建产物与下载的编译器
├─ build_all.cmd               一键构建（项目根目录）
├─ src/                        源码
│  ├─ hook/hook_main.c         产品主程序（钩子、状态机、候选、上屏、定位、英文联想）
│  ├─ core/
│  │  ├─ peim_dict.{c,h}       中文词库只读加载/遍历/音节二分查找
│  │  ├─ peim_pinyin.{c,h}     全拼 DP 切分、候选生成、提交
│  │  └─ peim_en.{c,h}         英文联想词库（前缀查询）
│  ├─ ui/
│  │  ├─ peim_candwin.{c,h}    候选窗（自绘两行 + 闪烁光标 + ▲/▼ 点击）
│  │  └─ peim_tray.{c,h}       托盘图标 / PE 右下角状态方框
│  └─ common/
│     ├─ peim_config.{c,h}     INI 配置
│     └─ peim_log.{c,h}        文件日志（512 KB 轮转）
├─ tools/                      构建期工具与数据
│  ├─ README.md                工具/编译器下载说明
│  ├─ get_w64devkit.ps1        自动下载并解压 w64devkit
│  ├─ words2dict.py            词库生成器（一次生成中英文词库）
│  ├─ ptest.c                  拼音核心命令行测试
│  ├─ words/
│  │  ├─ cn_words_curated.txt  中文领域补充词
│  │  ├─ en_words_freq.txt     英文常用词源（按频率排序）
│  │  └─ en_words_curated.txt  英文领域补充词
│  ├─ licenses/                第三方许可全文
│  └─ w64devkit/               编译器（由脚本下载，不入库）
├─ build/                      构建产物
│  ├─ PEIMH.EXE                运行时：主程序
│  ├─ peim_cn.dict             运行时：中文词库
│  ├─ peim_en.dict             运行时：英文联想词库
│  ├─ peim.ini                 运行时：默认配置
│  └─ peim_cn_dump.txt         生成时的可读导出（调试用，可删）
├─ README.md                   本文件（项目结构/模块/流程/修改指引）
├─ docs/
│  └─ AI_REPRODUCTION_GUIDE.md 实现规格 + 验收矩阵（可交给 Agent 复现）
└─ dist/                       最终运行包（可选，只有运行文件 + 校验和）
```

## 3. 各模块职责

| 模块 | 职责 | 关键点 |
|---|---|---|
| `hook_main.c` | 程序入口、键盘钩子、中/英状态机、候选拼接、上屏决策、注入队列、定位、英文联想、托盘消息 | 钩子回调里**不能调 SendInput**，只能入队 + PostMessage |
| `peim_dict` | 中文词库零拷贝读取、词条遍历、音节名/ID、二分查找、音节前缀判断 | 词库文件只读；音节表必须字典序 |
| `peim_pinyin` | `PeimComp` 状态、DP 切分、音节候选、提交（消耗音节） | 切分权重来自词库单字权重 |
| `peim_en` | 英文词库读取、前缀查询、短词优先后排序 | 词条按字典序，二分 + 顺序收集 |
| `peim_candwin` | 候选窗创建/绘制/定位/定时器/鼠标翻页 | `WS_EX_NOACTIVATE`，两行布局，闪烁光标 |
| `peim_tray` | 托盘图标 / PE 右下角方框、右键菜单（切换/退出） | 中/英图标均为蓝底白字；无 Explorer 时自动降级为方框 |
| `peim_config` | 读 `peim.ini`；文件不存在时自动生成默认配置 | `PEIM_INI` 环境变量可覆盖（测试） |
| `peim_log` | 写 `PEIM.log`，512 KB 轮转 | `PEIM_LOG_DIR` 可覆盖目录；`log_verbose` 记录候选列表 |

## 4. 启动流程（`WinMain`）

1. `CreateMutexW("PEIM_HOOK_SINGLE_INSTANCE")`：已有实例直接退出
   （跨完整性级别时 `ERROR_ACCESS_DENIED` 也按已有实例处理）
2. 取 EXE 目录 → 读 `peim.ini` → 打开 `PEIM.log`
3. 低完整性检测：Low 进程无法注入普通窗口，提示并退出（退出码 4）
4. 读同目录 `peim_cn.dict`、`peim_en.dict`（可选）；失败继续（中文退化为原文直输）
5. 创建隐藏消息窗口 `PEIMHookMsgWnd`（接收注入/翻页/托盘消息）
6. 创建候选窗（隐藏）
7. 初始化托盘图标（无托盘则创建右下角方框）
8. `SetWindowsHookExW(WH_KEYBOARD_LL)` 装钩子 → 进入消息循环
9. `--selftest`：只做 1~6，写自检日志后退出（不装钩子、不弹窗、不提权）

## 5. 按键与候选流程

### 5.1 中/英模式

- 默认英文态；单击 Shift 切换（Shift+字母/Shift+Tab 不触发）
- 切换时候选窗短暂显示"中/英"约 0.9 秒
- 中文态：字母进缓冲（最多 32），重新解析并刷新候选
- 英文态：字母放行，同时刷新英文补全候选

### 5.2 中文候选拼接（`refresh_cands`）

按以下顺序拼接（细节见 [docs/AI_REPRODUCTION_GUIDE.md](docs/AI_REPRODUCTION_GUIDE.md) §6）：

1. 解析失败 → 原文候选
2. 解析失败但命中简拼 → 简拼候选 → 原文候选
3. 首字母大写 → 原文 → 整串完全匹配 → 前缀 → 音节
4. 纯完整音节 → 整串匹配 → 音节 → 前缀
5. 有未完成音节 → 整串匹配（通常无）→ 前缀 → 音节
6. 无完整音节（如 `z`）→ 整串匹配 → 前缀

整串匹配/前缀/简拼候选选中时**吃掉整个输入缓冲**；音节候选只删除它覆盖的音节。

### 5.3 上屏与注入

- 钩子回调只把 UTF-16 文本压入环形队列，`PostMessage(WM_PEIM_INJECT)`
- 消息循环收到后 `SendInput(KEYEVENTF_UNICODE)` 逐字符注入
- 英文联想替换：先按已输入字母数发退格，再注入完整单词
- 中文模式：主键盘符号键固定转全角/中文符号（如 Shift+4→`＄`、Shift+2→`＠`、`,`→`，`、引号成对）；英文模式保持英文符号
- 这样避免"在低级钩子里调 SendInput 导致死锁"

### 5.4 候选窗

- `WS_POPUP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST`
- 两行：拼音/字母 + 闪烁光标 / `1.候选 … 页码 ▲▼`
- 定位：优先 Win32 插入符（`GetGUIThreadInfo`），否则鼠标下方
- ▲/▼ 点击向消息窗口发 `PEIM_CAND_MSG_PAGE`，改页码重绘，不抢焦点
- 翻页只刷新候选与页码，**不改变候选窗位置**（鼠标停在箭头上也不会跟着跑）

### 5.5 英文模式补全

- 字母照常输入，同时记录最近字母（1~32）
- 查询 `peim_en.dict`，短词优先显示
- `1`~`9` / Tab 选择（列表显示时数字被吞）；空格/回车/标点结束联想
- 切换窗口、切换中/英、按 Esc 会清空联想

## 6. 数据与词库生成

运行时词库是二进制文件，不手工编辑；源数据在 `tools/words/`：

| 源文件 | 生成 | 说明 |
|---|---|---|
| `cn_words_curated.txt` | `build/peim_cn.dict` | 中文领域补充词（最高权重） |
| `en_words_freq.txt` + `en_words_curated.txt` | `build/peim_en.dict` | 英文常用词 + 补充词 |

- 中文词库：单字 + 二字/三字/四字词；权重用 wordfreq 通用词频，缺数据退回 jieba，
  多音字次读音按常用词使用证据过滤
- `tools/words2dict.py` 还会输出 `build/peim_cn_dump.txt`（可读，调试用）
- 命名约定：运行时 `peim_<语言>.dict`；源数据 `<语言>_words_<类别>.txt`；
  生成脚本 `words2dict.py`（头部 `CN_WORD_FILES` / `EN_WORD_FILES` 分别列源文件）

改词流程：

```cmd
rem 1. 改 tools/words/*.txt
build_all.cmd
rem 2. 用 ptest 验证
build\ptest.exe build\peim_cn.dict <拼音>
rem 3. 把新的 peim_cn.dict / peim_en.dict 复制到运行目录
```

## 7. 构建、测试、部署

```cmd
build_all.cmd    rem 每次都会：重新生成中英文词库 → 编译 PEIMH.EXE / ptest.exe

build\ptest.exe build\peim_cn.dict zhongguo nihao    rem 核心测试
build\PEIMH.EXE --selftest                            rem 部署自检（不装钩子、不弹窗）
```

- Python：每次构建都需要 Python 3.8+ + `jieba` + `pypinyin` + `wordfreq`（缺失会报错）
- 编译器：`tools\w64devkit` 不存在时，`build_all.cmd` 会自动调用 `tools\get_w64devkit.ps1`
  下载 w64devkit 2.11.0（GCC 16.2.0）；也可手动运行脚本。静态链接，无需 VS/管理员
- 运行时布局：`PEIMH.EXE` + `peim_cn.dict` + `peim_en.dict`（可缺省）+ `peim.ini`（可缺省）
- PE 里把文件复制到可写目录（如 `X:\Tools\PEIM`）后手动运行；仓库不包含 PE 注入/安装脚本
- `dist/` 是可选运行包（只含运行文件 + SHA256SUMS）

## 8. 关键设计决策与坑

1. **钩子而非 IMM32/TSF**：PE 不加载第三方 IME；钩子 + 注入零注册表、可复制运行。
2. **默认英文态**：PE 菜单/工具热键是单键，中文态会吞字母；Shift 才进中文。
3. **注入必须排队**：低级钩子回调里直接 `SendInput` 会死锁（实测卡死）。
4. **字母一律进缓冲**：不因"不是合法拼音"拒绝按键，否则英文原文会丢字母。
5. **DP 切分**：长音节优先容易切出 `ming|o|ng`；改为音节权重 DP + 最少音节优先。
6. **整串完全匹配优先**：`fangan` 不能因为切成 `fan|gan` 丢掉"方案"。
7. **多音字次读音过滤**：常见字（而/事/耐）的生僻异读不能抢常用字（能/自/帮）。
8. **候选窗 NOACTIVATE**：不抢焦点；不要加 `HTTRANSPARENT`，否则 ▲/▼ 点不到。
9. **单实例互斥体**：双实例会双倍吞键；跨完整性级别也要避免重复。
10. **低完整性检测**：从 Low 标签目录启动会无法注入，宁可提示退出也不要静默失效。
11. **日志限容轮转**：PE 的 X: 是内存盘，超过 512 KB 轮转 `PEIM.log.old`。\n12. **不要和 Microsoft Pinyin 同时设为中文**：两个中文 IME 会同时处理按键和符号；PEIM 使用时把系统输入法设为英文或关闭。

## 9. 修改指引

| 想做的事 | 改哪里 |
|---|---|
| 加按键/热键 | `hook_main.c` 的 `handle_key`（先判断是否在组字） |
| 改候选排序 | `peim_pinyin.c` 的 `peim_comp_candidates`；`hook_main.c` 的 `cmp_by_weight` |
| 改前缀匹配 | `hook_main.c` 的 `entry_prefix_match` / `prefix_cb`（`max_syl`） |
| 改简拼 | `hook_main.c` 的简拼分支 + `initials_cb` |
| 改上屏语义 | `select_item` 的三个分支（原文/提前上屏/音节） |
| 改候选窗布局 | `src/ui/peim_candwin.c` 的 `measure_width` / `paint`（两处要一致） |
| 改定位 | `hook_main.c` 的 `get_anchor` |
| 改托盘/方框 | `src/ui/peim_tray.c` |
| 加配置项 | `peim_config.h` 结构体 + `peim_config.c` 默认值/解析 + `build/peim.ini` |
| 改词库来源/权重 | `tools/words2dict.py`，再运行 `build_all.cmd`（会自动重新生成词库） |

## 10. 测试与验证

- **核心单测**：`build\ptest.exe build\peim_cn.dict <拼音...>`（切分/候选/提交，无窗口依赖）
- **部署自检**：`PEIMH.EXE --selftest`（不装钩子、不弹窗）
- **桌面/PE 实机**：按 [docs/AI_REPRODUCTION_GUIDE.md](docs/AI_REPRODUCTION_GUIDE.md) §10 验收矩阵逐项测试
- **排障**：`peim.ini` 设 `log_verbose=1`，`PEIM.log` 会记录每次输入与候选列表

回归必测：`zhongguo`→中国、`neng`→能、`zi`→自、`yange`→严格、`fangan`→方案、
`zhongg` 只二字词、`zhongguod` 三字词、`cx/cxy/cpgl` 简拼、`nh/ck`、`lue/nue`、
32 字母长拼音、Shift 切换、PE 菜单按键、候选窗 ▲/▼、英文联想 `hel`。

## 11. 第三方与许可

- 运行时 EXE 只依赖 Windows 系统 DLL，不链接第三方库（仅静态链接 MinGW-w64 启动代码）。
- 词库生成使用 jieba（MIT）、pypinyin（MIT）、wordfreq（Apache-2.0）、
  google-10000-english（仓库未声明明确许可）、w64devkit（宽松许可）。
- 许可全文在 `tools/licenses/`；分发词库数据时请保留该目录。

## 文档

- [AI 复现指南（实现规格与验收）](docs/AI_REPRODUCTION_GUIDE.md)——可交给 Agent 从零复现
