# PEHIME 项目交接笔记（新会话请先读本文件）

更新时间：2026-11-05 · 当前版本 **v0.2.1** · 发布提交 `af007c3` · 工作区 `D:\Projects\PEIM`

> 本文件是给接手开发的新会话/新同学的“重点记忆”，不替代 README 和实现指南。
> 阅读顺序建议：本文件 → `README.md` → `docs/AI_REPRODUCTION_GUIDE.md`。

## 1. 项目速览

- 产品名：**PEHIME**（WinPE 极简中文输入法；由早期名字 PEIM 全量重命名而来）
- 仓库：https://github.com/bluejay21st/PEHIME （public，Apache-2.0，`LICENSE`）
- 当前发布：**v0.2.1（latest）**，annotated tag `v0.2.1` → commit `af007c3`
  - https://github.com/bluejay21st/PEHIME/releases/tag/v0.2.1
  - 资产：`PEHIME-v0.2.1-x64.zip`、`PEHIME-v0.2.1-x86.zip` 及各自 `.sha256`
- 旧发布：`v0.1`、`v0.2` 保留未动（旧资产仍是 `PEIM-*` 命名）
- 本地 `main` 与 `origin/main` 同步；`build/`、`dist/`、`.tmp/`、`tools/w64devkit/` 均被 `.gitignore` 忽略
- 本地目录名仍是 `D:\Projects\PEIM`，但产品/文件命名均已改为 PEHIME

## 2. 项目是什么

给 **WinPE 10.0.19041 x64/x86**（以及现代 Windows NT6+）用的极简中文输入法。

- WinPE 不加载第三方 IMM32/TSF，因此采用：`WH_KEYBOARD_LL` 全局键盘钩子 → 自维护输入缓冲/候选 → 候选窗 → 队列 + `SendInput(KEYEVENTF_UNICODE)` 注入前台窗口
- 纯 C99、单线程、静态链接；不写注册表、不装驱动/服务；复制运行文件即可用，删除即卸载
- 需要管理员权限；普通用户启动会请求 UAC 提权
- 默认英文态（避免 PE 菜单热键被吞），单击 Shift 切中/英
- **英文态所有按键原样直通**，不缓冲、不弹候选窗（仅切换模式时短暂显示中/英提示）
- 中文态：全拼、简拼、整串匹配优先、音节/前缀候选、原文候选、中文标点映射、候选窗翻页
- 目标明确“面向 WinPE”，文档不强调 Windows 桌面兼容，也不明确否认；**不承诺 NT5/XP**

## 3. 源码结构（`src/`）

| 文件 | 职责 |
|---|---|
| `hook/hook_main.c` | 入口 `WinMain`、键盘钩子、状态机、候选拼接、上屏/注入队列、定位、托盘消息、单实例/提权/自检 |
| `core/pehime_dict.{c,h}` | 二进制词库只读加载、遍历、音节二分查找；加载时整段校验词条区 |
| `core/pehime_pinyin.{c,h}` | 全拼 DP 切分、候选生成、上屏语义 |
| `ui/pehime_candwin.{c,h}` | 候选窗绘制/定位/定时器/▲▼ 点击 |
| `ui/pehime_tray.{c,h}` | 托盘图标 / PE 右下角状态方框 |
| `common/pehime_config.{c,h}` | `pehime.ini` 读取与默认配置生成 |
| `common/pehime_log.{c,h}` | `PEHIME.log` 写入与 512 KB 轮转 |
| `tools/words2dict.py` | 生成中文词库（`.tmp/pehime.dict` + `.tmp/pehime_cn_dump.txt`） |
| `tools/ptest.c` | 拼音核心命令行测试；调试输出 `.tmp/ptest_out.txt` |

## 4. 关键命名 / 格式 / 配置

- 可执行文件：`PEHIME.EXE`；运行时文件：`pehime.dict`、`pehime.ini`、`PEHIME.log` / `PEHIME.log.old`
- 窗口类：`PEHIMEHookMsgWnd` / `PEHIMECandWnd` / `PEHIMETrayWnd` / `PEHIMEIndWnd`
- 互斥体：`PEHIME_HOOK_SINGLE_INSTANCE`
- 环境变量：`PEHIME_INI`（覆盖 ini 路径）、`PEHIME_LOG_DIR`（日志目录）、`PEHIME_INDICATOR`（强制右下角方框，调试）
- 版本宏：`PEHIME_HOOK_VERSION`（`hook_main.c`）
- 词库格式：v1 小端，magic 固定 **4 字节 `0x4D494550`**（历史格式，改名时**故意保持不变**）；当前 415 音节 / 70,130 条 / 941,919 B，SHA256 `8806258423e974fb1208ac63183bcf30b33900ce6d39d48228da675db7923589`
- 词库与架构无关：x64/x86 共用同一份 `pehime.dict`
- `pehime.ini` 键与默认值：
  - `start_cn=0`（0 英文 / 1 中文）
  - `page_size=5`（3~9）
  - `page_height=20`（10~40，候选窗字体像素高；旧键 `font_height` 已不再识别）
  - `log=0`（1 才写日志）
  - `log_verbose=0`（需 `log=1` 才记录按键/候选）
- 自动生成的默认 ini：UTF-8 BOM + CRLF + 中文注释 + 空行分隔（318 字节）
- 注入队列 64 条；钩子回调内**绝不**直接 `SendInput`，队列满则丢弃并记日志

## 5. 构建（根目录脚本）

```cmd
build_words.cmd   rem 词库：检查 Python/依赖 → words2dict → 复制到 build\x64、build\x86
build_x64.cmd     rem 只编译 x64：build\x64\PEHIME.EXE + build\x64\ptest.exe
build_x86.cmd     rem 只编译 x86：build\x86\PEHIME.EXE + build\x86\ptest.exe
```

- 已删除旧 `build_all.cmd`
- `build_words.cmd`：
  - Python 3.8+ 缺失/过低 → 报错 + `pause` 等待按键 + `exit /b 1`
  - 缺 `jieba`/`pypinyin`/`wordfreq` → 提示“按任意键安装” + `python -m pip install ...` + 复查
  - 生成 `.tmp\pehime.dict`、`.tmp\pehime_cn_dump.txt`，把 dict 复制到两个架构目录后删除 `.tmp\pehime.dict`；dump 保留
- `build_x64.cmd` / `build_x86.cmd`：不碰词库、不要求 Python；缺编译器时自动跑 `tools\get_w64devkit.ps1` 下载并**保留**在 `tools\w64devkit\`（`.gitignore` 忽略）
- 最终布局：
  - `build\x64\{PEHIME.EXE, ptest.exe, pehime.dict}`
  - `build\x86\{PEHIME.EXE, ptest.exe, pehime.dict}`
  - 调试/临时文件在 `.tmp\`（已 gitignore）
- 编译参数：
  - 主程序：`-O2 -Wall -Wextra -std=c99 -DUNICODE -D_UNICODE -Isrc/core -Isrc/ui -Isrc/common -mwindows -static -static-libgcc -s`
  - ptest：同上但不带 `-mwindows`
  - 链接库：`-luser32 -lgdi32 -lshell32 -ladvapi32`
- 词库依赖：`jieba`、`pypinyin`、`wordfreq`（`tools/words2dict.py` 中 wordfreq 缺失会退回 jieba 词频）

## 6. 测试与验证命令

```cmd
build\x64\ptest.exe build\x64\pehime.dict zhongguo nihao jisuanji
build\x64\PEHIME.EXE --selftest
build\x86\ptest.exe build\x86\pehime.dict zhongguo nihao
build\x86\PEHIME.EXE --selftest
```

- ptest 回归期望：`zhongguo`→中国、`nihao`→你好、`jisuanji`→计算机、`neng`→能、`zi`→自、`yange`→严格、`lue`→略、`nue`→虐/疟；32 字母长拼音能正确切分
- `--selftest`：不装钩子、不弹窗、不提权；退出码 0=正常、3=词库缺失/损坏；日志 `SELFTEST: OK`、`ver=当前版本`
- 测试时建议用环境变量避免污染 build 目录：
  - `PEHIME_INI=<临时 ini>`（否则会在 EXE 同目录自动生成 `pehime.ini`）
  - `PEHIME_LOG_DIR=<临时目录>`
- 损坏词库健壮性应保持：截断/字段越界/非法音节 ID 等都被加载器拒绝（selftest 退出 3），不崩溃
- `ptest` 的 `.tmp/ptest_out.txt` 是工作目录相对路径；从仓库根运行写仓库根 `.tmp`

## 7. 发布流程（重要）

## 7.1 当前发布状态

- 最新：**v0.2.1**，tag `v0.2.1`（annotated），commit `af007c3`
- 资产（GitHub Release）：
  - `PEHIME-v0.2.1-x64.zip` 684,582 B，SHA256 `1b3fd8aee1738abb617a6170824ab78dba212ed8165fb21bfe5e3e5d49cbfa92`
  - `PEHIME-v0.2.1-x86.zip` 685,857 B，SHA256 `c34f557b1c1dd71e9047043eef2bef85e5277a3e445268263aeaaaa4908bc144`
  - 各自 `.sha256` 资产
- 每个 zip 根级：`PEHIME.EXE`、`pehime.dict`、`pehime.ini`、`LICENSE`、`SHA256SUMS.txt`
- **按用户决定**：发布包不携带第三方许可文本；文档已注明第三方许可见仓库 `tools/licenses/`
- 旧 Release v0.1/v0.2 不删除、不覆盖；v0.2.1 为 latest
- `dist/` 是本机发布产物目录（gitignored），保留 v0.2.1 的 zip、`.sha256`、`RELEASE_NOTES-v0.2.1.md`

## 7.2 下次发版步骤（需用户明确批准后再执行）

1. 确认版本号（三处同步）：
   - `src/hook/hook_main.c` 头注释 + `PEHIME_HOOK_VERSION`
   - `README.md` 的“版本：vX.Y.Z”
   - `docs/AI_REPRODUCTION_GUIDE.md` 的“版本标识”
2. 重新生成词库（如词源/生成器有变化）并重新编译两架构，跑 ptest/selftest
3. 准备 `dist/`：
   - 每个架构一个 zip：`PEHIME-vX.Y.Z-{x64,x86}.zip`（只含上述 5 个文件）
   - 生成 `SHA256SUMS.txt`（CRLF，`hash  filename` 两空格）与外层 `.sha256`
   - 写 `RELEASE_NOTES-vX.Y.Z.md`（正文用于 GitHub Release）
4. 本地复核：zip 内容/哈希、解压后双架构 selftest、版本号、文档
5. 用户批准后执行：
   - `git add` / `git commit -m "release: vX.Y.Z"`
   - `git tag -a vX.Y.Z -m "PEHIME vX.Y.Z"`
   - push `main` + tag；创建 GitHub Release（非 draft/非 prerelease，make latest），上传 zip + `.sha256`，正文用 release notes
   - 下载资产复算哈希；保留旧 release
6. 推送方式：**GitHub SSH 在当前沙箱会被管道限制阻断**；使用 HTTPS + 本机 Git Credential Manager 提供的凭据（脚本从 GCM `get` 取 token，通过 `http.extraHeader` 临时注入，不写入仓库/不打印）。仓库地址已从 `PEIM` 改名为 `PEHIME`，`origin` 为 `git@github.com:bluejay21st/PEHIME.git`
7. 不要提交/推送 `dist/`、`build/`、`.tmp/`、`tools/w64devkit/`

## 8. 关键历史与设计决策

- 已移除英文候选词/补全功能；英文态完全直通
- 配置键 `font_height` → `page_height`；默认 ini 使用注释+空行格式，`log` 默认 0
- 运行时词库 `peim_cn.dict` → `pehime.dict`
- 项目从 PEIM 全量重命名为 PEHIME（源码文件、宏/类型/函数、窗口类、互斥体、环境变量、文档、GitHub 仓库）
- 增加 32 位构建；目标 WinPE 10 x86 / 现代 32 位 Windows（NT6+），不承诺 NT5/XP
- 构建脚本拆分为 words/x64/x86 三个；编译器保留；临时文件统一 `.tmp`
- 词库加载做过健壮性加固；注入队列满时丢弃而非回调内注入；Shift 组合键不会误切换
- README 只保留“核心功能/设计/环境/依赖/构建”，且不引用 AI 实现文档；`docs/AI_REPRODUCTION_GUIDE.md` 仍保留，但 README 不链接
- 许可证 Apache-2.0；第三方许可文本在 `tools/licenses/`

## 9. 已知限制 / 待验证

- x86 目前只在 64 位 Windows 的 WOW64 下验证过；**真实 WinPE 10 x86 实机验收未做**
- x64 的 PE 实机验收（键钩子/候选窗/托盘/UAC/注入）也建议按 `docs/AI_REPRODUCTION_GUIDE.md` §10 矩阵跑一轮
- `tools\get_w64devkit.ps1` 的真实联网下载→SHA256→解压链路未端到端验证过（脚本解析/BOM/错误分支已通过）
- 目标平台明确 NT6+；NT5/XP 不支持也未验证
- `tools/licenses/` 中第三方许可是仓库内文件，不在发布 zip 内（用户已确认这样即可）
- README 当前 63 行；若用户再次要求“只描述核心功能/设计/环境/依赖/构建”，不要扩展章节

## 10. 用户偏好与协作约定

- 文档用中文；**尽量简洁、准确**；README 只写核心功能、设计、环境、依赖、构建
- README 中**不要提 AI 实现文档**
- 面向 WinPE 优先；不要强调 Windows 桌面兼容，也不要明确否认
- **发版必须等用户批准后再执行**；不要擅自 push/打 tag/发 Release
- 遇到范围不明或需要取舍时先问，不要猜测/自行决定
- 临时文件全部放 `.tmp/`，保持目录整洁
- 编译器要保留（`tools/w64devkit/`），确保 `.gitignore` 覆盖、不上传
- 发布包按用户确认：x64/x86 各一个 zip + 各自 `.sha256`；不包第三方许可文本

## 11. 常见坑

- `tools/get_w64devkit.ps1` 必须是 **UTF-8 with BOM**，否则 Windows PowerShell 5.1 解析失败（build 脚本调用的正是 5.1）
- 批处理错误分支不要写在嵌套 `if (...)` 块里用 `exit /b`（cmd 可能丢退出码）；本项目已改为 label/goto + `pause` + `exit /b 1`
- 构建脚本调用 `get_w64devkit.ps1` 时不传 `-Force`，避免误删已保留的编译器；只有显式 `-Force` 才会删除目标目录
- `.tmp/` 是工作目录相对；从不同 cwd 跑 ptest 会在该 cwd 下建 `.tmp/ptest_out.txt`，可接受
- `PEHIME.EXE --selftest` 若未设置 `PEHIME_INI`，会在 EXE 同目录自动生成 `pehime.ini`；自动化测试要设置环境变量
- 词库 magic 是 4 字节 `0x4D494550`，改名时没有改它；不要因为“全量重命名”去改词库格式
- 根目录已无 `build_all.cmd`；不要恢复
- 当前仓库公开；发布资产在 GitHub Releases；本地 `dist/` 只是候选/留档

## 12. 常用入口链接

- README：`README.md`
- 实现规格/验收：`docs/AI_REPRODUCTION_GUIDE.md`
- 构建脚本：`build_words.cmd`、`build_x64.cmd`、`build_x86.cmd`
- 工具说明：`tools/README.md`
- 词库生成器：`tools/words2dict.py`
- 发布说明草稿：`dist/RELEASE_NOTES-v0.2.1.md`
- GitHub：https://github.com/bluejay21st/PEHIME
- 最新 Release：https://github.com/bluejay21st/PEHIME/releases/tag/v0.2.1
