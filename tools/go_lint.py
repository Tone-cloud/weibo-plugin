#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Go 静态体检（无工具链时的兜底）。

重点查三类「编译必然失败」的问题：
  1. 未使用的 import
  2. 未使用的局部变量（:= / var 声明后全文件再无引用）
  3. 调用了本包内不存在的函数（跨文件对账）

用法: python3 tools/go_lint.py
"""

from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GO_DIR = os.path.join(ROOT, "go_server", "main")

BUILTINS = {
    "append", "bool", "byte", "cap", "close", "complex", "complex64", "complex128",
    "copy", "delete", "error", "float32", "float64", "imag", "int", "int8", "int16",
    "int32", "int64", "len", "make", "new", "panic", "print", "println", "real",
    "recover", "rune", "string", "uint", "uint8", "uint16", "uint32", "uint64",
    "uintptr", "any", "min", "max", "clear", "true", "false", "nil", "iota",
    "if", "for", "switch", "select", "go", "defer", "return", "func", "range",
    "case", "else", "break", "continue", "fallthrough", "var", "const", "type",
    "struct", "interface", "map", "chan", "import", "package", "goto",
}

# 只做「去注释 + 去字符串」处理，保留结构
def strip_comments_and_strings(src: str) -> str:
    out = []
    i, n = 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        ch = src[i]
        if ch == "`":  # raw string
            j = src.find("`", i + 1)
            i = n if j < 0 else j + 1
            out.append(" ")
            continue
        if ch in "\"'":
            q = ch
            i += 1
            while i < n:
                if src[i] == "\\":
                    i += 2
                    continue
                if src[i] == q:
                    i += 1
                    break
                i += 1
            out.append(" ")
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def load() -> dict[str, tuple[str, str]]:
    files = {}
    if not os.path.isdir(GO_DIR):
        print(f"目录不存在: {GO_DIR}")
        sys.exit(1)
    for name in sorted(os.listdir(GO_DIR)):
        if not name.endswith(".go"):
            continue
        raw = open(os.path.join(GO_DIR, name), encoding="utf-8", errors="replace").read()
        files[name] = (raw, strip_comments_and_strings(raw))
    return files


def check_unused_imports(files) -> list[str]:
    problems = []
    for name, (raw, clean) in files.items():
        # 抓 import 段
        blocks = re.findall(r"^import\s*\((.*?)\)", raw, re.S | re.M)
        entries = []
        for b in blocks:
            for line in b.splitlines():
                line = line.split("//")[0].strip()
                if not line:
                    continue
                m = re.match(r'(?:(\w+|\.|_)\s+)?"([^"]+)"', line)
                if not m:
                    continue
                alias, path = m.group(1), m.group(2)
                entries.append((alias, path))
        for m in re.finditer(r'^import\s+(?:(\w+|\.|_)\s+)?"([^"]+)"', raw, re.M):
            entries.append((m.group(1), m.group(2)))

        body = re.sub(r"^import\s*\(.*?\)", "", clean, flags=re.S | re.M)
        body = re.sub(r'^import\s+(?:\w+\s+)?"[^"]+"', "", body, flags=re.M)

        for alias, path in entries:
            if alias == "_":
                continue
            pkg = alias if alias else path.split("/")[-1]
            if alias == ".":
                continue
            if not re.search(rf"\b{re.escape(pkg)}\s*\.", body):
                problems.append(f"{name}: 未使用的 import \"{path}\"（包名 {pkg}）")
    return problems


def check_unused_locals(files) -> list[str]:
    problems = []
    for name, (raw, clean) in files.items():
        for m in re.finditer(r"\b(\w+)\s*:=", clean):
            var = m.group(1)
            if var == "_":
                continue
            rest = clean[m.end():]
            if not re.search(rf"\b{re.escape(var)}\b", rest):
                line = clean[: m.start()].count("\n") + 1
                problems.append(f"{name}:{line}: 局部变量 {var} 声明后未再使用（Go 编译错误）")
    return problems


def check_package_identifiers(files) -> list[str]:
    defined_funcs, defined_types, defined_vars = set(), set(), set()
    for name, (raw, clean) in files.items():
        defined_funcs |= set(re.findall(r"^func\s+(?:\([^)]*\)\s*)?(\w+)\s*\(", clean, re.M))
        defined_types |= set(re.findall(r"^type\s+(\w+)\s", clean, re.M))
        defined_vars |= set(re.findall(r"^(?:var|const)\s+(\w+)\s", clean, re.M))
        for blk in re.findall(r"^(?:var|const)\s*\((.*?)\)", clean, re.S | re.M):
            for line in blk.splitlines():
                mm = re.match(r"\s*(\w+)\s", line)
                if mm:
                    defined_vars.add(mm.group(1))

    known = defined_funcs | defined_types | defined_vars | BUILTINS
    problems = []
    for name, (raw, clean) in files.items():
        # 所有「非方法」调用
        for m in re.finditer(r"(?<![\w.])([a-z]\w*)\s*\(", clean):
            fn = m.group(1)
            if fn in known:
                continue
            # 可能是局部变量/参数持有函数，或类型转换；检查是否在文件里被声明过
            if re.search(rf"\b{re.escape(fn)}\b\s*(?::=|,|\s+\w+\s*:?=|\w+\s*$)", clean, re.M):
                continue
            if re.search(rf"\b(?:var|const)\s+{re.escape(fn)}\b", clean):
                continue
            if re.search(rf"func\s*\([^)]*\)\s*{re.escape(fn)}\b", clean):
                continue
            # 变量赋值目标 / 参数名
            if re.search(rf"[,\s(]{re.escape(fn)}\s*[,)]", clean):
                continue
            if re.search(rf"\b{re.escape(fn)}\s*:=", clean):
                continue
            line = clean[: m.start()].count("\n") + 1
            problems.append(f"{name}:{line}: 调用了未定义的 {fn}()（本包内没找到声明，需人工确认）")

    # 重复声明
    seen = {}
    for name, (raw, clean) in files.items():
        for f in re.findall(r"^func\s+(\w+)\s*\(", clean, re.M):
            seen.setdefault(f, []).append(name)
    for f, where in sorted(seen.items()):
        if len(where) > 1:
            problems.append(f"{f} 被重复定义于: {', '.join(where)}")
    return problems


def check_misc(files) -> list[str]:
    problems = []
    mains = [n for n, (_r, c) in files.items() if re.search(r"^func\s+main\s*\(", c, re.M)]
    if len(mains) != 1:
        problems.append(f"func main() 出现 {len(mains)} 次: {mains}")
    for name, (raw, clean) in files.items():
        if not re.match(r"^package\s+main\b", clean.strip()):
            problems.append(f"{name}: 不是 package main")
        # %s/%d 与参数个数（粗查 fmt.Sprintf/Printf/Errorf/Logf 等）
        for m in re.finditer(r"\b(?:Sprintf|Printf|Errorf|Fatalf|Logf|Warnf|Infof|Debugf|printf|logf)\s*\(([^;]*?)\)\s*$",
                             clean, re.M):
            pass
    return problems


def main() -> int:
    files = load()
    print(f"Go 文件: {len(files)} 个 —— {', '.join(sorted(files))}\n")
    groups = [
        ("未使用的 import", check_unused_imports(files)),
        ("未使用的局部变量", check_unused_locals(files)),
        ("标识符 / 重复定义", check_package_identifiers(files)),
        ("其它", check_misc(files)),
    ]
    total = 0
    for title, items in groups:
        print(f"== {title}: {len(items)} 条")
        for it in items:
            print("   " + it)
        total += len(items)
        print()
    print(f"合计 {total} 条待人工确认")
    return 0


if __name__ == "__main__":
    sys.exit(main())
