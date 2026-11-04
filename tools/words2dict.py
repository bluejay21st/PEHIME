#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PEIM 词库生成器（words2dict.py）—— 一次生成中文 + 英文两个词库

配置在文件头部：
  CN_WORD_FILES  中文补充词文件列表（基础词表仍来自 jieba 自带 dict.txt）
  EN_WORD_FILES  英文词文件列表（第 1 个 = 按频率排序的常用词源，其余 = 补充词）

产物：
  build/peim_cn.dict       中文词库（C 直接读取）
  build/peim_cn_dump.txt   中文词库可读导出（调试用）
  build/peim_en.dict       英文联想词库

中文数据来源：
  - 单字/词：jieba 自带 dict.txt（MIT）
  - 拼音：pypinyin（pinyin-data，MIT）
  - 排序：wordfreq 通用词频（zipf），缺失时退回 jieba 词频
英文数据来源：
  - tools/words/en_words_freq.txt（Google 10k 常用词，按频率排序）
  - tools/words/en_words_curated.txt（项目补充词，最高权重）

中文格式 v1（小端）：
  u32 magic 'PEIM'; u16 version=1; u16 syllable_count; u32 entry_count
  音节表：u8 len + ASCII 名称（按字典序升序）
  词条：u8 syl_count + u16 syl_ids[] + u16 weight + u8 text_len + u16 text[]（UTF-16LE）

英文格式 v1（小端）：
  u32 magic 'PEEN'; u16 version=1; u32 count
  u32 offsets[count]（相对词条区偏移）
  词条区：u8 len + ASCII 小写词 + u16 weight（按字典序升序）
"""
import math
import os
import re
import struct
import sys

import jieba
import pypinyin

try:
    from wordfreq import zipf_frequency as _zipf
except Exception:
    _zipf = None

# =====================================================================
# 词库文件配置：分别列出 CN / EN 两组文件
# =====================================================================
CN_WORD_FILES = [
    "tools/words/cn_words_curated.txt",
]
EN_WORD_FILES = [
    "tools/words/en_words_freq.txt",       # 英文常用词源（按频率排序）
    "tools/words/en_words_curated.txt",    # 英文补充词（最高权重）
]

CN_DICT = "build/peim_cn.dict"
CN_DUMP = "build/peim_cn_dump.txt"
EN_DICT = "build/peim_en.dict"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CJK_MIN, CJK_MAX = 0x4E00, 0x9FFF

# 常见多音字的常用次读音：即使词库证据为零也保留（否则"地(de)""曾(zeng)"会丢）
COMMON_READINGS = {
    ("地", "de"), ("这", "zhei"), ("那", "nei"), ("哪", "nei"), ("和", "hu"),
    ("系", "ji"), ("提", "di"), ("省", "xian"), ("属", "zhu"), ("车", "ju"),
    ("区", "ou"), ("万", "mo"), ("种", "chong"), ("乾", "gan"), ("见", "xian"),
    ("仇", "qiu"), ("单", "shan"), ("曾", "zeng"), ("解", "xie"), ("查", "zha"),
    ("朴", "piao"), ("翟", "zhai"), ("覃", "qin"), ("号", "xiao"), ("乐", "yue"),
    ("行", "hang"), ("长", "zhang"), ("重", "chong"), ("还", "huan"), ("便", "pian"),
    ("着", "zhao"), ("着", "zhuo"), ("得", "dei"), ("的", "di"), ("了", "liao"),
    ("石", "dan"), ("说", "shui"), ("似", "shi"), ("弹", "tan"), ("帖", "tie"),
    ("应", "ying"), ("载", "zai"), ("只", "zhi"), ("转", "zhuan"), ("传", "zhuan"),
    ("创", "chuang"), ("大", "dai"), ("逮", "dai"), ("担", "dan"), ("度", "duo"),
    ("缝", "feng"), ("否", "pi"), ("脯", "pu"), ("盖", "ge"), ("颈", "geng"),
    ("广", "an"), ("横", "heng"), ("哄", "hong"), ("划", "hua"), ("晃", "huang"),
    ("几", "ji"), ("尽", "jin"), ("劲", "jing"), ("卷", "juan"), ("累", "lei"),
    ("俩", "lia"), ("凉", "liang"), ("淋", "lin"), ("领", "ling"), ("笼", "long"),
    ("女", "nv"), ("暖", "nuan"), ("虐", "nve"), ("片", "pian"), ("漂", "piao"),
    ("屏", "bing"), ("铺", "pu"), ("岂", "kai"), ("悄", "qiao"), ("亲", "qing"),
    ("曲", "qu"), ("圈", "juan"), ("任", "ren"), ("散", "san"), ("舍", "she"),
    ("数", "shuo"), ("宿", "xiu"), ("缩", "su"), ("拓", "ta"), ("吐", "tu"),
    ("瓦", "wa"), ("为", "wei"), ("遗", "wei"), ("与", "yu"), ("扎", "zha"),
    ("占", "zhan"), ("涨", "zhang"), ("正", "zheng"), ("刹", "sha"), ("禅", "shan"),
    ("颤", "zhan"), ("场", "chang"), ("朝", "zhao"), ("乘", "sheng"), ("畜", "chu"),
    ("刺", "ci"), ("答", "da"), ("钉", "ding"), ("发", "fa"), ("坊", "fang"),
    ("父", "fu"), ("盖", "gai"), ("勾", "gou"), ("估", "gu"), ("骨", "gu"),
    ("哈", "ha"), ("汗", "han"), ("喝", "he"), ("荷", "he"), ("可", "ke"),
    ("苦", "ku"), ("夸", "kua"), ("擂", "lei"), ("连", "lian"), ("裂", "lie"),
    ("溜", "liu"), ("麻", "ma"), ("靡", "mi"), ("难", "nan"), ("宁", "ning"),
    ("拧", "ning"), ("弄", "long"), ("偶", "ou"), ("排", "pai"), ("胖", "pan"),
    ("炮", "pao"), ("喷", "pen"), ("撇", "pie"), ("扑", "pu"), ("期", "ji"),
    ("枪", "qiang"), ("强", "jiang"), ("雀", "qiao"), ("丧", "sang"), ("森", "sen"),
    ("杀", "sha"), ("硕", "shi"), ("丝", "si"), ("松", "song"), ("伺", "ci"),
    ("同", "tong"), ("托", "tuo"), ("娃", "wa"), ("歪", "wai"), ("王", "wang"),
    ("於", "wu"), ("晕", "yun"), ("择", "zhai"), ("粘", "nian"), ("折", "she"),
    # 生僻但真实存在的音节（保证打字能出来正确的字）
    ("嗯", "ng"), ("哼", "hng"), ("咯", "lo"), ("呒", "m"), ("呣", "m"),
    ("嗲", "dia"), ("忒", "tei"), ("刻", "kei"), ("撮", "chua"), ("啉", "len"),
}

# ü 的两种输入习惯：lve/lue、nve/nue 都要能用
SYL_ALIAS = {"lve": ["lve", "lue"], "nve": ["nve", "nue"]}
WORD_RE = re.compile(r"^[a-z]{2,16}$")


# =====================================================================
# 通用工具
# =====================================================================
def path_of(rel):
    return os.path.join(ROOT, rel.replace("/", os.sep))


def syl_variants(syls):
    out = [[]]
    for s in syls:
        alts = SYL_ALIAS.get(s, [s])
        out = [prev + [a] for prev in out for a in alts]
    return out


def is_gb2312(ch: str) -> bool:
    try:
        ch.encode("gb2312")
        return True
    except UnicodeEncodeError:
        return False


def read_jieba_dict():
    path = os.path.join(os.path.dirname(jieba.__file__), "dict.txt")
    chars, words = {}, {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 2:
                continue
            w, fr = parts[0], parts[1]
            try:
                freq = int(fr)
            except ValueError:
                continue
            if len(w) == 1:
                chars[w] = freq
            elif len(w) in (2, 3, 4):
                words[w] = freq
    return chars, words


def to_pinyin(text: str):
    out = []
    for item in pypinyin.lazy_pinyin(
        text, style=pypinyin.Style.NORMAL, errors=lambda x: None
    ):
        if not item or not item.isalpha() or not item.isascii():
            return None
        out.append(item.lower())
    return out


def read_curated_files(files):
    """中文补充词：每行 '词 [拼音]'，拼音音节用 ' 分隔。"""
    out = {}
    for rel in files:
        p = path_of(rel)
        if not os.path.exists(p):
            continue
        with open(p, encoding="utf-8") as f:
            for line in f:
                line = line.split("#")[0].strip()
                if not line:
                    continue
                parts = line.split()
                w = parts[0]
                if len(w) not in (2, 3, 4):
                    continue
                if len(parts) > 1 and "'" in parts[1]:
                    syls = tuple(s.lower() for s in parts[1].split("'"))
                    if len(syls) == len(w):
                        out[w] = syls
                    continue
                out.setdefault(w, None)
    return out


def read_words(rel):
    """英文词：一行一个，只保留 2~16 个小写字母。"""
    p = path_of(rel)
    out = []
    if not os.path.exists(p):
        return out
    with open(p, encoding="utf-8", errors="ignore") as f:
        for line in f:
            w = line.strip().lower()
            if WORD_RE.match(w):
                out.append(w)
    return out


def zipf_weight(text):
    """通用词频 → u16 权重；没有数据返回 None"""
    if _zipf is None:
        return None
    z = _zipf(text, "zh")
    if z <= 0:
        return None
    return max(1, min(65535, int(round(65535.0 * (z - 1.0) / 7.0))))


def fallback_weight(freq):
    """jieba 词频兜底（和 wordfreq 权重大致同量级）"""
    if freq <= 0:
        return 1
    return max(1, min(65535, int(round(8000.0 * math.log10(1.0 + freq)))))


# =====================================================================
# 中文词库
# =====================================================================
def build_entries(chars, words, max_words, max_3words, max_4words, curated):
    word_entries = []          # (syls, text, freq, kind)
    evidence = {}              # (字, 音节) -> 词频累计，判断多音字读音是否常用
    dropped_map = {}           # 音节 -> 一个字，保证音节表不丢（生僻音节保留最低权重）

    def add_word(text, syls, freq):
        for variant in syl_variants(syls):
            word_entries.append((tuple(variant), text, freq, "word"))
        for i, ch in enumerate(text):
            key = (ch, syls[i])
            evidence[key] = evidence.get(key, 0) + freq

    def add_jieba_words(word_len, limit):
        cand = []
        for w, freq in words.items():
            if len(w) != word_len:
                continue
            if not all(CJK_MIN <= ord(c) <= CJK_MAX and is_gb2312(c) for c in w):
                continue
            syls = to_pinyin(w)
            if not syls or len(syls) != word_len:
                continue
            cand.append((freq, w, tuple(syls)))
        cand.sort(key=lambda x: -x[0])
        seen = set()
        n = 0
        for freq, w, syls in cand:
            if w in seen:
                continue
            seen.add(w)
            add_word(w, syls, freq)
            n += 1
            if n >= limit:
                break

    add_jieba_words(2, max_words)
    add_jieba_words(3, max_3words)
    add_jieba_words(4, max_4words)

    # 领域词表：高权重 + 也作为多音字证据
    for w, given in curated.items():
        syls = given if given else to_pinyin(w)
        if not syls or len(syls) != len(w):
            continue
        add_word(w, tuple(syls), 200000)

    # 每个音节下最大的"用词证据"，用于给次读音定权重
    syl_max_ev = {}
    for (ch, syl), ev in evidence.items():
        if ev > syl_max_ev.get(syl, 0):
            syl_max_ev[syl] = ev

    char_entries = []
    dropped = 0
    for ch, freq in chars.items():
        if len(ch) != 1 or not (CJK_MIN <= ord(ch) <= CJK_MAX):
            continue
        if not is_gb2312(ch):
            continue
        readings = pypinyin.pinyin(
            ch, style=pypinyin.Style.NORMAL, heteronym=True, errors=lambda x: None
        )
        if not readings or not readings[0]:
            continue
        primary = [r.lower() for r in readings[0] if r and r.isascii()]
        if not primary:
            continue
        uniq = list(dict.fromkeys(primary))

        def add_char(syl, kind, factor=None):
            for v in syl_variants([syl]):
                e = ((v[0],), ch, freq, kind)
                if kind == "secondary":
                    e = e + (factor,)
                char_entries.append(e)

        for idx, syl in enumerate(uniq):
            if idx == 0:
                add_char(syl, "primary")
                continue
            ev_sec = evidence.get((ch, syl), 0)
            ev_pri = evidence.get((ch, uniq[0]), 0)
            max_ev = syl_max_ev.get(syl, 0)
            wl = (ch, syl) in COMMON_READINGS
            if ev_sec <= 0:
                if wl:
                    add_char(syl, "secondary", 0.5)
                else:
                    dropped += 1
                    dropped_map.setdefault(syl, ch)
                continue
            # 相对该字主读音的实际使用比例；小比例说明是生僻读音
            ratio = float(ev_sec) / float(ev_pri) if ev_pri > 0 else \
                    (float(ev_sec) / float(max_ev) if max_ev > 0 else 0.0)
            if ratio < 0.01 and not wl:
                dropped += 1
                dropped_map.setdefault(syl, ch)
                continue
            factor = min(1.0, ratio ** 0.25)
            if wl and factor < 0.5:
                factor = 0.5
            add_char(syl, "secondary", factor)

    return word_entries, char_entries, dropped, dropped_map


def build_cn():
    chars, words = read_jieba_dict()
    curated = read_curated_files(CN_WORD_FILES)
    print(f"[CN] jieba: 单字 {len(chars)}，二字/三字/四字词 {len(words)}；补充词 {len(curated)} 条")

    word_entries, char_entries, dropped, dropped_map = build_entries(
        chars, words, 40000, 15000, 8000, curated
    )
    curated_set = set(curated.keys())

    merged = {}
    for entry in word_entries + char_entries:
        syls, text, freq, kind = entry[0], entry[1], entry[2], entry[3]
        factor = entry[4] if len(entry) > 4 else 1.0
        if kind == "primary":
            weight = zipf_weight(text)
            if weight is None:
                weight = fallback_weight(freq)
        elif kind == "secondary":
            base = zipf_weight(text)
            if base is None:
                base = fallback_weight(freq)
            weight = max(1, int(base * factor))
        else:
            weight = zipf_weight(text)
            if weight is None:
                weight = fallback_weight(freq)
        if text in curated_set:
            weight = 65535
        key = (tuple(syls), text)
        if key not in merged or weight > merged[key]:
            merged[key] = weight

    # 补回因过滤而消失的生僻音节
    used_syl = {s for (syls, _text) in merged for s in syls}
    restored = 0
    for syl, ch in dropped_map.items():
        if syl not in used_syl:
            key = ((syl,), ch)
            if key not in merged:
                merged[key] = 1
                used_syl.add(syl)
                restored += 1

    groups = {}
    for (syls, text), weight in merged.items():
        groups.setdefault(syls, []).append((weight, text))

    syllables = sorted({s for syls in groups for s in syls})
    syl_id = {s: i for i, s in enumerate(syllables)}

    records = []
    for syls, items in groups.items():
        ids = tuple(syl_id[s] for s in syls)
        for weight, text in items:
            records.append((ids, weight, text))
    records.sort(key=lambda r: r[0])

    out = [b"PEIM", struct.pack("<HHI", 1, len(syllables), len(records))]
    for s in syllables:
        b = s.encode("ascii")
        out.append(struct.pack("<B", len(b)) + b)
    for ids, weight, text in records:
        assert 1 <= len(ids) <= 8, f"音节数异常: {text}"
        assert len(text) <= 8
        out.append(struct.pack("<B", len(ids)))
        out.append(struct.pack("<%dH" % len(ids), *ids))
        out.append(struct.pack("<H", weight))
        out.append(struct.pack("<B", len(text)))
        out.append(text.encode("utf-16-le"))
    blob = b"".join(out)

    dict_path = path_of(CN_DICT)
    dump_path = path_of(CN_DUMP)
    os.makedirs(os.path.dirname(dict_path), exist_ok=True)
    with open(dict_path, "wb") as f:
        f.write(blob)
    with open(dump_path, "w", encoding="utf-8") as f:
        f.write(f"# entries={len(records)} syllables={len(syllables)} bytes={len(blob)}\n")
        for ids, weight, text in records:
            f.write("%s\t%s\t%d\n" % ("'".join(syllables[i] for i in ids), text, weight))

    print(f"[CN] 音节 {len(syllables)}，词条 {len(records)}，文件 {len(blob)} 字节 ({len(blob)/1024:.1f} KB)")
    print(f"[CN] 多音字次读音：丢弃 {dropped}，补回生僻音节 {restored}")
    for probe in ("zhongguo", "nihao", "jisuanji", "cipanguanli", "bang"):
        hits = [(w, t) for ids, w, t in records if "".join(syllables[i] for i in ids) == probe]
        hits.sort(key=lambda x: -x[0])
        print(f"  [CN] {probe}: {''.join(t for _, t in hits[:6]) or '（无）'}")
    return len(records)


# =====================================================================
# 英文词库
# =====================================================================
def build_en():
    freq_files = EN_WORD_FILES[:1]
    curated_files = EN_WORD_FILES[1:]

    raw = []
    for rel in freq_files:
        raw.extend(read_words(rel))
    if not raw:
        print("警告: 未找到英文常用词源（%s）" % ", ".join(freq_files))

    curated = []
    for rel in curated_files:
        curated.extend(read_words(rel))

    weights = {}
    n = max(1, len(raw))
    for i, w in enumerate(raw):
        weight = max(1, 65535 - int(50000 * i / n))
        if weight > weights.get(w, 0):
            weights[w] = weight
    for w in curated:
        weights[w] = 65535

    words = sorted(weights.keys())
    entries = []
    offsets = []
    off = 0
    for w in words:
        b = w.encode("ascii")
        offsets.append(off)
        entries.append(struct.pack("<B", len(b)) + b + struct.pack("<H", weights[w]))
        off += 1 + len(b) + 2

    blob = bytearray(b"PEEN")
    blob += struct.pack("<HI", 1, len(words))
    for o in offsets:
        blob += struct.pack("<I", o)
    for e in entries:
        blob += e

    out_path = path_of(EN_DICT)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(blob)
    print("[EN] 词条 %d，文件 %d 字节 (%.1f KB)" % (len(words), len(blob), len(blob) / 1024))
    return len(words)


def main():
    print("words2dict: 生成中文 + 英文词库")
    cn = build_cn()
    en = build_en()
    print("完成：CN=%d 条，EN=%d 条" % (cn, en))
    return 0


if __name__ == "__main__":
    sys.exit(main())
