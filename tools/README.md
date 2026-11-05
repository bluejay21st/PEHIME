# tools 说明

本目录存放**构建期工具和数据**，运行时不需要。

## 编译器（w64devkit）

仓库不再直接携带编译器；`build_all.cmd` 发现 `tools\w64devkit` 不存在时会自动调用本脚本。也可以手动运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\get_w64devkit.ps1
```

- 版本：**w64devkit 2.11.0**（内含 GCC 16.2.0 / MinGW-w64，可交叉编译 x64 与 x86 目标）
- 下载地址：<https://github.com/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x64-2.11.0.7z.exe>
- SHA256：`54c2bb9962e8a30c8270e34c5efc3d5fee579f53087fa7d435b95fbba2af7b2b`
- 解压位置：`tools\w64devkit\w64devkit\`（脚本会自动创建）
- 校验：脚本会校验 SHA256，并检查 `bin\x86_64-w64-mingw32-gcc.exe` 是否可运行
- 32 位构建：`build_x86.cmd` 使用同一目录下的 `bin\i686-w64-mingw32-gcc.exe`，不存在时同样会自动下载 w64devkit

w64devkit 是便携的 C/C++ 开发套件（GCC、binutils、GNU Make、busybox-w32 等），
只用于编译，不进入最终运行包；其许可为 public domain / 宽松许可，
MinGW-w64 运行时代码的许可见 `w64devkit` 目录内的 `COPYING.MinGW-w64-runtime.txt`。
`tools\w64devkit\` 已在根目录 `.gitignore` 中忽略，不会误提交。

> 下载慢或无法访问 GitHub 时，可自行下载同一文件放到本地，然后：
> `powershell -File tools\get_w64devkit.ps1 -Url "file:///D:/path/w64devkit-x64-2.11.0.7z.exe"`
> 或直接把解压后的 `w64devkit` 目录放到 `tools\w64devkit\`。

## 词库数据

| 文件 | 用途 |
|---|---|
| `words/cn_words_curated.txt` | 中文领域补充词（最高优先级） |

`words2dict.py` 头部用 `CN_WORD_FILES` 数组列出中文补充词源文件：

```python
CN_WORD_FILES = ["tools/words/cn_words_curated.txt"]
```

每次运行 `build_all.cmd` 都会重新生成中文词库（覆盖 `build/pehime.dict`、`build/pehime_cn_dump.txt`）。
32 位构建用根目录的 `build_x86.cmd`：它读取 `build/pehime.dict`（需先运行 `build_all.cmd`），
编译 `build\x86\PEHIME.EXE`、`build\x86\ptest.exe`，并把 `build\pehime.dict`、`build\pehime.ini`
复制到 `build\x86\` 供 32 位程序使用。
构建脚本会检查 Python 3.8+；词库生成需要 `jieba` + `pypinyin`（必需）与 `wordfreq`
（可选，缺失时词频退回 jieba）：

```cmd
pip install jieba pypinyin wordfreq
```

## 其他

| 文件 | 用途 |
|---|---|
| `words2dict.py` | 生成 `build/pehime.dict` 和 `build/pehime_cn_dump.txt` |
| `ptest.c` | 拼音核心命令行测试（构建时编为 `build/ptest.exe`，32 位为 `build/x86/ptest.exe`） |
| `licenses/` | 词库数据/工具涉及的第三方许可全文 |

## 开源项目与地址

构建期用到的开源项目（运行时不依赖这些库）：

| 项目 | 用途 | 许可 | 地址 |
|---|---|---|---|
| w64devkit | 便携 GCC/MinGW-w64 工具链（编译器） | public domain / 宽松许可 | https://github.com/skeeto/w64devkit |
| GCC | C 编译器（w64devkit 内含） | GPLv3（仅构建工具，不进入交付 EXE） | https://gcc.gnu.org/ |
| MinGW-w64 | Windows x64/x86 目标支持与运行时代码 | 宽松许可（见编译器目录） | https://www.mingw-w64.org/ |
| Python | 运行词库生成脚本 | PSF License | https://www.python.org/ |
| jieba | 中文分词/词频（生成中文词库） | MIT | https://github.com/fxsjy/jieba |
| pypinyin | 汉字注音/多音字（生成中文词库） | MIT | https://github.com/mozillazg/python-pinyin |
| wordfreq | 中文通用词频排序（生成中文词库） | Apache-2.0 | https://github.com/rspeer/wordfreq |

- 运行包 `PEHIME.EXE`（x64 与 32 位 `build\x86\PEHIME.EXE`）只依赖 Windows 系统 DLL，不链接这些开源库。
- 词库数据由上述数据/工具生成，是衍生数据；许可全文在 `tools/licenses/`。
- `tools/words/cn_words_curated.txt` 为项目补充词，非第三方词表。
