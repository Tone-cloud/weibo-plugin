package main

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"strings"
	"time"
)

// ============================================================================
// 「电脑端导入」登录页（独立监听 0.0.0.0）
//
// 思路与参考实现一致：
//   * BiliPocket：bili-sms 监听 0.0.0.0:8666，浏览器打开
//     http://<词典笔IP>:8666/verify.html
//   * netease-music：登录服务监听 0.0.0.0:8667，浏览器打开
//     http://<词典笔IP>:8667/verify.html（主 API 仍留在 127.0.0.1）
//
// 主 API 继续只监听 127.0.0.1（安全），**只有这一张粘贴 Cookie 的页面**
// 暴露给局域网。整份实现就在这一个文件里，不引第三方依赖、页面不引外部资源
// （PC 可能离线，笔上也没有公网 CDN 可用）。
//
// 相比参考实现多了一点：URL 里带一次性随机 token。词典笔界面上会显示完整
// 链接，用户直接照抄，因此对用户是零成本的；但同网段的陌生设备没法凭空猜出
// 路径来改写你笔上的登录态。
//   WEIBO_LOGIN_PORT=0       关闭这个服务
//   WEIBO_LOGIN_BIND=...     改监听地址（默认 0.0.0.0）
//   WEIBO_LOGIN_PORT=80xx    改端口（默认 8011，刻意避开 bili 的 8666 / netease 的 8667）
// ============================================================================

const defaultLoginPort = "8011"

var loginPageToken = newLoginToken()

func newLoginToken() string {
	// 4 字节 → 8 个十六进制字符。够挡住「同网段随便扫端口就能改你登录态」，
	// 又短到用户愿意从词典笔屏幕上抄一遍。
	buf := make([]byte, 4)
	if _, err := rand.Read(buf); err != nil {
		return fmt.Sprintf("%08x", time.Now().UnixNano()&0xFFFFFFFF)
	}
	return hex.EncodeToString(buf)
}

// loginPageAddr 返回登录页监听地址；返回空串表示已关闭。
func loginPageAddr() string {
	port := strings.TrimSpace(os.Getenv("WEIBO_LOGIN_PORT"))
	if port == "" {
		port = defaultLoginPort
	}
	if port == "0" || port == "off" {
		return ""
	}
	host := strings.TrimSpace(os.Getenv("WEIBO_LOGIN_BIND"))
	if host == "" {
		host = "0.0.0.0"
	}
	return net.JoinHostPort(host, port)
}

// loginURLs 返回全部候选链接（已按「电脑能否连上」排序），最多 3 条。
// 第一条给界面显示；后面几条写日志，万一第一条打不开还能试别的。
func loginURLs() []string {
	addr := loginPageAddr()
	if addr == "" {
		return nil
	}
	_, port, err := net.SplitHostPort(addr)
	if err != nil {
		return nil
	}
	ips := lanIPv4()
	if len(ips) == 0 {
		return []string{fmt.Sprintf("http://<词典笔IP>:%s/%s", port, loginPageToken)}
	}
	if len(ips) > 3 {
		ips = ips[:3]
	}
	out := make([]string, 0, len(ips))
	for _, ip := range ips {
		out = append(out, fmt.Sprintf("http://%s:%s/%s", ip, port, loginPageToken))
	}
	return out
}

// loginURL 返回给用户照抄的链接（第一条候选）。
func loginURL() string {
	urls := loginURLs()
	if len(urls) == 0 {
		return ""
	}
	return urls[0]
}

// startLoginPage 起独立监听；失败只警告，绝不影响主服务与插件本身。
func startLoginPage() {
	addr := loginPageAddr()
	if addr == "" {
		logInfo("电脑端导入页已关闭（WEIBO_LOGIN_PORT=0）")
		return
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/", handleLoginPageIndex)
	mux.HandleFunc("/"+loginPageToken, handleLoginPageIndex)
	mux.HandleFunc("/"+loginPageToken+"/state", handleLoginPageState)
	mux.HandleFunc("/"+loginPageToken+"/import", handleLoginPageImport)
	mux.HandleFunc("/"+loginPageToken+"/cookies.json", handleLoginPageCookies)

	server := &http.Server{
		Addr:              addr,
		Handler:           mux,
		ReadHeaderTimeout: 10 * time.Second,
		WriteTimeout:      30 * time.Second,
	}

	urls := loginURLs()
	logSuccess("电脑端导入页：%s", loginURL())
	for _, u := range urls[minInt(1, len(urls)):] {
		logPlain("  备用地址（第一条打不开时试试）：%s", u)
	}

	// 端口可能被占（比如上一次的 sidecar 还没退干净），失败就只警告。
	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		logWarn("电脑端导入页启动失败（不影响插件使用）：%v", err)
	}
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// handleLoginPageIndex 返回粘贴页。
//
// 只有带 token 的路径才给页面；裸 "/" 给一个说明页 —— 这样陌生设备猜不到入口，
// 而用户只要照抄词典笔上显示的完整链接就行。
func handleLoginPageIndex(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/"+loginPageToken {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.WriteHeader(http.StatusNotFound)
		_, _ = io.WriteString(w, loginPagePlaceholderHTML)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	_, _ = io.WriteString(w, renderLoginPage())
}

// handleLoginPageState 给页面显示当前登录态用（只读本地缓存，不请求上游）。
func handleLoginPageState(w http.ResponseWriter, r *http.Request) {
	state, err := localState()
	if err != nil {
		writeError(w, err)
		return
	}
	writeOK(w, state)
}

// handleLoginPageImport 接收页面提交的 Cookie / cookies.json。
//
// 表单字段 payload（textarea 的内容，或上传文件读出来的文本）。
func handleLoginPageImport(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeJSON(w, http.StatusMethodNotAllowed, map[string]any{
			"code": -1, "message": "请用 POST 提交", "data": map[string]any{},
		})
		return
	}
	if err := r.ParseForm(); err != nil {
		writeError(w, errBadRequest("表单解析失败"))
		return
	}
	payload := r.PostFormValue("payload")
	if strings.TrimSpace(payload) == "" {
		payload = r.PostFormValue("cookie")
	}
	if strings.TrimSpace(payload) == "" {
		writeError(w, errBadRequest("请先粘贴 Cookie 或选择 cookies.json 文件"))
		return
	}
	out, err := importCookiesJSON([]byte(payload))
	if err != nil {
		writeError(w, err)
		return
	}
	writeOK(w, out)
}

// handleLoginPageCookies 把设备上现有的 cookies.json 下载回电脑（备份/换机器用）。
func handleLoginPageCookies(w http.ResponseWriter, r *http.Request) {
	list, err := loadCookies()
	if err != nil {
		writeError(w, err)
		return
	}
	raw, err := json.MarshalIndent(cookieFilePayload{
		Cookies:   list,
		UpdatedAt: time.Now().Unix(),
	}, "", "  ")
	if err != nil {
		writeError(w, err)
		return
	}
	raw = append(raw, '\n')
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Content-Disposition", `attachment; filename="cookies.json"`)
	_, _ = w.Write(raw)
}

// loginPagePlaceholderHTML 是猜错路径时看到的页面（纯静态，不含任何信息）。
const loginPagePlaceholderHTML = `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>笔里微博</title>
<style>
body{font-family:-apple-system,"Segoe UI","Microsoft YaHei",sans-serif;margin:0;
     padding:2.5rem 1.25rem;background:#141414;color:#e8e8e8;line-height:1.7}
.card{max-width:34rem;margin:0 auto;background:#1e1e1e;border:1px solid #2e2e2e;
      border-radius:12px;padding:1.5rem}
h1{font-size:1.15rem;margin:0 0 .75rem;color:#ff8200}
code{background:#2c2c2c;padding:.15rem .4rem;border-radius:4px}
</style></head><body><div class="card">
<h1>笔里微博 · 电脑端导入</h1>
<p>这个地址不带访问令牌，所以看不到导入页。</p>
<p>请在词典笔上打开 <b>设置 → 电脑端导入</b>，照抄那里显示的<b>完整链接</b>
（形如 <code>http://192.168.1.23:8011/xxxxxxxx</code>）再访问。</p>
</div></body></html>`

// loginPageHTML 是真正的导入页。用 __TOKEN__ 占位（不能用 Sprintf：
// CSS 里的 100% 会被当成格式动词）。
const loginPageHTML = `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>笔里微博 · 电脑端导入 Cookie</title>
<style>
*{box-sizing:border-box}
body{font-family:-apple-system,"Segoe UI","Microsoft YaHei",sans-serif;margin:0;
     padding:2rem 1.25rem;background:#141414;color:#e8e8e8;line-height:1.7}
.card{max-width:40rem;margin:0 auto;background:#1e1e1e;border:1px solid #2e2e2e;
      border-radius:12px;padding:1.5rem}
h1{font-size:1.2rem;margin:0 0 .35rem;color:#ff8200}
.sub{color:#a0a0a0;font-size:.9rem;margin:0 0 1.25rem}
ol{color:#c8c8c8;font-size:.9rem;padding-left:1.2rem;margin:0 0 1.25rem}
ol li{margin:.2rem 0}
textarea{width:100%;height:9rem;padding:.75rem;border-radius:8px;border:1px solid #3a3a3a;
         background:#2c2c2c;color:#f0f0f0;font-family:ui-monospace,Consolas,monospace;
         font-size:.85rem;resize:vertical}
textarea:focus{outline:2px solid #ff8200;outline-offset:1px}
.row{display:flex;gap:.75rem;align-items:center;flex-wrap:wrap;margin:1rem 0}
button{background:#ff8200;color:#fff;border:0;border-radius:8px;padding:.6rem 1.25rem;
       font-size:.95rem;cursor:pointer}
button:disabled{opacity:.55;cursor:default}
input[type=file]{color:#a0a0a0;font-size:.85rem}
#out{margin-top:1rem;padding:.75rem;border-radius:8px;display:none;font-size:.9rem;
     white-space:pre-wrap;word-break:break-all}
.ok{background:#16301b;border:1px solid #2e6b3a;color:#9ae6a8}
.warn{background:#3a2f14;border:1px solid #6b5a2e;color:#f0cf8a}
.err{background:#3a1717;border:1px solid #6b2e2e;color:#f0a0a0}
.foot{margin-top:1.5rem;padding-top:1rem;border-top:1px solid #2e2e2e;
      color:#8a8a8a;font-size:.85rem}
a{color:#ffa033}
code{background:#2c2c2c;padding:.1rem .35rem;border-radius:4px}
</style></head><body><div class="card">
<h1>笔里微博 · 电脑端导入 Cookie</h1>
<p class="sub">在电脑上完成登录，词典笔上不用打字。导入后笔上几秒内自动生效。</p>
<ol>
  <li>电脑浏览器登录 <b>weibo.com</b></li>
  <li>F12 → Application → Cookies → <code>https://weibo.com</code></li>
  <li>复制 <b>SUB</b> 与 <b>SUBP</b> 的值，粘到下面</li>
</ol>
<textarea id="payload" placeholder="SUB=xxxxx; SUBP=yyyyy&#10;（也可以直接粘贴 cookies.json 的内容）"></textarea>
<div class="row">
  <button id="go" type="button">导入到词典笔</button>
  <input type="file" id="file" accept=".json,.txt,application/json,text/plain">
</div>
<div id="out"></div>
<div class="foot">
  当前状态：<span id="state">读取中…</span><br>
  <a id="dl" href="#">下载词典笔上的 cookies.json</a>
</div>
</div>
<script>
var TOKEN = "__TOKEN__";
var out = document.getElementById('out');
var stateEl = document.getElementById('state');
document.getElementById('dl').setAttribute('href', TOKEN + '/cookies.json');

function show(kind, text) {
  out.className = kind;
  out.style.display = 'block';
  out.textContent = text;
}

function refreshState() {
  fetch(TOKEN + '/state', {cache: 'no-store'}).then(function (r) { return r.json(); })
    .then(function (j) {
      if (!j || j.code !== 0) { stateEl.textContent = '读取失败'; return; }
      var d = j.data;
      if (d.verified) {
        stateEl.textContent = '已登录：' + (d.screen_name || '(未返回昵称)') + '（uid ' + d.uid + '）';
      } else if (d.logged_in) {
        stateEl.textContent = '已保存 Cookie，但还没通过微博校验（可能已过期或当前离线）';
      } else {
        stateEl.textContent = '未登录';
      }
    }).catch(function () { stateEl.textContent = '读取失败'; });
}

document.getElementById('file').addEventListener('change', function (e) {
  var f = e.target.files && e.target.files[0];
  if (!f) return;
  var reader = new FileReader();
  reader.onload = function () {
    document.getElementById('payload').value = reader.result;
    show('ok', '已读入文件 ' + f.name + '，点「导入到词典笔」提交。');
  };
  reader.readAsText(f);
});

document.getElementById('go').addEventListener('click', function () {
  var payload = document.getElementById('payload').value;
  if (!payload || !payload.trim()) { show('err', '请先粘贴 Cookie 或选择 cookies.json 文件。'); return; }
  var btn = document.getElementById('go');
  btn.disabled = true;
  show('warn', '正在导入…');
  var body = 'payload=' + encodeURIComponent(payload);
  fetch(TOKEN + '/import', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
    body: body
  }).then(function (r) { return r.json(); }).then(function (j) {
    btn.disabled = false;
    if (!j || j.code !== 0) { show('err', '导入失败：' + ((j && j.message) || '未知错误')); return; }
    var d = j.data;
    if (d.verified) {
      show('ok', '导入成功，微博已确认登录：' + (d.screen_name || '(未返回昵称)') +
                 '\n词典笔上几秒内会出现昵称。');
    } else {
      show('warn', 'Cookie 已保存到词典笔' + (d.message ? ('，但' + d.message) : '') +
                   '\n（常见原因：Cookie 已过期，或词典笔当前连不上微博。）');
    }
    refreshState();
  }).catch(function (e) {
    btn.disabled = false;
    show('err', '请求失败：' + e);
  });
});

refreshState();
</script>
</body></html>`

// renderLoginPage 把 token 填进模板。
func renderLoginPage() string {
	return strings.ReplaceAll(loginPageHTML, "__TOKEN__", "/"+loginPageToken)
}
