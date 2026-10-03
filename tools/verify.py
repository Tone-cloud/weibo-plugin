#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
WeiboPocket 静态一致性校验。

本地没有 aarch64 Qt / Go 工具链，所以 CI 之前先跑这个脚本做「结构 + 契约」检查。
它不能替代编译，但能挡住绝大多数低级错误：

  A  metadata.json 与打包布局
  B  weibo_plugin.pro 里声明的每个源文件都真实存在
  C  qml/qmldir 里声明的每个文件都真实存在，且 components/ pages/ 下的文件都被登记
  D  每个 QML 文件括号平衡
  E  每个 page 都声明了 controller 属性
  F  C++：头文件里声明的成员函数在 .cpp 里都有定义（粗粒度）
  G  C++：Q_INVOKABLE / Q_PROPERTY 的 READ 访问器都有定义
  H  Go：只使用标准库
  I  Go：routes.go 注册的每个 handler 都有定义；SPEC 里的路由都注册了
  J  .github/workflows/build.yml 的 YAML 结构（tab / 缩进 / 键值 / needs）
  K  QML：用到 Theme 的文件必须有 `import ".."`；用到组件的页面必须有
     `import "../components"`

用法：
    python3 tools/verify.py            # 在仓库根目录执行
    python3 tools/verify.py --fix      # 先自动补齐 K 里缺失的 import，再校验
    python3 tools/verify.py --strict   # 警告也视为失败
退出码 0 = 通过，1 = 有错误。
"""

from __future__ import annotations

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STRICT = "--strict" in sys.argv

errors: list[str] = []
warnings: list[str] = []


def err(msg: str) -> None:
    errors.append(msg)


def warn(msg: str) -> None:
    warnings.append(msg)


def log_ok(msg: str) -> None:
    """正向确认（打印成 [ok]），用于「这东西确实被查过且没问题」的项。"""
    print(f"  [ok  ] {msg}")


def rel(path: str) -> str:
    return os.path.relpath(path, ROOT).replace("\\", "/")


def read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


# ---------------------------------------------------------------- A: metadata
def check_metadata() -> None:
    path = os.path.join(ROOT, "metadata.json")
    if not os.path.exists(path):
        err("A: metadata.json 缺失")
        return
    try:
        meta = json.loads(read(path))
    except json.JSONDecodeError as exc:
        err(f"A: metadata.json 不是合法 JSON: {exc}")
        return

    for key in ("id", "name", "version", "author", "icon", "main_qml", "main_so"):
        if key not in meta:
            err(f"A: metadata.json 缺少字段 {key}")

    # main_qml 必须真实存在
    qml_name = meta.get("main_qml")
    if qml_name and not os.path.exists(os.path.join(ROOT, qml_name)):
        err(f"A: metadata.json 的 main_qml 指向不存在的文件: {qml_name}")

    # main_so 是编译产物（本地/CI 构建前不存在），改为与 .pro 的 TARGET 对账
    so_name = meta.get("main_so", "")
    pro_path = os.path.join(ROOT, "weibo_plugin.pro")
    if so_name and os.path.exists(pro_path):
        tm = re.search(r"^\s*TARGET\s*=\s*(\S+)", read(pro_path), re.M)
        if tm:
            expect = f"lib{tm.group(1)}.so"
            if so_name != expect:
                err(
                    f"A: metadata.json 的 main_so={so_name} 与 weibo_plugin.pro 的 "
                    f"TARGET 不一致，应为 {expect}"
                )
        else:
            warn("A: weibo_plugin.pro 里没找到 TARGET")
    if so_name and not os.path.exists(os.path.join(ROOT, so_name)):
        warn(f"A: {so_name} 尚未构建（构建产物，CI 打包 job 会再校验一次）")

    if not os.path.exists(os.path.join(ROOT, "icon.png")):
        err("A: icon.png 缺失")


# ---------------------------------------------------------------- B: .pro
def check_pro() -> None:
    path = os.path.join(ROOT, "weibo_plugin.pro")
    if not os.path.exists(path):
        err("B: weibo_plugin.pro 缺失")
        return
    text = read(path)
    # 把续行拼接起来
    joined = re.sub(r"\\\s*\n", " ", text)
    for block in ("SOURCES", "HEADERS"):
        m = re.search(rf"^{block}\s*\+?=\s*(.*?)(?=\n[A-Z_]|\Z)", joined, re.S | re.M)
        if not m:
            warn(f"B: {block} 段未找到")
            continue
        entries = re.findall(r"(src/[\w./+-]+)", m.group(1))
        if not entries:
            err(f"B: {block} 段没有解析到任何 src/ 路径")
        for entry in entries:
            if not os.path.exists(os.path.join(ROOT, entry)):
                err(f"B: {block} 声明了不存在的文件: {entry}")

    # 反向检查：src 下的 .cpp/.h 是否都被登记
    declared = set(re.findall(r"(src/[\w./+-]+\.(?:cpp|h|hpp))", joined))
    on_disk: set[str] = set()
    for dirpath, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for name in files:
            if name.endswith((".cpp", ".h", ".hpp")):
                on_disk.add(rel(os.path.join(dirpath, name)))
    for missing in sorted(on_disk - declared):
        if missing.endswith(".hpp"):
            continue  # 头文件里的 .hpp 工具头可以不进 HEADERS
        warn(f"B: {missing} 未写进 weibo_plugin.pro（xmake 用通配符不受影响，qmake 会漏编）")


# ---------------------------------------------------------------- C/D/E: qml
def check_qmldir() -> set[str]:
    """校验 QML 模块布局。

    目标布局（与 netease-music 真机验证过的布局一致）：
        qml/components/qmldir   ← singleton Theme + 全部组件
        qml/                    ← 没有 qmldir
        qml/pages/              ← 没有 qmldir（靠目录隐式导入）
    """
    qml_root = os.path.join(ROOT, "qml")
    comp_dir = os.path.join(qml_root, "components")
    registered: set[str] = set()

    if os.path.exists(os.path.join(qml_root, "qmldir")):
        err("C: qml/qmldir 不应存在（会与 components/qmldir 产生同名类型重复声明；"
            "Theme/组件的可见性统一由 components/qmldir 提供）")

    comp_qmldir = os.path.join(comp_dir, "qmldir")
    if not os.path.exists(comp_qmldir):
        err("C: qml/components/qmldir 缺失")
        return registered

    declared_theme = False
    for lineno, raw in enumerate(read(comp_qmldir).splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith("module "):
            continue
        parts = line.split()
        if len(parts) < 3:
            warn(f"C: components/qmldir:{lineno} 无法解析: {raw!r}")
            continue
        qml_path = parts[-1]
        registered.add(f"components/{qml_path}")
        if not os.path.exists(os.path.join(comp_dir, qml_path)):
            err(f"C: components/qmldir:{lineno} 指向不存在的文件: {qml_path}")
        if parts[0] == "singleton" and parts[1] == "Theme":
            declared_theme = True
    if not declared_theme:
        err("C: components/qmldir 没有声明 `singleton Theme 1.0 Theme.qml`")

    if not os.path.isdir(comp_dir):
        err("C: qml/components/ 目录缺失")
        return registered
    for name in sorted(os.listdir(comp_dir)):
        if not name.endswith(".qml"):
            continue
        if f"components/{name}" not in registered:
            err(f"C: components/{name} 没有登记到 qml/components/qmldir")

    page_dir = os.path.join(qml_root, "pages")
    if not os.path.isdir(page_dir):
        err("C: qml/pages/ 目录缺失")
    else:
        for name in sorted(os.listdir(page_dir)):
            if name.endswith(".qml"):
                registered.add(f"pages/{name}")
    return registered


def strip_js_and_strings(src: str) -> str:
    """去掉注释与字符串字面量，避免括号计数 / 声明解析被注释里的内容干扰。

    关键：字符串扫描**不允许跨行**。源码里出现一个孤立的引号（例如注释外的
    撇号、或把 `"` 当字符用）时，如果允许跨行就会把后面几行整段吞掉，
    导致行首锚点失效、定义"消失"。这里遇到换行就放弃当前字符串。
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        two = src[i : i + 2]
        if two == "//":
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if two == "/*":
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        ch = src[i]
        if ch in "\"'":
            quote = ch
            j = i + 1
            closed = False
            while j < n:
                if src[j] == "\n":
                    break
                if src[j] == "\\" and j + 1 < n and src[j + 1] != "\n":
                    j += 2
                    continue
                if src[j] == quote:
                    closed = True
                    j += 1
                    break
                j += 1
            i = j if closed else i + 1
            out.append(" ")
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def check_qml_files(registered: set[str] | None) -> None:
    qml_root = os.path.join(ROOT, "qml")
    if not os.path.isdir(qml_root):
        err("D: qml/ 目录缺失")
        return
    for dirpath, _dirs, files in os.walk(qml_root):
        for name in files:
            if not name.endswith(".qml"):
                continue
            full = os.path.join(dirpath, name)
            src = strip_js_and_strings(read(full))
            for open_ch, close_ch in (("{", "}"), ("(", ")"), ("[", "]")):
                if src.count(open_ch) != src.count(close_ch):
                    err(
                        f"D: {rel(full)} 括号不平衡: "
                        f"{open_ch}{src.count(open_ch)} vs {close_ch}{src.count(close_ch)}"
                    )
            # 根元素必须存在
            head = re.sub(r"^\s*(?:/\*.*?\*/\s*)?(?:import[^\n]*\n\s*)*", "", src, flags=re.S)
            if not head.strip():
                err(f"D: {rel(full)} 没有根元素")

    pages = os.path.join(qml_root, "pages")
    if os.path.isdir(pages):
        for name in sorted(os.listdir(pages)):
            if not name.endswith(".qml"):
                continue
            full = os.path.join(pages, name)
            src = read(full)
            if "property var controller" not in src:
                err(f"E: {rel(full)} 缺少 `property var controller`")
            # 契约要求页面不得自己 new 其它页面
            for other in sorted(os.listdir(pages)):
                if other == name or not other.endswith(".qml"):
                    continue
                cls = other[:-4]
                if re.search(rf"\b{cls}\s*\{{", src):
                    warn(f"E: {rel(full)} 直接实例化了其它页面 {cls}（契约要求走 rootRef.navigateTo）")


# ---------------------------------------------------------------- F/G: C++
CPP_FUNC_RE = re.compile(
    r"^\s*(?:virtual\s+)?(?:static\s+)?(?:inline\s+)?"
    r"(?P<ret>[A-Za-z_][\w:<>,\s\*&]*?)\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*\([^;{)]*\)\s*"
    r"(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?(?:=\s*0\s*)?;",
    re.M,
)


def _strip_signals_section(hsrc: str) -> str:
    """去掉 `signals:` 段（信号只需声明，不需要定义）。"""
    out = []
    in_signals = False
    for line in hsrc.splitlines():
        stripped = line.strip()
        if re.match(r"^(public|private|protected)\s*(slots)?\s*:", stripped) or \
           stripped.startswith("};") or stripped == "}":
            in_signals = False
        if stripped.startswith("signals:") or stripped.startswith("Q_SIGNALS"):
            in_signals = True
            continue
        if not in_signals:
            out.append(line)
    return "\n".join(out)


def check_cpp_pairs() -> None:
    """头文件里声明的类成员函数，应该在配对的 .cpp 里有定义。"""
    src_root = os.path.join(ROOT, "src")
    for dirpath, _dirs, files in os.walk(src_root):
        for name in files:
            if not name.endswith(".h"):
                continue
            header = os.path.join(dirpath, name)
            cpp = header[:-2] + ".cpp"
            hsrc = read(header)
            # 只检查 Q_OBJECT 类头（自由函数头 / 工具头不适用）
            if "Q_OBJECT" not in hsrc:
                continue
            if not os.path.exists(cpp):
                warn(f"F: {rel(header)} 没有对应的 .cpp")
                continue
            csrc = read(cpp)

            body = _strip_signals_section(strip_js_and_strings(hsrc))
            declared = set()
            for m in CPP_FUNC_RE.finditer(body):
                nm = m.group("name")
                if nm in ("return", "if", "for", "while", "switch", "class", "struct",
                          "namespace", "using", "typedef", "explicit", "friend",
                          "operator", "template", "typename"):
                    continue
                declared.add(nm)
            inline_defined = set(
                re.findall(r"\b(\w+)\s*\([^;{)]*\)\s*(?:const\s*)?(?:override\s*)?\{",
                           body)
            )
            missing = []
            for nm in sorted(declared - inline_defined):
                if not re.search(rf"\b{re.escape(nm)}\s*\(", csrc):
                    missing.append(nm)
            if missing:
                err(f"F: {rel(header)} 声明的函数在 {rel(cpp)} 里找不到定义: {', '.join(missing)}")


def check_invokable_and_properties() -> None:
    """Q_INVOKABLE 名字与 Q_PROPERTY 的 READ 访问器必须有定义（.cpp 或头文件内联）。"""
    src_root = os.path.join(ROOT, "src")
    for dirpath, _dirs, files in os.walk(src_root):
        for name in files:
            if not name.endswith(".h"):
                continue
            header = os.path.join(dirpath, name)
            cpp = header[:-2] + ".cpp"
            hsrc = read(header)
            if "Q_OBJECT" not in hsrc:
                continue
            if not os.path.exists(cpp):
                continue
            csrc = read(cpp)
            hsrc_nc = strip_js_and_strings(hsrc)

            def defined(name: str) -> bool:
                if re.search(rf"\b{re.escape(name)}\s*\(", csrc):
                    return True
                # 头文件内联定义：名称 + (...) 后（可带 const/override/noexcept）直接 {
                return bool(
                    re.search(
                        rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*"
                        rf"(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?\{{",
                        hsrc_nc,
                    )
                )

            for m in re.finditer(r"Q_INVOKABLE\s+[\w:<>,\s\*&]*?(\w+)\s*\(", hsrc_nc):
                nm = m.group(1)
                if not defined(nm):
                    err(f"G: {rel(header)} 的 Q_INVOKABLE {nm}() 没有定义")

            for m in re.finditer(r"Q_PROPERTY\s*\((.*?)\)\s*(?=\n|$)", hsrc, re.S):
                body = m.group(1)
                rm = re.search(r"\bREAD\s+(\w+)", body)
                if not rm:
                    continue
                accessor = rm.group(1)
                if not defined(accessor):
                    err(f"G: {rel(header)} 的 Q_PROPERTY READ {accessor}() 没有定义")


# ---------------------------------------------------------------- H/I: Go
def go_files() -> list[str]:
    d = os.path.join(ROOT, "go_server", "main")
    if not os.path.isdir(d):
        return []
    return [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".go")]


def check_go_stdlib() -> None:
    files = go_files()
    if not files:
        err("H: go_server/main/ 下没有 .go 文件")
        return
    for path in files:
        src = read(path)
        for block in re.findall(r"^import\s*\((.*?)\)", src, re.S | re.M):
            for line in block.splitlines():
                line = line.strip()
                if not line or line.startswith("//"):
                    continue
                mm = re.match(r'(?:[\w.]+\s+)?"([^"]+)"', line)
                if not mm:
                    continue
                imp = mm.group(1)
                first = imp.split("/")[0]
                if "." in first:
                    err(f"H: {rel(path)} 引入第三方包 {imp}（本项目只允许标准库）")
        for single in re.findall(r'^import\s+(?:[\w.]+\s+)?"([^"]+)"', src, re.M):
            first = single.split("/")[0]
            if "." in first:
                err(f"H: {rel(path)} 引入第三方包 {single}")

    gosum = os.path.join(ROOT, "go_server", "main", "go.sum")
    if os.path.exists(gosum) and os.path.getsize(gosum) > 0:
        err("H: go_server/main/go.sum 非空 —— 说明引入了第三方依赖")


def check_go_routes() -> None:
    files = go_files()
    if not files:
        return
    routes_path = os.path.join(ROOT, "go_server", "main", "routes.go")
    if not os.path.exists(routes_path):
        err("I: go_server/main/routes.go 缺失")
        return
    registered = re.findall(r'mux\.HandleFunc\(\s*"([^"]+)"\s*,\s*(\w+)\s*\)', read(routes_path))
    if not registered:
        err("I: routes.go 里没有解析到任何 mux.HandleFunc 注册")
        return
    all_src = "\n".join(read(p) for p in files)
    for path, handler in registered:
        if not re.search(rf"func\s+{re.escape(handler)}\s*\(", all_src):
            err(f"I: routes.go 注册了 {path} -> {handler}，但没有找到该函数定义")

    # SPEC 里的路由必须都注册
    spec = os.path.join(ROOT, "docs", "SPEC.md")
    if os.path.exists(spec):
        spec_routes = set(re.findall(r"^\|\s*(?:GET|POST)\s*\|\s*`([^`]+)`", read(spec), re.M))
        registered_paths = {p for p, _ in registered}
        for route in sorted(spec_routes - registered_paths):
            err(f"I: SPEC.md 里的路由 {route} 没有在 routes.go 注册")
        for route in sorted(registered_paths - spec_routes):
            warn(f"I: routes.go 注册了 SPEC.md 未记录的 {route}")


def check_workflow_yaml() -> None:
    """极简 YAML 结构校验（无第三方解析器可用时的兜底）。

    检查：禁用 tab、缩进为偶数、键值行语法、块标量后的缩进、引号成对、job needs 引用有效。
    不足以替代真正的 YAML 解析，但能挡住绝大多数手写错误。
    """
    path = os.path.join(ROOT, ".github", "workflows", "build.yml")
    if not os.path.exists(path):
        err("J: .github/workflows/build.yml 缺失")
        return
    raw_lines = read(path).splitlines()

    work_indent = 0  # 进入块标量后的最小缩进
    for lineno, line in enumerate(raw_lines, 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip(" "))
        # 块标量（run: | 之类）里的内容是 shell / python 代码，
        # 缩进可以任意、也可以带 tab，必须在做 YAML 语法检查之前跳过。
        if work_indent and indent >= work_indent:
            continue
        work_indent = 0
        if "\t" in line:
            err(f"J: build.yml:{lineno} 含 tab 字符（YAML 映射部分禁用 tab 缩进）")
        if indent % 2 != 0:
            err(f"J: build.yml:{lineno} 缩进 {indent} 不是 2 的倍数")
        stripped = line.strip()
        # 列表项 / 键值行
        is_item = stripped.startswith("- ")
        probe = stripped[2:].strip() if is_item else stripped
        if probe.startswith("- "):
            is_item = True
            probe = probe[2:].strip()
        if probe.endswith(":") or probe.endswith(": |") or probe.endswith(": >"):
            if probe.endswith("|") or probe.endswith(">"):
                work_indent = indent + 2
            continue
        # 纯标量列表项（`- 'src/**'` / `- 1` / `- foo`）是合法 YAML
        if is_item and (":" not in probe or probe[0] in "\"'"):
            continue
        if ":" not in probe:
            err(f"J: build.yml:{lineno} 既不是列表项也不是键值行: {stripped[:60]!r}")
            continue
        key, _, value = probe.partition(":")
        if not re.fullmatch(r"[\w.\-/'\"*]+", key.strip()):
            warn(f"J: build.yml:{lineno} 可疑的键名: {key.strip()!r}")
        value = value.strip()
        if value in ("|", ">", "|-", ">-", "|+", ">+"):
            work_indent = indent + 2
        if value.count('"') % 2 or value.count("'") % 2:
            warn(f"J: build.yml:{lineno} 值里的引号可能不成对: {value[:60]!r}")

    # job needs 引用有效性（用文本级检查，够用）
    jobs_block = re.search(r"^jobs:\s*$", "\n".join(raw_lines), re.M)
    if not jobs_block:
        err("J: build.yml 里找不到 `jobs:` 段")
        return
    job_names = set(re.findall(r"^  ([A-Za-z0-9_-]+):\s*$", "\n".join(raw_lines), re.M))
    for needs in re.findall(r"^\s+needs:\s*\[([^\]]*)\]", "\n".join(raw_lines), re.M):
        for name in (n.strip() for n in needs.split(",")):
            if name and name not in job_names:
                err(f"J: build.yml 的 needs 引用了不存在的 job: {name}")


COMPONENT_NAMES = [
    "TitleBar", "LoadingIndicator", "ErrorOverlay", "Toast", "IconButton", "Avatar",
    "TabBar", "BlogCard", "RichTextLabel", "UserRow", "CommentRow", "HotRow",
    "TopicRow", "LoadMoreListView", "SkeletonPill", "PopupStack",
    "VirtualKeyboardInput", "SearchInput", "TextAreaInput", "EmptyState",
    "ConfirmPopup",
]

# Theme 放在 components/ 里，并由 components/qmldir 声明为 singleton。
# 这是 netease-music 插件在真机上验证过的布局：
#   * components/ 下的文件靠「同目录隐式导入」直接拿到 Theme，不需要任何 import
#   * pages/ 用未限定的 `import "../components"` 拿到 Theme 与全部组件
#   * main.qml 用未限定的 `import "components"`
# 之所以不把 Theme 放在 qml/ 根目录，是因为 QML 单例只有在
# 「qmldir 声明」或「同目录隐式导入」下才可见，根目录布局会要求每个
# 文件都写 `import ".."`，漏一个就是运行期报错。


def _insert_import(src: str, want: str) -> tuple[str, bool]:
    """在 import 段末尾插入一条 import（已存在则不动）。"""
    if re.search(rf"^\s*import\s+{re.escape(want)}\s*$", src, re.M):
        return src, False
    lines = src.split("\n")
    last = -1
    for i, line in enumerate(lines):
        if line.lstrip().startswith("import "):
            last = i
    if last < 0:
        lines.insert(0, f"import {want}")
        return "\n".join(lines), True
    lines.insert(last + 1, f"import {want}")
    return "\n".join(lines), True


def fix_qml_module() -> int:
    """把 QML 模块布局规范化成上面描述的 netease 布局（幂等）。"""
    changed = 0
    qml_root = os.path.join(ROOT, "qml")
    comp_dir = os.path.join(qml_root, "components")
    page_dir = os.path.join(qml_root, "pages")

    # 1) Theme.qml 从 qml/ 移到 qml/components/
    old_theme = os.path.join(qml_root, "Theme.qml")
    new_theme = os.path.join(comp_dir, "Theme.qml")
    if os.path.exists(old_theme) and not os.path.exists(new_theme):
        os.makedirs(comp_dir, exist_ok=True)
        os.replace(old_theme, new_theme)
        print(f"  [fix ] 移动 Theme.qml -> {rel(new_theme)}")
        changed += 1

    if not os.path.isdir(comp_dir):
        return changed

    # 2) 生成 components/qmldir
    comp_files = [
        f for f in sorted(os.listdir(comp_dir))
        if f.endswith(".qml") and f != "Theme.qml"
    ]
    if os.path.exists(new_theme):
        lines = ["singleton Theme 1.0 Theme.qml", ""]
    else:
        lines = []
    for f in comp_files:
        lines.append(f"{f[:-4]} 1.0 {f}")
    want_qmldir = "\n".join(lines) + "\n"
    qmldir_path = os.path.join(comp_dir, "qmldir")
    if not os.path.exists(qmldir_path) or read(qmldir_path) != want_qmldir:
        with open(qmldir_path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(want_qmldir)
        print(f"  [fix ] 写入 {rel(qmldir_path)}（{len(comp_files)} 个组件 + Theme）")
        changed += 1

    # 3) 删除 qml/qmldir：Theme 已移入 components/，根目录再声明组件会造成
    #    「同一文件被两个 import 作用域各声明一次」的同名类型重复。
    root_qmldir = os.path.join(qml_root, "qmldir")
    if os.path.exists(root_qmldir):
        os.remove(root_qmldir)
        print(f"  [fix ] 删除 {rel(root_qmldir)}（组件统一由 components/qmldir 提供）")
        changed += 1

    # 4) pages/*.qml 需要「未限定」的 import "../components"；
    #    同时清掉历史遗留的 `import ".."`（qml/ 根目录已无 qmldir，
    #    它只会把 main.qml 当成类型引入，形成 main -> pages -> main 的循环导入味道）
    if os.path.isdir(page_dir):
        for name in sorted(os.listdir(page_dir)):
            if not name.endswith(".qml"):
                continue
            full = os.path.join(page_dir, name)
            src = read(full)
            orig = src
            if re.search(r"\bTheme\s*\.", src) and not re.search(
                r'^\s*import\s+"\.\./components"\s*$', src, re.M
            ):
                src, _ = _insert_import(src, '"../components"')
            src = re.sub(r'^[ \t]*import\s+"\.\."\s*\n', "", src, flags=re.M)
            if src != orig:
                with open(full, "w", encoding="utf-8", newline="\n") as fh:
                    fh.write(src)
                print(f"  [fix ] 规范化 import: {rel(full)}")
                changed += 1

    # 5) main.qml 需要「未限定」的 import "components"
    main_qml = os.path.join(qml_root, "main.qml")
    if os.path.exists(main_qml):
        src = read(main_qml)
        if re.search(r"\bTheme\s*\.", src) and \
           not re.search(r'^\s*import\s+"components"\s*$', src, re.M):
            src, c = _insert_import(src, '"components"')
            if c:
                with open(main_qml, "w", encoding="utf-8", newline="\n") as fh:
                    fh.write(src)
                print(f"  [fix ] 补充未限定 import: {rel(main_qml)}")
                changed += 1

    return changed


def check_qml_imports() -> None:
    qml_root = os.path.join(ROOT, "qml")
    comp_dir = os.path.join(qml_root, "components")
    page_dir = os.path.join(qml_root, "pages")
    if not os.path.isdir(qml_root):
        return

    if not os.path.exists(os.path.join(comp_dir, "Theme.qml")):
        err("K: qml/components/Theme.qml 不存在（Theme 单例必须放在 components/ 下）")
    if not os.path.exists(os.path.join(comp_dir, "qmldir")):
        err("K: qml/components/qmldir 不存在（Theme 与组件的模块声明）")
    root_qmldir = os.path.join(qml_root, "qmldir")
    if os.path.exists(root_qmldir):
        for lineno, line in enumerate(read(root_qmldir).splitlines(), 1):
            if re.match(r"^singleton\s+Theme\b", line.strip()):
                err(f"K: qml/qmldir:{lineno} 不应再声明 Theme（已移入 components/）")

    if os.path.isdir(page_dir):
        for name in sorted(os.listdir(page_dir)):
            if not name.endswith(".qml"):
                continue
            full = os.path.join(page_dir, name)
            src = read(full)
            if re.search(r"\bTheme\s*\.", src) and not re.search(
                r'^\s*import\s+"\.\./components"\s*$', src, re.M
            ):
                err(f'K: {rel(full)} 用了 Theme 但缺少未限定的 `import "../components"`')
            has_bare_component = any(
                re.search(rf"^\s*{re.escape(cn)}\s*\{{", src, re.M) for cn in COMPONENT_NAMES
            )
            if has_bare_component and not re.search(
                r'^\s*import\s+"\.\./components"\s*$', src, re.M
            ):
                err(f'K: {rel(full)} 用了裸组件名但缺少未限定的 `import "../components"`')
            if re.search(r'^\s*import\s+"\.\."\s*$', src, re.M):
                warn(f'K: {rel(full)} 还有多余的 `import ".."`（qml/ 根目录已无 qmldir），'
                     f'跑 `--fix` 会自动清掉')

    main_qml = os.path.join(qml_root, "main.qml")
    if os.path.exists(main_qml):
        src = read(main_qml)
        if re.search(r"\bTheme\s*\.", src) and not re.search(
            r'^\s*import\s+"components"\s*$', src, re.M
        ):
            err('K: qml/main.qml 用了 Theme 但缺少未限定的 `import "components"`')


MODULE_HEADERS = {
    "feed": "src/modules/feed/WeiboFeedModule.h",
    "status": "src/modules/status/WeiboStatusModule.h",
    "comments": "src/modules/comment/WeiboCommentModule.h",
    "search": "src/modules/search/WeiboSearchModule.h",
    "profile": "src/modules/profile/WeiboProfileModule.h",
    "login": "src/modules/login/WeiboLoginModule.h",
    "publish": "src/modules/publish/WeiboPublishModule.h",
    "topic": "src/modules/topic/WeiboTopicModule.h",
    "media": "src/modules/media/WeiboMediaModule.h",
    "viewer": "src/modules/viewer/WeiboViewerModule.h",
}


def _module_api() -> tuple[dict[str, set[str]], set[str]]:
    """每个业务模块头文件里 Q_INVOKABLE / 公开方法的集合，以及全部头文件的方法全集。"""
    api: dict[str, set[str]] = {}
    for mod, rel_path in MODULE_HEADERS.items():
        path = os.path.join(ROOT, rel_path)
        if not os.path.exists(path):
            err(f"L: 模块头文件缺失: {rel_path}")
            api[mod] = set()
            continue
        src = strip_js_and_strings(read(path))
        names = set(re.findall(r"Q_INVOKABLE\s+[\w:<>,\s\*&]*?(\w+)\s*\(", src))
        names |= set(re.findall(r"^\s+(?:virtual\s+)?[\w:<>,\s\*&]+?\s(\w+)\s*\(", src, re.M))
        api[mod] = names

    ctrl = os.path.join(ROOT, "src", "WeiboController.h")
    api["__controller__"] = set()
    if os.path.exists(ctrl):
        api["__controller__"] = set(re.findall(r"\b(\w+)\s*\(", read(ctrl)))

    # 全项目头文件的方法全集：用来抑制「方法确实存在、只是别名归类不准」的误报
    global_api: set[str] = set()
    for dirpath, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for name in files:
            if name.endswith((".h", ".hpp")):
                src = strip_js_and_strings(read(os.path.join(dirpath, name)))
                global_api |= set(re.findall(r"\b(\w+)\s*\(", src))
    return api, global_api


def check_qml_cpp_api() -> None:
    """QML 里调用的 controller.<module>.<method>() 必须在对应头文件里真实存在。

    这是最容易在真机上炸掉的一类问题（"Property 'xxx' is not a function"），
    而静态编译完全发现不了。这里做别名解析：
        var p = c.profile            -> p 视为 profile 模块
        function commentOf() { return c.comments }  -> commentOf() 的返回值视为 comments
        var m = commentOf()          -> m 视为 comments
    """
    if not os.path.isdir(os.path.join(ROOT, "qml")):
        return
    api, global_api = _module_api()
    modules = "|".join(MODULE_HEADERS)

    qml_files = []
    for dirpath, _dirs, files in os.walk(os.path.join(ROOT, "qml")):
        for name in files:
            if name.endswith(".qml"):
                qml_files.append(os.path.join(dirpath, name))

    for path in sorted(qml_files):
        src = read(path)
        aliases: dict[str, str] = {}
        funcs: dict[str, str] = {}

        # 函数体里 return <...>.<module> 的辅助函数
        # 别名解析：只在 `= <...>.<module>` 结尾时才算模块别名，
        # 排除 `var m = c.comments.commentModel()` 这种「取模型」的赋值。
        for fm in re.finditer(
            r"function\s+(\w+)\s*\([^)]*\)\s*\{(.*?)\n    \}", src, re.S
        ):
            body = fm.group(2)
            mm = re.search(rf"return\s+[\w.]*?\.({modules})\s*;?\s*$", body, re.M)
            if mm:
                funcs[fm.group(1)] = mm.group(1)

        for am in re.finditer(
            rf"\b(?:var\s+)?(\w+)\s*=\s*[^\n;()]*?\.({modules})\s*[;\n]", src
        ):
            aliases.setdefault(am.group(1), am.group(2))
        # var x = someHelper()
        for _ in range(2):
            for am in re.finditer(r"\b(?:var\s+)?(\w+)\s*=\s*(\w+)\s*\(\s*\)", src):
                if am.group(2) in funcs:
                    aliases.setdefault(am.group(1), funcs[am.group(2)])

        for call in re.finditer(r"\b(\w+)\.(\w+)\s*\(", src):
            var, method = call.group(1), call.group(2)
            if var not in aliases:
                continue
            mod = aliases[var]
            if method in api.get(mod, set()) or method in global_api:
                continue
            line = src[: call.start()].count("\n") + 1
            err(
                f"L: {rel(path)}:{line} 调用了 {var}.{method}()，"
                f"但 {MODULE_HEADERS[mod]} 里没有这个方法（{mod} 模块）"
            )

    # controller 上的模型访问器（controller.homeModel() 等）
    ctrl_api = api.get("__controller__", set())
    for path in sorted(qml_files):
        src = read(path)
        for call in re.finditer(r"\bcontroller\.(\w+)\s*\(", src):
            method = call.group(1)
            if method not in ctrl_api:
                line = src[: call.start()].count("\n") + 1
                err(f"L: {rel(path)}:{line} 调用了 controller.{method}()，"
                    f"但 src/WeiboController.h 里没有这个方法")


def _split_top_level(params: str) -> list[str]:
    """按顶层逗号切分参数列表（忽略 <> () [] 内的逗号）。"""
    out, depth, cur = [], 0, []
    for ch in params:
        if ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    tail = "".join(cur).strip()
    if tail:
        out.append(tail)
    return out


# Qt 宏：跨行折叠后很容易被「声明提取」正则误当成方法名
QT_MACROS = {
    "Q_PROPERTY", "Q_INVOKABLE", "Q_ENUM", "Q_ENUMS", "Q_OBJECT", "Q_GADGET",
    "Q_SIGNALS", "Q_SIGNAL", "Q_SLOTS", "Q_SLOT", "Q_FLAG", "Q_FLAGS",
    "Q_DECLARE_METATYPE", "Q_DISABLE_COPY", "Q_NODISCARD_CTOR",
}


def _header_declarations(hsrc: str) -> dict[tuple[str, str], tuple[int, bool]]:
    """从类头文件抽取 (类名, 方法名) -> (参数个数, 是否 const)。"""
    decls: dict[tuple[str, str], tuple[int, bool]] = {}
    body_all = _strip_signals_section(strip_js_and_strings(hsrc))
    collapsed = re.sub(r"\s+", " ", body_all)

    # 逐个类体（用花括号配平切出来）
    for cm in re.finditer(r"\b(?:class|struct)\s+(\w+)\b[^{;]*\{", collapsed):
        cls = cm.group(1)
        i = cm.end() - 1
        depth = 0
        for j in range(i, len(collapsed)):
            if collapsed[j] == "{":
                depth += 1
            elif collapsed[j] == "}":
                depth -= 1
                if depth == 0:
                    break
        body = collapsed[i + 1 : j]
        # 构造函数 / 析构函数
        for mm in re.finditer(rf"(?:explicit\s+)?{re.escape(cls)}\s*\(([^;{{}}]*?)\)\s*;", body):
            decls[(cls, cls)] = (len(_split_top_level(mm.group(1))), False)
        for mm in re.finditer(rf"~{re.escape(cls)}\s*\(\s*\)\s*(?:override\s*)?;", body):
            decls[(cls, "~" + cls)] = (0, False)
        # 普通成员函数声明（以 ; 结尾，且不是内联定义）
        for mm in re.finditer(
            r"(?:Q_INVOKABLE\s+)?(?:virtual\s+)?(?:static\s+)?(?:inline\s+)?"
            r"([\w:<>,\s\*&]+?)\s+(\w+)\s*\(([^;{}]*?)\)\s*(const\s*)?"
            r"(?:override\s*)?(?:noexcept\s*)?;",
            body,
        ):
            ret, name, params, is_const = mm.group(1), mm.group(2), mm.group(3), mm.group(4)
            if name in ("return", "if", "for", "while", "switch", "sizeof") or name in QT_MACROS:
                continue
            ret_words = set(ret.split())
            if ret_words & {"return", "typedef", "using", "friend", "template"}:
                continue
            if ret.strip() in QT_MACROS or ret.strip().startswith("Q_PROPERTY"):
                continue
            decls[(cls, name)] = (len(_split_top_level(params)), bool(is_const))
    return decls


def _cpp_definitions(csrc: str, fname: str) -> list[tuple[str, str, int, bool, int]]:
    """从 .cpp 抽取 (类名, 方法名, 参数个数, 是否 const, 行号)。

    只认**行首（第 0 列）**的 `Class::method(...)`，这样函数体里缩进的
    `QObject::connect(...)`、`QTimer::singleShot(...)` 这类调用不会被误判成定义。
    构造函数带成员初始化列表（`)` 后面是 `:` 而不是 `{`）也要认得；
    花括号另起一行（Allman 风格）同样要认得。
    """
    clean = strip_js_and_strings(csrc)
    collapsed = re.sub(r"[ \t]+", " ", clean)
    pattern = re.compile(
        r"^(?![ \t])"                    # 定义必须在第 0 列（排除函数体里缩进的调用）
        r"(?:([\w:<>,*&\s]+?) )?"        # 可选返回类型，允许内部空格（如 QHash<int, QByteArray>）
        r"(\w+)::(~?\w+)\s*"
        r"\(([^;{}]*?)\)"
        r"\s*(const\b)?"
        r"\s*(?:noexcept\b)?"
        r"\s*(?:\{|:)",                  # `{` 或构造函数的成员初始化列表 `:`
        re.M,
    )
    defs = []
    for m in pattern.finditer(collapsed):
        cls, name, params, is_const = m.group(2), m.group(3), m.group(4), m.group(5)
        line = collapsed[: m.start()].count("\n") + 1
        defs.append((cls, name, len(_split_top_level(params)), bool(is_const), line, fname))
    return defs


def check_cpp_signatures() -> None:
    """头文件声明 ↔ .cpp 定义的参数个数 / const 一致性，以及重复定义。"""
    src_root = os.path.join(ROOT, "src")
    all_defs: dict[tuple[str, str], list] = {}
    per_header: dict[str, dict] = {}

    for dirpath, _dirs, files in os.walk(src_root):
        for name in files:
            if not name.endswith(".h"):
                continue
            header = os.path.join(dirpath, name)
            hsrc = read(header)
            if "Q_OBJECT" not in hsrc:
                continue
            cpp = header[:-2] + ".cpp"
            if not os.path.exists(cpp):
                continue
            per_header[header] = _header_declarations(hsrc)
            for cls, mname, arity, is_const, line, fname in _cpp_definitions(
                read(cpp), os.path.basename(cpp)
            ):
                all_defs.setdefault((cls, mname), []).append(
                    (arity, is_const, line, fname, header)
                )

    # 重复定义（链接错误）。真实定义只可能来自各自配对的 .cpp。
    for (cls, mname), entries in sorted(all_defs.items()):
        if len(entries) > 1:
            locs = ", ".join(f"{e[3]}:{e[2]}" for e in entries)
            err(f"M: {cls}::{mname} 被定义了 {len(entries)} 次（重复定义，链接错误）: {locs}")

    # 声明 ↔ 定义对账
    for header, decls in per_header.items():
        cpp = header[:-2] + ".cpp"
        hsrc = read(header)
        for (cls, mname), (d_arity, d_const) in decls.items():
            # 构造函数 / 析构函数：F 检查与人工复核覆盖，这里不报以免噪声
            if mname == cls or mname == "~" + cls:
                continue
            entries = [e for e in all_defs.get((cls, mname), []) if e[4] == header]
            if not entries:
                if re.search(
                    rf"\b{re.escape(mname)}\s*\([^;{{}}]*\)\s*(?:const\s*)?(?:override\s*)?\{{",
                    hsrc,
                ):
                    continue  # 头文件内联定义
                err(f"M: {rel(header)} 声明了 {cls}::{mname}，但 {rel(cpp)} 里没有对应定义")
                continue
            for arity, is_const, line, fname, _h in entries:
                if arity != d_arity:
                    err(
                        f"M: {cls}::{mname} 参数个数不一致：头文件 {d_arity} 个，"
                        f"{fname}:{line} 定义 {arity} 个"
                    )
                if is_const != d_const:
                    err(
                        f"M: {cls}::{mname} const 限定不一致：头文件 "
                        f"{'const' if d_const else '非 const'}，{fname}:{line} "
                        f"{'const' if is_const else '非 const'}"
                    )


def check_qml_relative_paths() -> None:
    """QML/JS 里的相对 import 与 source 路径必须能解析到真实文件。

    这类错误（尤其 Theme.qml 被移动后 FontLoader 的相对路径失效）编译期
    完全看不出来，只会在设备上静默失败或报 "Cannot open: ..."。
    """
    qml_root = os.path.join(ROOT, "qml")
    if not os.path.isdir(qml_root):
        return
    for dirpath, _dirs, files in os.walk(qml_root):
        for name in files:
            if not name.endswith((".qml", ".js")):
                continue
            full = os.path.join(dirpath, name)
            src = read(full)
            for m in re.finditer(r'\bimport\s+"([^"]+)"|source:\s*"([^"]+)"', src):
                ref = m.group(1) if m.group(1) is not None else m.group(2)
                if not ref:
                    continue
                if "://" in ref or ref.startswith(("qrc:", "image:", "data:", "weibo:")):
                    continue
                target = os.path.normpath(os.path.join(dirpath, ref))
                if os.path.exists(target):
                    continue
                line = src[: m.start()].count("\n") + 1
                if ref.endswith((".ttf", ".otf", ".ttc")):
                    # 插件自带中文字体（qml/fonts/msyh.ttf），缺了设备上中文全是方块，
                    # 所以这里是**错误**而不是警告：字体必须随包一起走。
                    err(
                        f"N: {rel(full)}:{line} 字体 {ref} 不存在"
                        f"（期望 {rel(target)}）—— 缺少中文字体时设备上中文会显示成方块"
                    )
                else:
                    err(f"N: {rel(full)}:{line} 相对路径 {ref} 解析不到文件（期望 {rel(target)}）")


def check_font() -> None:
    """插件自带的中文字体必须是**真正的** TrueType。

    为什么单独查：设备自带字体不含中文字形，这份字体一旦坏掉，插件照样能启动，
    只是中文全变成方块 —— 属于「编译/打包全通过、上机才发现」的问题。
    真实踩过的坑包括：被 Git LFS 换成几行文本指针、被当文本做了换行转换、
    复制成了 `.ttc` 字体集合（Qt 5.15 的 FontLoader 不一定能加载）。
    """
    path = os.path.join(ROOT, "qml", "fonts", "msyh.ttf")
    if not os.path.exists(path):
        err("U: qml/fonts/msyh.ttf 不存在 —— 设备上中文会显示成方块")
        return
    raw = open(path, "rb").read()
    if len(raw) < 100 * 1024:
        err(f"U: qml/fonts/msyh.ttf 只有 {len(raw)} 字节，不像真字体（可能被 LFS 指针或占位文件替换）")
        return
    if raw[:40].startswith(b"version https://git-lfs"):
        err("U: qml/fonts/msyh.ttf 是 Git LFS 指针文件，不是字体本身")
        return
    magic = raw[:4]
    if magic not in (b"\x00\x01\x00\x00", b"true", b"OTTO", b"ttcf"):
        err(f"U: qml/fonts/msyh.ttf 魔数 {magic.hex()} 不是字体（被当成文本转换过？）")
        return
    if magic == b"ttcf":
        err("U: qml/fonts/msyh.ttf 是 .ttc 字体集合，FontLoader 在 Qt 5.15 上不一定能加载，请换成单个 .ttf")
        return
    num_tables = int.from_bytes(raw[4:6], "big")
    tables = {raw[12 + i * 16: 16 + i * 16].decode("latin1") for i in range(num_tables)}
    needed = {"head", "name", "cmap", "hhea"}
    if magic == b"OTTO":
        needed.add("CFF ")
    else:
        needed |= {"glyf", "loca"}
    missing = sorted(needed - tables)
    if missing:
        err(f"U: qml/fonts/msyh.ttf 缺少必要的字体表: {missing}")
    else:
        log_ok(f"U: 中文字体 qml/fonts/msyh.ttf 正常（{len(raw) // 1024} KB，{num_tables} 张表）")


# 常见根元素的内建属性。在 QML 里对**根对象**重复声明同名的 property，
# 或者给同一个属性赋两次值，Qt 会报
#   "<file>:<line> Property <name> set multiple times"
# 而且这个组件会直接变成 "unavailable"，整条加载链一起失败 ——
# 真机上表现为「插件点开就报 Type Pages.HomePage unavailable」。
# 这类错误 verify.py 早期完全查不出来（编译器/打包都过），所以单列一项。
QT_BUILTIN_BY_ROOT: dict[str, set[str]] = {
    "Text": {
        "text", "color", "font", "elide", "wrapMode", "maximumLineCount",
        "textFormat", "lineHeight", "horizontalAlignment", "verticalAlignment",
        "style", "styleColor", "linkColor", "readOnly", "selectionColor",
        "selectedTextColor", "cursorVisible", "renderType", "baseUrl",
    },
    "TextEdit": {
        "text", "color", "font", "elide", "wrapMode", "textFormat",
        "horizontalAlignment", "verticalAlignment", "readOnly", "selectByMouse",
        "selectedTextColor", "selectionColor", "cursorVisible", "renderType",
        "persistentSelection", "textMargin",
    },
    "TextInput": {
        "text", "color", "font", "echoMode", "validator", "inputMask",
        "horizontalAlignment", "verticalAlignment", "readOnly", "passwordCharacter",
        "acceptableInput", "cursorVisible", "selectionColor", "selectedTextColor",
    },
    "Item": {
        "width", "height", "x", "y", "z", "opacity", "visible", "enabled",
        "rotation", "scale", "clip", "parent", "anchors", "children", "transform",
        "state", "states", "transitions", "layer", "implicitWidth", "implicitHeight",
        "focus", "activeFocus", "smooth", "antialiasing", "baselineOffset",
    },
    "Rectangle": {
        "color", "radius", "border", "gradient", "antialiasing", "topLeftRadius",
        "topRightRadius", "bottomLeftRadius", "bottomRightRadius",
    },
    "Image": {
        "source", "fillMode", "sourceSize", "status", "progress", "asynchronous",
        "cache", "mirrored", "paintedWidth", "paintedHeight", "mipmap",
        "horizontalAlignment", "verticalAlignment",
    },
    "MouseArea": {
        "acceptedButtons", "pressed", "containsMouse", "hoverEnabled",
        "propagateComposedEvents", "pressAndHoldInterval", "cursorShape", "enabled",
        "preventStealing", "drag", "onClicked", "onPressed", "onReleased",
    },
    "ListView": {
        "model", "delegate", "spacing", "orientation", "currentIndex", "contentY",
        "contentX", "count", "header", "footer", "highlight", "cacheBuffer",
        "snapMode", "boundsBehavior", "interactive", "flickDeceleration",
        "highlightFollowsCurrentItem", "highlightMoveDuration", "keyNavigationWraps",
    },
    "Flickable": {
        "contentWidth", "contentHeight", "contentX", "contentY", "interactive",
        "boundsBehavior", "flickableDirection", "pressDelay", "pixelAligned",
        "topMargin", "bottomMargin", "leftMargin", "rightMargin", "atXBeginning",
        "atYBeginning", "atXEnd", "atYEnd",
    },
    "Column": {"spacing", "layoutDirection", "padding", "topPadding", "bottomPadding"},
    "Row": {"spacing", "layoutDirection", "padding", "leftPadding", "rightPadding"},
    "Grid": {
        "spacing", "rows", "columns", "rowSpacing", "columnSpacing",
        "flow", "layoutDirection",
    },
    "Loader": {"source", "sourceComponent", "item", "active", "asynchronous", "progress", "status"},
}

_STR_LITERAL_RE = re.compile(r'"(?:[^"\\]|\\.)*"')


def _root_object_property_uses(src: str) -> tuple[str, list[tuple[int, str, str]]]:
    """返回 (根元素类型, [(行号, 属性名, 原始行)]) —— 只统计根对象**自己那一层**。

    做法：按大括号深度走一遍，只收集深度为 1 的行；行内的字符串字面量先去掉，
    免得 `text: "{"` 这类内容把深度算错。
    """
    root_type = ""
    uses: list[tuple[int, str, str]] = []
    depth = 0
    in_block_comment = False
    for lineno, raw_line in enumerate(src.split("\n"), 1):
        line = raw_line
        # 简化处理块注释（QML 里很少出现，但错了会误报）
        if in_block_comment:
            if "*/" in line:
                line = line.split("*/", 1)[1]
                in_block_comment = False
            else:
                continue
        if "/*" in line and "*/" not in line:
            line = line.split("/*", 1)[0]
            in_block_comment = True
        elif "/*" in line:
            line = re.sub(r"/\*.*?\*/", "", line)
        stripped = line.strip()
        if stripped.startswith("//"):
            continue
        line = _STR_LITERAL_RE.sub('""', line)
        was_depth = depth
        depth += line.count("{") - line.count("}")
        if was_depth == 0 and not root_type:
            # 根元素：第一个带 { 的类型名
            m = re.match(r"^\s*([A-Z][\w.]*)\s*(?:\{|\s*$)", line)
            if m and "{" in line:
                root_type = m.group(1)
            elif m:
                root_type = m.group(1)
        if was_depth != 1:
            continue
        # 同一层里的属性声明与赋值
        m_decl = re.match(r"^\s*(?:readonly\s+|default\s+)?property\s+[\w.<>]+\s+(\w+)\s*:", line)
        if m_decl:
            uses.append((lineno, m_decl.group(1), stripped))
            continue
        m_assign = re.match(r"^\s*([A-Za-z_][\w.]*)\s*:", line)
        if m_assign:
            name = m_assign.group(1)
            # id / signal / function / onXxx 都不是普通属性赋值
            if name == "id" or name.startswith("on") or name in ("signal", "function"):
                continue
            uses.append((lineno, name, stripped))
    return root_type, uses


def check_qml_duplicate_properties() -> None:
    """根对象里同一个属性被声明/赋值多次，或重复声明内建属性。

    真机踩过：`RichTextLabel.qml` 根元素是 Text，却又写了
    `property string text: ""` 并在下面 `text: displayText`，
    词典笔直接报
      PluginManager component error: main.qml:224 Type Pages.HomePage unavailable
      HomePage.qml:214 Type Components.BlogCard unavailable
      BlogCard.qml:233 Type RichTextLabel unavailable
      RichTextLabel.qml:45 Property value set multiple times
    一个组件的属性错误会让整条引用链全部 unavailable，所以必须静态拦住。
    """
    qml_root = os.path.join(ROOT, "qml")
    for dirpath, _dirs, files in os.walk(qml_root):
        for name in files:
            if not name.endswith(".qml"):
                continue
            full = os.path.join(dirpath, name)
            src = read(full)
            root_type, uses = _root_object_property_uses(src)
            seen: dict[str, int] = {}
            for lineno, prop, raw in uses:
                # 分组属性必须按**完整名字**比较：font.pixelSize 与 font.family
                # 是两个不同属性，各自赋一次是合法的。
                if prop in seen:
                    err(
                        f"V: {rel(full)}:{lineno} 属性 {prop} 在根对象里出现了两次"
                        f"（第 {seen[prop]} 行已经用过）—— Qt 会报 "
                        f"'Property {prop} set multiple times'，组件直接 unavailable"
                    )
                else:
                    seen[prop] = lineno
            builtins = QT_BUILTIN_BY_ROOT.get(root_type.split(".")[-1], set())
            for lineno, prop, raw in uses:
                if prop in builtins and re.match(
                    r"^\s*(?:readonly\s+|default\s+)?property\s", raw
                ):
                    err(
                        f"V: {rel(full)}:{lineno} 根元素是 {root_type}，不能重新声明它的内建属性 "
                        f"{prop} —— 换个属性名（例如 sourceText），把 {prop} 留作渲染输出"
                    )


def check_component_props() -> None:
    """实例化本地组件时赋的属性，必须真的存在于该组件上。

    对应运行期错误 `Cannot assign to non-existent property "xxx"`。
    真机上这类错误同样会让组件 unavailable、整条引用链失效 ——
    给 RichTextLabel 的输入属性改名（text → sourceText）时，
    任何一个调用点漏改都会以此形式爆炸，所以必须静态对账。

    属性来源 = 组件自己声明的 property
             ∪ 根元素类型的内建属性（含继承链，如 Text → Item）
             ∪ 声明的 signal（onXxx）
             ∪ 附属属性（Component.onCompleted / Keys.onPressed …）
    """
    components = _local_components()          # 组件名 -> 文件路径
    declared = {name: _declared_props(path) for name, path in components.items()}

    def effective(name: str, seen: set[str] | None = None) -> set[str] | None:
        """组件的全部可赋值属性；未知类型返回 None 表示「无法判断，不报」。"""
        seen = seen or set()
        if name in seen:
            return set()
        seen.add(name)
        if name in declared:
            path = components[name]
            own, root = declared[name]
            base = effective(root, seen)
            if base is None:
                return None            # 根类型不认识，放弃这个组件的检查
            return own | base
        return QT_EFFECTIVE_BUILTINS.get(name)

    local_names = set(components)
    for dirpath, _dirs, files in os.walk(os.path.join(ROOT, "qml")):
        for fname in files:
            if not fname.endswith(".qml"):
                continue
            full = os.path.join(dirpath, fname)
            src = read(full)
            for type_name, line, props in _object_blocks(src):
                target = type_name.split(".")[-1]
                if target not in local_names:
                    continue           # 只管本地组件；Qt 内建类型不在这一项范围内
                allowed = effective(target)
                if allowed is None:
                    continue
                for pline, prop, raw in props:
                    base = prop.split(".", 1)[0]
                    if base in QT_ATTACHED or base == "id" or base.startswith("on"):
                        continue
                    if "." in prop:
                        if base in allowed:
                            continue
                        # 分组属性：base 必须在，且不校验子项（子项太多）
                        err(
                            f"W: {rel(full)}:{pline} {target} 没有 {base} 属性"
                            f"（{raw[:60]}）"
                        )
                        continue
                    if prop not in allowed:
                        err(
                            f"W: {rel(full)}:{pline} {target} 没有属性 {prop}"
                            f"（{raw[:60]}）—— 真机会报 Cannot assign to non-existent property"
                        )


QT_ATTACHED = {
    "Component", "Keys", "ListView", "GridView", "Drag", "MouseArea", "Accessible",
    "Layout", "Screen", "Application", "Window", "Shortcut", "HoverHandler",
    "TapHandler", "WheelHandler", "DragHandler", "PinchHandler", "SystemPalette",
    "Style", "ToolTip", "Popup", "Dialog", "Menu", "Action",
}


def _object_blocks(src: str) -> list[tuple[str, int, list[tuple[int, str, str]]]]:
    """粗粒度扫描每个 `TypeName {` 块及其自身一层的属性赋值。

    用「块栈 + 花括号深度」判断属性归属：只有当前深度正好等于栈顶块的
    **块体深度**时，这一行的 `名字:` 才算该块的属性。这样
    `blog: ({ ... })` 这种 JS 对象字面量里的键就不会被误算到外层组件上
    （那会产生成百上千条假报）。

    只处理「一行一个属性」的常规写法；单行内联对象会漏掉，但不会误报。
    """
    out: list[tuple[str, int, list[tuple[int, str, str]]]] = []
    # 栈元素: [类型名, 起始行, 属性列表, 块体深度]
    stack: list[list] = []
    depth = 0
    in_block_comment = False
    for lineno, raw_line in enumerate(src.split("\n"), 1):
        line = raw_line
        if in_block_comment:
            if "*/" in line:
                line = line.split("*/", 1)[1]
                in_block_comment = False
            else:
                continue
        if "/*" in line and "*/" not in line:
            line = line.split("/*", 1)[0]
            in_block_comment = True
        if line.strip().startswith("//"):
            line = ""
        line = _STR_LITERAL_RE.sub('""', line)
        opens = line.count("{")
        closes = line.count("}")

        # 1) 归属判断：只有在栈顶块自己的那一层才记录属性
        if stack and depth == stack[-1][3] and opens == 0 and closes == 0:
            m_prop = re.match(r"^\s*([A-Za-z_][\w.]*)\s*:", line)
            if m_prop:
                stack[-1][2].append((lineno, m_prop.group(1), raw_line.strip()))

        # 2) 这一行是否打开一个新的类型块？
        m_open = re.match(r"^\s*([A-Z][\w.]*)\s*\{\s*$", line)
        if not m_open:
            m_open = re.match(r"^\s*[A-Za-z_][\w.]*\s*:\s*([A-Z][\w.]*)\s*\{\s*$", line)
        if m_open:
            block: list = [m_open.group(1), lineno, [], depth + 1]
            stack.append(block)
            out.append((block[0], block[1], block[2]))
            depth += 1
        else:
            depth += opens - closes

        # 3) 弹出已经闭合的块
        while stack and depth < stack[-1][3]:
            stack.pop()
    return out


def _local_components() -> dict[str, str]:
    """qml 下所有可作为类型使用的组件 → 文件路径（按文件名/相对路径两种写法）。"""
    out: dict[str, str] = {}
    qml_root = os.path.join(ROOT, "qml")
    for sub in ("components", "pages", ""):
        d = os.path.join(qml_root, sub) if sub else qml_root
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith(".qml"):
                out.setdefault(name[:-4], os.path.join(d, name))
    return out


def _declared_props(path: str) -> tuple[set[str], str]:
    """(该文件根对象声明的属性名集合, 根元素类型名)。"""
    src = read(path)
    root_type, uses = _root_object_property_uses(src)
    own = {prop for _line, prop, raw in uses
           if re.match(r"^\s*(?:readonly\s+|default\s+)?property\s", raw)}
    for m in re.finditer(r"^\s*(?:readonly\s+|default\s+)?property\s+[\w.<>]+\s+(\w+)\s*:", src, re.M):
        own.add(m.group(1))
    return own, root_type.split(".")[-1]


def _build_effective_builtins() -> dict[str, set[str]]:
    """把内建属性按继承链合并：Text 也要有 Item 的 width/height/anchors 等。"""
    out: dict[str, set[str]] = {}
    for name in QT_BUILTIN_BY_ROOT:
        chain: set[str] = set()
        cur: str | None = name
        guard = 0
        while cur and guard < 8:
            chain |= QT_BUILTIN_BY_ROOT.get(cur, set())
            cur = QT_PARENT.get(cur)
            guard += 1
        out[name] = chain
    return out


QT_PARENT: dict[str, str] = {
    "Text": "Item", "TextEdit": "Item", "TextInput": "Item", "Image": "Item",
    "MouseArea": "Item", "Rectangle": "Item", "Column": "Item", "Row": "Item",
    "Grid": "Item", "Loader": "Item", "ListView": "Flickable", "Flickable": "Item",
}
QT_EFFECTIVE_BUILTINS = _build_effective_builtins()


def _model_roles() -> tuple[set[str], set[str]]:
    """(角色名, 列表模型的 Q_PROPERTY 名)。

    `model.x` 在 QML 里既可能是 delegate 的角色名，也可能是
    `LoadMoreListView.model`（一个列表模型对象）的 Q_PROPERTY
    （loading / hasMore / count / errorMessage …），两者都要放行。
    """
    path = os.path.join(ROOT, "src", "WeiboModels.cpp")
    roles: set[str] = set()
    if os.path.exists(path):
        src = read(path)
        for m in re.finditer(r"::roleNames\s*\(\s*\)\s*const\s*\{", src):
            end = re.search(r"\n\}", src[m.end():])
            body = src[m.end(): m.end() + (end.start() if end else 4000)]
            roles |= set(re.findall(r'"([A-Za-z_]\w*)"', body))

    props: set[str] = set()
    hdr = os.path.join(ROOT, "src", "WeiboModels.h")
    if os.path.exists(hdr):
        for m in re.finditer(r"Q_PROPERTY\s*\(([^)]*)\)", read(hdr)):
            rm = re.search(r"\bREAD\s+(\w+)", m.group(1))
            if rm:
                props.add(rm.group(1))
    return roles, props


def check_qml_model_roles() -> None:
    """QML 里 `model.<role>` 用的名字必须真的存在（角色名或模型属性）。

    角色名写错时 QML 不会报错，只会静默得到 undefined —— 这是最难查的一类
    上机问题，所以这里做静态对账。
    """
    qml_root = os.path.join(ROOT, "qml")
    if not os.path.isdir(qml_root):
        return
    roles, props = _model_roles()
    if not roles:
        warn("O: 没能从 src/WeiboModels.cpp 解析出任何角色名，跳过角色对账")
        return
    allowed = roles | props | {"index", "count", "modelData", "objectName"}
    for dirpath, _dirs, files in os.walk(qml_root):
        for name in files:
            if not name.endswith(".qml"):
                continue
            full = os.path.join(dirpath, name)
            src = read(full)
            for m in re.finditer(r"\bmodel\.([A-Za-z_]\w*)", src):
                role = m.group(1)
                if role in allowed:
                    continue
                line = src[: m.start()].count("\n") + 1
                err(
                    f"O: {rel(full)}:{line} 使用了 model.{role}，"
                    f"但 WeiboModels 里既没有这个角色也没有这个模型属性"
                )


def check_blogcard_keys() -> None:
    """BlogCard 读取的 `blog.<key>` 必须是页面能提供的 BlogListModel 角色名。

    页面 delegate 里写的是 `blog: ({ id: model.id, ... })`，键名一旦与
    BlogCard 内部读的名字不一致，QML 只会静默拿到 undefined（卡片空白），
    不会报任何错。
    """
    card = os.path.join(ROOT, "qml", "components", "BlogCard.qml")
    if not os.path.exists(card):
        return
    roles, _props = _model_roles()
    if not roles:
        return
    extra = {"index", "count", "modelData", "length"}
    src = read(card)
    for m in re.finditer(r"\bblog\.([A-Za-z_]\w*)", src):
        key = m.group(1)
        if key in roles or key in extra:
            continue
        line = src[: m.start()].count("\n") + 1
        err(
            f"P: qml/components/BlogCard.qml:{line} 读取 blog.{key}，"
            f"但 BlogListModel 没有这个角色（页面 delegate 提供不了）"
        )


def ps1_files() -> list[str]:
    out = []
    for dirpath, dirnames, files in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", "dist", "__pycache__")]
        for name in files:
            if name.lower().endswith(".ps1"):
                out.append(os.path.join(dirpath, name))
    return sorted(out)


# 这些扩展名的文件**绝不能**有 UTF-8 BOM：
#   .sh    → 会破坏 shebang（#!/bin/bash 变成 \xef\xbb\xbf#!/bin/bash）
#   .go    → Go 工具链容忍，但没必要
#   .json  → encoding/json 直接报 invalid character 'ï'
#   .yml   → YAML 规范允许，但 GitHub Actions/各种解析器行为不一致，一律不要
#   .cpp/.h/.qml/.js/.md/.pro/.lua → 交给编译器/解释器时都是纯文本更安全
NO_BOM_EXTS = (".sh", ".go", ".json", ".yml", ".yaml", ".cpp", ".h", ".hpp",
               ".qml", ".js", ".md", ".pro", ".lua", ".txt", ".gitignore",
               ".gitattributes")


def fix_ps1_bom() -> int:
    """BOM 策略修复：给含非 ASCII 的 .ps1 补 UTF-8 BOM；去掉其它文本文件的多余 BOM。

    为什么 .ps1 必须带 BOM：Windows PowerShell 5.1 读没有 BOM 的 .ps1 会按系统
    ANSI 代码页（简中机器上是 GBK）解码，UTF-8 的中文注释被解成乱码后可能凑出
    引号/花括号，直接报 "Unexpected token" 之类的语法错误 —— 脚本根本跑不起来。
    """
    changed = 0
    for path in ps1_files():
        raw = open(path, "rb").read()
        if raw.startswith(b"\xef\xbb\xbf"):
            continue
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            continue
        if not any(ord(c) > 127 for c in text):
            continue
        with open(path, "wb") as fh:
            fh.write(b"\xef\xbb\xbf" + raw)
        print(f"  [fix ] 为 {rel(path)} 添加 UTF-8 BOM")
        changed += 1

    for dirpath, dirnames, files in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", "dist", "__pycache__")]
        for name in files:
            if not name.endswith(NO_BOM_EXTS) and name not in (".gitignore", ".gitattributes"):
                continue
            path = os.path.join(dirpath, name)
            raw = open(path, "rb").read()
            if raw.startswith(b"\xef\xbb\xbf"):
                with open(path, "wb") as fh:
                    fh.write(raw[3:])
                print(f"  [fix ] 去掉 {rel(path)} 多余的 UTF-8 BOM")
                changed += 1
    return changed


def check_ps1_bom() -> None:
    for path in ps1_files():
        raw = open(path, "rb").read()
        has_bom = raw.startswith(b"\xef\xbb\xbf")
        try:
            text = raw.decode("utf-8-sig")
        except UnicodeDecodeError as exc:
            err(f"Q: {rel(path)} 不是合法 UTF-8: {exc}")
            continue
        if any(ord(c) > 127 for c in text) and not has_bom:
            err(
                f"Q: {rel(path)} 含非 ASCII 但没有 UTF-8 BOM —— "
                f"Windows PowerShell 5.1 会按 GBK 解析并报语法错误（跑 --fix 修复）"
            )

    for dirpath, dirnames, files in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", "dist", "__pycache__")]
        for name in files:
            if not name.endswith(NO_BOM_EXTS) and name not in (".gitignore", ".gitattributes"):
                continue
            path = os.path.join(dirpath, name)
            if open(path, "rb").read().startswith(b"\xef\xbb\xbf"):
                extra = "（会破坏 shebang）" if name.endswith(".sh") else \
                        "（encoding/json 会报 invalid character）" if name.endswith(".json") else ""
                err(f"Q: {rel(path)} 带了多余的 UTF-8 BOM{extra}（跑 --fix 去掉）")


VEXING_PARSE_RE = re.compile(
    r"^\s*(?:const\s+)?(Q[A-Z]\w*|std::\w+|[A-Z]\w*)\s+(?:const\s+)?(\w+)\s*"
    r"\(\s*(Q[A-Z]\w*|std::\w+|[A-Z]\w*)\s*\(\s*(\w+)\s*\)\s*\)\s*;",
    re.M,
)


def check_most_vexing_parse() -> None:
    """侦测 most vexing parse：`Type var(Other(expr));` 会被当成函数声明。

    典型症状（CI 实际报过）：
        error: request for member 'setHeader' in 'request',
               which is of non-class type 'QNetworkRequest(QUrl)'
    即 `QNetworkRequest request(QUrl(url));` 被解析成函数声明。
    修法：`QNetworkRequest request{QUrl(url)};` 或再加一层括号。

    为避免误报（`void f(int(x));` 是合法的函数声明），只在外层返回类型是
    大写开头的类型（Q*/std::*/驼峰）时才报。
    """
    for dirpath, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for name in files:
            if not name.endswith((".cpp", ".h", ".hpp")):
                continue
            path = os.path.join(dirpath, name)
            src = strip_js_and_strings(read(path))
            for m in VEXING_PARSE_RE.finditer(src):
                line = src[: m.start()].count("\n") + 1
                err(
                    f"R: {rel(path)}:{line} 疑似 most vexing parse："
                    f"`{m.group(1)} {m.group(2)}({m.group(3)}({m.group(4)}));` "
                    f"会被解析成函数声明（改成 `{m.group(1)} {m.group(2)}"
                    f"{{{m.group(3)}({m.group(4)})}};`）"
                )


def check_lambda_this() -> None:
    """lambda 里用了 this（emit 成员信号 / 裸 m_ 成员）却没在捕获列表里写 this。

    这是 CI 上真实踩过的编译错误：
        error: 'this' was not captured for this lambda function
    逻辑复用 tools/lambda_this.py（可单独运行看详情）。
    """
    tools_dir = os.path.join(ROOT, "tools")
    if tools_dir not in sys.path:
        sys.path.insert(0, tools_dir)
    try:
        import lambda_this  # type: ignore
    except Exception as exc:  # pragma: no cover
        warn(f"S: 无法导入 tools/lambda_this.py（{exc}），跳过 lambda 捕获检查")
        return
    for problem in lambda_this.find_problems():
        err(f"S: {problem}")


MOJIBAKE_EXTS = (".md", ".go", ".cpp", ".h", ".hpp", ".qml", ".js", ".ps1", ".sh",
                 ".yml", ".json", ".pro", ".lua", ".py", ".txt")


def _cjk_count(s: str) -> int:
    return sum(1 for c in s if "\u4e00" <= c <= "\u9fff")


def check_mojibake() -> None:
    """侦测「UTF-8 被按 GBK 解码后又存成 UTF-8」的乱码。

    真实踩过这个坑：用 Windows PowerShell 5.1 的 `Get-Content -Raw`（默认按系统
    ANSI 代码页解码，简中机器上是 GBK）读一个 UTF-8 文件，再用
    `Set-Content -Encoding UTF8` 写回去，中文就变成
    「鏃犲伐鍏烽摼」这种乱码；更糟的是个别字节会丢成 `?`，足以让 shell 脚本的
    引号不配对，CI 直接报 `unexpected EOF while looking for matching '`。

    判定方式（两种，互补）：
      1. 逐行做「gbk 编码 → utf-8 解码」往返。真中文的 GBK 字节几乎不可能
         是合法 UTF-8（会解码失败），而乱码往返后一定变得更像中文。
         逐行而不是整文件，是因为文件里只要有一个坏字节，整文件往返就会失败。
      2. 出现 Unicode 私有使用区字符（U+E000–U+F8FF）。源码里出现这些字符
         基本只可能是「有字节没有对应 GBK 码位、被替换掉」留下的残渣 ——
         这一条专治上面往返检测漏掉的**有损**乱码（那种情况下还有 `?` 顶替，
         往返会直接失败，检测不到）。
    """
    for dirpath, dirnames, files in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", "dist", "__pycache__")]
        for name in files:
            if not name.endswith(MOJIBAKE_EXTS):
                continue
            path = os.path.join(dirpath, name)
            try:
                lines = read(path).split("\n")
            except OSError:
                continue
            for lineno, line in enumerate(lines, 1):
                # 2) 私有使用区残渣
                pua = [c for c in line if "\ue000" <= c <= "\uf8ff"]
                if pua:
                    err(
                        f"T: {rel(path)}:{lineno} 含 Unicode 私有使用区字符 "
                        f"U+{ord(pua[0]):04X} —— 典型的「UTF-8 被按 GBK 解码」有损乱码"
                        f"（注意：这种乱码还会吃掉换行，足以让 shell 引号不配对）"
                    )
                    continue
                if not any(ord(c) > 0x2FFF for c in line):
                    continue  # 纯 ASCII / 拉丁文，不可能是这种乱码
                # 1) gbk → utf-8 往返
                try:
                    back = line.encode("gbk").decode("utf-8")
                except (UnicodeEncodeError, UnicodeDecodeError):
                    continue
                if _cjk_count(back) > _cjk_count(line):
                    err(
                        f"T: {rel(path)}:{lineno} 疑似 GBK 乱码（UTF-8 被按 GBK 解码后再存）"
                        f"，正确内容应类似: {back.strip()[:40]}"
                    )


def main() -> int:
    fix = "--fix" in sys.argv
    print(f"校验根目录: {ROOT}" + ("  [--fix 模式]\n" if fix else "\n"))
    if fix:
        n = fix_qml_module()
        n += fix_ps1_bom()
        print(f"  规范化了 {n} 处\n")
    check_metadata()
    check_pro()
    registered = check_qmldir()
    check_qml_files(registered)
    check_qml_imports()
    check_qml_relative_paths()
    check_cpp_pairs()
    check_cpp_signatures()
    check_invokable_and_properties()
    check_go_stdlib()
    check_go_routes()
    check_qml_cpp_api()
    check_qml_model_roles()
    check_blogcard_keys()
    check_ps1_bom()
    check_most_vexing_parse()
    check_lambda_this()
    check_mojibake()
    check_font()
    check_qml_duplicate_properties()
    check_component_props()
    check_workflow_yaml()

    for w in warnings:
        print(f"  [warn] {w}")
    for e in errors:
        print(f"  [ERR ] {e}")
    print()
    print(f"错误 {len(errors)} 条，警告 {len(warnings)} 条")
    if errors:
        return 1
    if STRICT and warnings:
        return 1
    # 注意：不要在这里用 emoji（Windows 控制台可能是 GBK，会 UnicodeEncodeError）
    print("结构校验通过 [PASS] —— 注意：这不能替代真正的编译")
    return 0


if __name__ == "__main__":
    sys.exit(main())
