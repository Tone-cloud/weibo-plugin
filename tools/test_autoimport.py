#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""自动导入（电脑端 → 词典笔）集成测试。

不需要真实词典笔：在本机跑一个 host 版 sidecar 当"设备"，验证
  * POST /config/import 的四种请求体写法
  * 离线/校验失败时也必须落盘（这是自动导入的关键）
  * cookies.json 被外部改动 → 文件监听 2 秒内自动加载（adb push 的路径）
  * 带 UTF-8 BOM 的文件也能读（记事本存过的）
  * 错误分支（空体 / 没有 SUB / 方法不对）
  * GET /server/state 只读本地缓存、不请求上游

用法：
    # 先编译 host 版 server（不对设备交叉编译）
    cd go_server/main && CGO_ENABLED=0 go build -o ../../server_host .

    python3 tools/test_autoimport.py                 # 自动找 server_host
    python3 tools/test_autoimport.py /path/to/server # 指定可执行文件

退出码 0 = 全部通过。
"""

import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = os.path.join(ROOT, ".testtmp")
COOKIE = os.path.join(TMP, "cookies.json")
PORT = 18011
BASE = f"http://127.0.0.1:{PORT}"


def find_exe() -> str:
    if len(sys.argv) > 1:
        return sys.argv[1]
    for name in ("server_host.exe", "server_host", "server_test.exe", "server_test"):
        p = os.path.join(ROOT, name)
        if os.path.exists(p):
            return p
    print("找不到 host 版 server。先执行：")
    print("  cd go_server/main && CGO_ENABLED=0 go build -o ../../server_host .")
    sys.exit(2)


EXE = find_exe()
LOG = os.path.join(TMP, "server.log")

ok = True


def check(label, cond, extra=""):
    global ok
    text = " ".join(str(e) for e in extra) if isinstance(extra, (list, tuple)) else str(extra)
    print(("  OK   " if cond else "  FAIL ") + label + (("  " + text) if text else ""))
    if not cond:
        ok = False


def req(path, method="GET", body=None, ctype="application/json"):
    data = body.encode("utf-8") if isinstance(body, str) else body
    r = urllib.request.Request(BASE + path, data=data, method=method)
    if body is not None:
        r.add_header("Content-Type", ctype)
    try:
        with urllib.request.urlopen(r, timeout=25) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def main() -> int:
    shutil.rmtree(TMP, ignore_errors=True)
    os.makedirs(TMP, exist_ok=True)

    env = dict(os.environ)
    env["PORT"] = str(PORT)
    env["WEIBO_COOKIE_FILE"] = COOKIE
    env["WEIBO_PLUGIN_DIR"] = TMP
    env.pop("DEBUG", None)  # 默认只监听 127.0.0.1

    logf = open(LOG, "wb")
    proc = subprocess.Popen([EXE], env=env, stdout=logf, stderr=subprocess.STDOUT)
    try:
        print("== 启动 ==")
        ready = False
        for _ in range(60):
            try:
                if req("/server/ping")[0] == 200:
                    ready = True
                    break
            except Exception:
                pass
            time.sleep(0.25)
        check("sidecar 已就绪", ready, EXE)
        if not ready:
            print(open(LOG, encoding="utf-8", errors="replace").read())
            return 1

        print("\n== 1. GET /server/state（未登录、无文件）==")
        s, b = req("/server/state")
        d = json.loads(b)["data"]
        check("200 + code 0", s == 200 and json.loads(b)["code"] == 0)
        check("logged_in=false", d["logged_in"] is False)
        check("verified=false", d["verified"] is False)
        check("带 cookie_file / cookie_rev / plugin_dir",
              all(k in d for k in ("cookie_file", "cookie_rev", "plugin_dir")))

        print("\n== 2. POST /config/import 发 cookies.json 原文 ==")
        payload = {"cookies": [
            {"name": "SUB", "value": "FAKE_SUB_FROM_PC_0001", "domain": ".weibo.com", "path": "/"},
            {"name": "SUBP", "value": "FAKE_SUBP_FROM_PC_0002", "domain": ".weibo.com", "path": "/"},
        ], "updated_at": 0}
        s, b = req("/config/import", "POST", json.dumps(payload, ensure_ascii=False))
        r = json.loads(b)
        check("200 + code 0", s == 200 and r["code"] == 0)
        check("ok=true（已接受）", r["data"].get("ok") is True)
        check("saved=true", r["data"].get("saved") is True)
        check("logged_in=true（本地已持有票据）", r["data"].get("logged_in") is True)
        check("verified=false（假 Cookie 上游不认）", r["data"].get("verified") is False)
        check("带 message 提示未通过校验", bool(r["data"].get("message")))

        print("\n== 3. 关键：离线/校验失败也必须落盘 ==")
        check("cookies.json 已生成", os.path.exists(COOKIE))
        if os.path.exists(COOKIE):
            saved = json.load(open(COOKIE, encoding="utf-8"))
            names = sorted({c["name"] for c in saved["cookies"]})
            # 同一张票据会写到 .weibo.com 与 .weibo.cn 两个域，条目数比输入多属预期
            check("SUB/SUBP 都在", names == ["SUB", "SUBP"], names)
            check("updated_at 已写入", saved.get("updated_at", 0) > 0)

        print("\n== 4. 外部改文件 → 文件监听自动生效（adb push 的路径）==")
        before = json.loads(req("/server/state")[1])["data"]["cookie_rev"]
        with open(COOKIE, "w", encoding="utf-8") as f:
            json.dump({"cookies": [{"name": "SUB", "value": "FAKE_PUSHED_BY_ADB",
                                    "domain": ".weibo.com"}], "updated_at": 0}, f)
        time.sleep(4.0)
        after = json.loads(req("/server/state")[1])["data"]["cookie_rev"]
        check("cookie_rev 变化", before != after, f"{before} -> {after}")
        log = open(LOG, encoding="utf-8", errors="replace").read()
        check("日志出现『检测到 cookies.json 被外部修改』", "检测到 cookies.json 被外部修改" in log)

        print("\n== 5. 带 UTF-8 BOM 的文件也要能读（记事本存过的）==")
        with open(COOKIE, "wb") as f:
            f.write(b"\xef\xbb\xbf" + json.dumps(
                {"cookies": [{"name": "SUB", "value": "FAKE_BOM_VALUE",
                              "domain": ".weibo.com"}], "updated_at": 0}).encode())
        time.sleep(4.0)
        log = open(LOG, encoding="utf-8", errors="replace").read()
        check("带 BOM 没有导致读取失败", "读取失败" not in log)

        print("\n== 6. 其它三种请求体写法 ==")
        check("裸数组 [{...}]", json.loads(req(
            "/config/import", "POST", json.dumps([{"name": "SUB", "value": "FAKE_ARRAY"}]))[1]
            )["data"].get("ok") is True)
        check('{"cookie":"SUB=..."}', json.loads(req(
            "/config/import", "POST", json.dumps({"cookie": "SUB=FAKE_HEADER"}))[1]
            )["data"].get("ok") is True)
        check("纯文本 SUB=...", json.loads(req(
            "/config/import", "POST", "SUB=FAKE_PLAIN", ctype="text/plain")[1]
            )["data"].get("ok") is True)

        print("\n== 7. 错误分支 ==")
        s, b = req("/config/import", "POST", "")
        check("空请求体 → 400", s == 400 and json.loads(b)["code"] != 0, b[:110])
        s, b = req("/config/import", "POST", json.dumps({"cookies": [{"name": "OTHER", "value": "x"}]}))
        check("没有 SUB → 400", s == 400 and json.loads(b)["code"] != 0, b[:110])
        s, b = req("/config/import", "GET")
        check("GET 该路由 → 405", s == 405, b[:80])
        s, b = req("/login/import", "POST", json.dumps({"cookie": "SUB=FAKE_QML_PASTE"}))
        check("旧的 /login/import 仍兼容", s == 200 and json.loads(b)["data"].get("ok") is True)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill()
        logf.close()

    print("\n结果:", "全部通过" if ok else "有失败项")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
