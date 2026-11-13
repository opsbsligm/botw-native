# SPDX-License-Identifier: MIT
"""BOTW SDK7 静态 mod：原始版本门禁、应用保留区和生成/打包所用的同一字节快照。"""
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

from code_mods import PreparedMods, decode_nso, prepare_modules
from legacy_sdk7 import PINS
from mods_snapshot import directory, manifest, publish, read_file, snapshot_exefs, snapshot_patches
from nso import (Nso, DT_NULL, DT_HASH, DT_GNU_HASH, DT_STRTAB, DT_STRSZ, DT_SYMTAB,
                 DT_RELA, DT_RELASZ, DT_JMPREL, DT_PLTRELSZ, DT_RELR, DT_RELRSZ)

ROOT = Path(__file__).resolve().parents[1]
RECORD = 'static-mods.json'


def reserved_ranges(raw, name, hle_symbols):
    """保护加载/符号/重定位/函数发现元数据，以及会被宿主 HLE/时钟替换的位置。

    只解析已通过 PINS 的原始模块。补丁不能修改这些字段后再诱导生成器解析它们。
    返回标准解压 NSO 文件偏移（内存偏移 + 0x100）。
    """
    from analyze import read_encoded
    from legacy_clock import GUARDS
    image = decode_nso(raw)
    nso = Nso(name, raw=raw)
    ranges = []

    def add(start, length):
        if not length:
            return
        if start < 0 or length < 0 or start + length > len(image.data) - 0x100:
            raise ValueError('原始模块的保留元数据越界：' + name)
        ranges.append((start + 0x100, start + 0x100 + length))

    add(0, 8)  # 模块起始分支和 MOD0 指针。
    add(nso.u32(4), 28)
    cursor = nso.dynamic
    while cursor + 16 <= len(nso.image):
        tag = nso.u64(cursor)
        cursor += 16
        if tag == DT_NULL:
            break
    else:
        raise ValueError('原始模块 dynamic 缺少结束标记')
    add(nso.dynamic, cursor - nso.dynamic)
    if DT_STRTAB in nso.dyn:
        if DT_STRSZ not in nso.dyn:
            raise ValueError('原始模块缺少符号字符串表尺寸')
        add(nso.dyn[DT_STRTAB], nso.dyn[DT_STRSZ])
    if DT_SYMTAB in nso.dyn:
        add(nso.dyn[DT_SYMTAB], 24 * len(nso.syms))
    if DT_HASH in nso.dyn:
        start = nso.dyn[DT_HASH]
        add(start, 8 + 4 * (nso.u32(start) + nso.u32(start + 4)))
    if DT_GNU_HASH in nso.dyn:
        start = nso.dyn[DT_GNU_HASH]
        buckets, first, bloom = struct.unpack_from('<III', nso.image, start)
        add(start, 16 + 8 * bloom + 4 * buckets + 4 * max(0, len(nso.syms) - first))
    for start_tag, size_tag in ((DT_RELA, DT_RELASZ), (DT_JMPREL, DT_PLTRELSZ), (DT_RELR, DT_RELRSZ)):
        if start_tag in nso.dyn:
            add(nso.dyn[start_tag], nso.dyn.get(size_tag, 0))
    # NSO 头记录的 dynstr/dynsym 范围相对于 rodata。
    for field in (0x90, 0x98):
        offset, size = struct.unpack_from('<II', raw, field)
        if size:
            add(nso.rodata[0] + offset, size)
    if nso.eh_start:
        add(nso.eh_start, nso.eh_end - nso.eh_start)
        hdr = nso.eh_start
        _, pointer, counter, table = nso.image[hdr:hdr + 4]
        _, size = read_encoded(nso.image, hdr + 4, pointer, hdr)
        at = hdr + 4 + size
        count, size = read_encoded(nso.image, at, counter, hdr)
        at += size
        if count < 0 or count > len(nso.image) // 8:
            raise ValueError('原始模块的异常表计数越界')
        for _ in range(count):
            _, size = read_encoded(nso.image, at, table, hdr)
            at += size
            fde, size = read_encoded(nso.image, at, table, hdr)
            at += size
            cie = fde + 4 - nso.u32(fde + 4)
            for record in (fde, cie):
                length = nso.u32(record)
                if length == 0xFFFFFFFF:
                    raise ValueError('未支持的 64 位异常记录')
                add(record, 4 + length)
        add(hdr, at - hdr)
    for offset, *_ in nso.relocs:
        # BSS 没有可打补丁的文件内容；已加载段内的重定位目标会被 loader 重写。
        if any(left <= offset + 0x100 and offset + 0x108 <= right for left, right in image.spans):
            add(offset, 8)
    functions = sorted({value for _, info, shndx, value, _ in nso.syms
                        if shndx and info & 15 == 2 and nso.text[0] <= value < sum(nso.text)})
    for symbol, info, shndx, value, size in nso.syms:
        if shndx and symbol in hle_symbols and info & 15 == 2 and value in functions:
            end = value + size if size else next((a for a in functions if a > value), sum(nso.text))
            add(value, end - value)
    if name == 'main':
        from legacy_sdk7 import PROJECTION_FUNCTIONS
        for start, end, _ in PROJECTION_FUNCTIONS:
            add(start, end - start)
        for offset in GUARDS:
            add(offset, 4)
    # 合并相邻区间，避免大量重定位导致每条补丁逐个遍历整个符号表。
    merged = []
    for left, right in sorted(ranges):
        if merged and left <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(right, merged[-1][1]))
        else:
            merged.append((left, right))
    return tuple(merged)


@dataclass(frozen=True)
class ModContext:
    originals: dict[str, bytes]
    prepared: PreparedMods


def prepare(exefs, mods):
    originals = snapshot_exefs(exefs, PINS)
    hle_symbols = {line.strip() for line in (ROOT / 'runtime/hle/hle_symbols.txt').read_text().splitlines()
                   if line.strip() and not line.startswith('#')}
    hle_symbols.update(('setjmp', '_setjmp', '__setjmp'))
    protected = {name: reserved_ranges(raw, name, hle_symbols if name != 'main' else {'setjmp', '_setjmp', '__setjmp'})
                 for name, raw in originals.items()}
    prepared = prepare_modules(originals, snapshot_patches(mods), protected=protected)
    return ModContext(originals, prepared)


def identity(prepared):
    record = manifest(prepared)
    digest = hashlib.sha256(json.dumps(record, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    return digest, record


def verify_published(target, prepared):
    expected = manifest(prepared)
    with directory(target) as fd:
        if set(os.listdir(fd)) != set(prepared.modules) | {'manifest.json'}:
            raise ValueError('已准备模块清单变化')
        if json.loads(read_file(fd, 'manifest.json', 2**20)) != expected:
            raise ValueError('已准备模块回执变化')
        for name, raw in prepared.modules.items():
            actual = read_file(fd, name, len(raw))
            if actual != raw:
                raise ValueError('已准备模块内容变化：' + name)
    return expected


def publish_context(context, outdir):
    outdir = Path(outdir)
    digest, record = identity(context.prepared)
    store = outdir / 'static-mods'
    store.mkdir(exist_ok=True)
    target = store / digest
    try:
        publish(context.prepared, target)
    except FileExistsError:
        verify_published(target, context.prepared)
    record = {**record, 'directory': 'static-mods/' + digest}
    fd, temporary = tempfile.mkstemp(prefix='.static-mods-', dir=outdir)
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(record, stream, sort_keys=True, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, outdir / RECORD)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return record


def verify_context(generated, exefs, mods):
    """打包重新检查原始版本、补丁与准备好的全部字节，不因回执存在而信任它。"""
    generated = Path(generated)
    context = prepare(exefs, mods)
    digest, expected = identity(context.prepared)
    expected = {**expected, 'directory': 'static-mods/' + digest}
    if json.loads((generated / RECORD).read_text()) != expected:
        raise ValueError('静态 mod 输入与生成阶段不同；必须重新生成和编译')
    verify_published(generated / expected['directory'], context.prepared)
    return context.prepared, expected
