# tools 说明

本目录存放**构建期工具和数据**，运行时不需要。

## 编译器（w64devkit）

仓库不再直接携带编译器；`build_x64.cmd` / `build_x86.cmd` 发现编译器不存在时会自动调用本脚本。也可以手动运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\get_w64devkit.ps1
```

- 版本：**w64devkit 2.11.0**（内含 GCC 16.2.0 / MinGW-w64，可交叉编译 x64 与 x86 目标）
- 下载地址：<https://github.com/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x64-2.11.0.7z.exe>
- SHA256：`54c2bb9962e8a30c8270e34c5efc3d5fee579f53087fa7d435b95fbba2af7b2b`
- 解压位置：`tools\w64devkit\w64devkit\`（脚本会自动创建；编译完成后保留，不删除）
- 该目录与下载包已在根目录 `.gitignore` 中忽略，不会上传到 GitHub
- 校验：脚本会校验 SHA256，并检查 `bin\x86_64-w64-mingw32-gcc.exe` 是否可运行
- 32 位构建：`build_x86.cmd` 使用同一目录下的 `bin\i686-w64-mingw32-gcc.exe`，缺少时同样会触发自动下载

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

根目录的 `build_words.cmd` 每次都会重新生成中文词库：运行 `tools/words2dict.py` 在 `.tmp\`
生成 `pehime.dict` 与 `pehime_cn_dump.txt`，随后把 `.tmp\pehime.dict` 复制到
`build\x64\` 与 `build\x86\`（目录不存在则创建），复制后删除 `.tmp\pehime.dict`；
`.tmp\pehime_cn_dump.txt` 作为可读导出保留（`.tmp\` 已在 `.gitignore` 中忽略，供调试用）。

`build_words.cmd` 会检查 Python 3.8+，并检查 `jieba`、`pypinyin`、`wordfreq` 三个导入；
缺少依赖时提示「按任意键安装」，自动执行下面的命令后复查：

```cmd
python -m pip install jieba pypinyin wordfreq
```

两个可执行文件构建脚本 `build_x64.cmd` / `build_x86.cmd` 只编译 `PEHIME.EXE` 与 `ptest.exe`，
不生成、不复制词库，也不要求 Python；词库变化后重跑 `build_words.cmd` 即可。

## 其他

| 文件 | 用途 |
|---|---|
| `words2dict.py` | 生成 `.tmp/pehime.dict` 和 `.tmp/pehime_cn_dump.txt` |
| `ptest.c` | 拼音核心命令行测试（构建时编为 `build/x64/ptest.exe`，32 位为 `build/x86/ptest.exe`；调试输出 `.tmp/ptest_out.txt`） |
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

- 运行包 `build\x64\PEHIME.EXE` 与 32 位 `build\x86\PEHIME.EXE` 只依赖 Windows 系统 DLL，不链接这些开源库。
- 词库数据由上述数据/工具生成，是衍生数据；许可全文在 `tools/licenses/`。
- `tools/words/cn_words_curated.txt` 为项目补充词，非第三方词表。
