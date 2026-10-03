#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""找出「lambda 里用了 this 却没捕获 this」的地方。

CI 上 GCC 的真实报错（已实际踩过）：
    error: 'this' was not captured for this lambda function
    error: cannot call member function 'void WeiboSearchModule::searchFinished(
               const QString&, int)' without object
即 lambda 里写了 `emit someMemberSignal(...)` 或裸的 `m_xxx`，但捕获列表里没有 this。

被 tools/verify.py 的 S 项复用，也可以单独跑：
    python3 tools/lambda_this.py
"""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

# 允许在 lambda 里裸发的名字（这些是别的对象，不是 this 的成员）
NOT_MEMBER = {"self", "m_controller", "guard", "controller"}

_M_LAMBDA = re.compile(r"\[([^\[\]]{0,160})\]\s*(?:\([^()]{0,300}\))?\s*(?:mutable\s*)?\{")


def strip_comments_strings(src: str) -> str:
    """去注释与字符串字面量（字符串不跨行）。"""
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
        if ch in "\"'":
            q = ch
            i += 1
            while i < n:
                if src[i] == "\n":
                    break
                if src[i] == "\\" and i + 1 < n and src[i + 1] != "\n":
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


def find_lambdas(src: str):
    """返回 [(capture_text, body_start, body_end, capture_start)]。"""
    lams = []
    for m in _M_LAMBDA.finditer(src):
        cap = m.group(1)
        # 排除下标表达式 / 初始化列表：真正的捕获列表里不会出现这些
        if re.search(r"[;=]", cap) or '"' in cap or "'" in cap:
            continue
        start = m.end() - 1
        depth = 0
        for j in range(start, len(src)):
            if src[j] == "{":
                depth += 1
            elif src[j] == "}":
                depth -= 1
                if depth == 0:
                    lams.append((cap, start, j, m.start()))
                    break
    return lams


def find_problems() -> list[str]:
    """确定性的问题列表（这些在 GCC 下必然是编译错误）。"""
    problems: list[str] = []
    if not os.path.isdir(SRC):
        return problems
    for dirpath, _dirs, files in os.walk(SRC):
        for name in sorted(files):
            if not name.endswith((".cpp", ".h", ".hpp")):
                continue
            path = os.path.join(dirpath, name)
            src = strip_comments_strings(open(path, encoding="utf-8").read())
            rel = os.path.relpath(path, ROOT).replace("\\", "/")
            for cap, bstart, bend, cstart in find_lambdas(src):
                if re.search(r"\bthis\b", cap):
                    continue
                body = src[bstart:bend]
                # 1) 裸 emit 成员信号
                for em in re.finditer(r"\bemit\s+(\w+)\s*\(", body):
                    sig = em.group(1)
                    if sig in NOT_MEMBER:
                        continue
                    ln = src[: bstart + em.start()].count("\n") + 1
                    problems.append(
                        f"{rel}:{ln} lambda([{cap}]) 里 `emit {sig}(...)` 但没有捕获 this"
                    )
                # 2) 裸的成员变量（排除 self->m_x / obj.m_x）
                seen = set()
                for mm in re.finditer(r"(?<![\w.>])m_\w+", body):
                    tok = mm.group(0)
                    if tok in seen:
                        continue
                    seen.add(tok)
                    ln = src[: bstart + mm.start()].count("\n") + 1
                    problems.append(
                        f"{rel}:{ln} lambda([{cap}]) 里用了 {tok} 但没有捕获 this"
                    )
    return problems


def main() -> int:
    problems = find_problems()
    if not problems:
        print("OK: 没有发现「lambda 缺 this」的问题")
        return 0
    for p in problems:
        print("  " + p)
    print(f"\n共 {len(problems)} 条（GCC 下都是硬编译错误）")
    return 1


if __name__ == "__main__":
    sys.exit(main())
