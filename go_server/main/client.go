package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/http/cookiejar"
	"net/url"
	"strconv"
	"strings"
	"sync"
	"time"
)

// maxUpstreamBody 限制单次上游响应体大小（8MB），避免异常响应吃满内存。
const maxUpstreamBody = 8 << 20

// clientSnapshot 是 /server/ping、/server/state 与 /login/info 共用的登录态快照。
type clientSnapshot struct {
	// LoggedIn 表示「本地持有登录票据」（有 SUB/SUBP）。离线时也为 true，
	// 这样界面能照常显示账号，只是请求会失败。
	LoggedIn bool `json:"logged_in"`
	// Verified 表示「上游 /api/config 确认过登录态」。Cookie 过期或访问不到
	// 微博时为 false —— 电脑端导入后要靠它判断是否真的生效。
	Verified   bool   `json:"verified"`
	UID        int64  `json:"uid"`
	ScreenName string `json:"screen_name"`
	Avatar     string `json:"avatar"`
	ExpiresAt  int64  `json:"expires_at"`
}

// swappableJar 包一层 cookiejar，使「整体替换 Cookiejar」在并发下也安全。
// http.Client.Jar 只在构造时赋值一次，之后永远指向这个包装体。
type swappableJar struct {
	mu  sync.RWMutex
	jar http.CookieJar
}

// newSwappableJar 创建包装体。
func newSwappableJar() *swappableJar {
	inner, _ := cookiejar.New(nil)
	return &swappableJar{jar: inner}
}

// Cookies 实现 http.CookieJar。
func (s *swappableJar) Cookies(u *url.URL) []*http.Cookie {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.jar.Cookies(u)
}

// SetCookies 实现 http.CookieJar。
func (s *swappableJar) SetCookies(u *url.URL, cookies []*http.Cookie) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.jar.SetCookies(u, cookies)
}

// reset 清空全部 Cookie（换一个全新的 Cookiejar）。
func (s *swappableJar) reset() {
	inner, _ := cookiejar.New(nil)
	s.mu.Lock()
	s.jar = inner
	s.mu.Unlock()
}

// WeiboClient 是访问微博上游的统一客户端。
// 所有导出方法都可以在多个 goroutine 中并发调用。
type WeiboClient struct {
	httpClient *http.Client
	jar        *swappableJar

	mu         sync.RWMutex
	subValue   string
	subpValue  string
	uidValue   string
	screenName string
	avatar     string
	loggedIn   bool
	expiresAt  int64
}

// NewWeiboClient 创建客户端；sub/subp 可以留空，之后由 /login/import 注入。
func NewWeiboClient(sub, subp string) *WeiboClient {
	jar := newSwappableJar()
	client := &WeiboClient{
		jar:        jar,
		subValue:   strings.TrimSpace(sub),
		subpValue:  strings.TrimSpace(subp),
		httpClient: &http.Client{Timeout: 30 * time.Second, Jar: jar},
	}
	return client
}

// Init 加载持久化 Cookie 并校验一次登录态。
// main.go 在端口就绪之后才异步调用它，避免拖慢 sidecar 启动。
func (c *WeiboClient) Init() {
	list, err := loadCookies()
	if err != nil {
		logWarn("读取本地 Cookie 失败：%v", err)
	}
	if len(list) > 0 {
		c.loadCookieStore(list)
		logInfo("已从 %s 载入 %d 条 Cookie", cookieStorePath(), len(list))
	} else {
		logInfo("未发现本地 Cookie（%s），等待 /login/import", cookieStorePath())
	}
	if c.hasLogin() {
		c.refreshLoginState()
	}
	if c.hasLogin() {
		logSuccess("微博登录态就绪（uid=%s）", c.uid())
	} else {
		logWarn("当前未登录，请通过 /login/import 导入 Cookie")
	}
}

// loadCookieStore 把持久化的 Cookie 灌进 Cookiejar。
// 同一条 Cookie 会同时写到 .weibo.com 与 .weibo.cn，两个域的接口都能带上。
func (c *WeiboClient) loadCookieStore(list []storedCookie) {
	for _, item := range list {
		name := strings.TrimSpace(item.Name)
		value := item.Value
		if name == "" || value == "" {
			continue
		}
		for _, domain := range cookieDomainsFor(item.Domain) {
			c.setJarCookie(domain, name, value)
		}
		switch name {
		case "SUB":
			c.mu.Lock()
			c.subValue = value
			c.mu.Unlock()
		case "SUBP":
			c.mu.Lock()
			c.subpValue = value
			c.mu.Unlock()
		default:
			// 其它 Cookie（XSRF-TOKEN 等）只留在 Cookiejar 里，读取时走 xsrfToken()。
		}
	}
}

// cookieDomainsFor 给一条 Cookie 计算要写入的域列表。
//
// 说明：SUB / SUBP 这类登录票据同时在 weibo.com 与 m.weibo.cn 使用，
// 而 java Cookiejar 的域匹配很严格，所以两个域都写一份。
func cookieDomainsFor(domain string) []string {
	d := strings.TrimSpace(domain)
	raw := make([]string, 0, 3)
	if d != "" {
		raw = append(raw, d)
	}
	if strings.Contains(d, "weibo.cn") {
		raw = append(raw, ".weibo.cn", ".weibo.com")
	} else {
		raw = append(raw, ".weibo.com", ".weibo.cn")
	}
	return dedupeStrings(raw)
}

// setJarCookie 往 Cookiejar 写一条 Cookie。
func (c *WeiboClient) setJarCookie(domain, name, value string) {
	d := strings.TrimSpace(domain)
	if d == "" {
		d = ".weibo.com"
	}
	host := "weibo.com"
	if strings.Contains(d, "weibo.cn") {
		host = "m.weibo.cn"
	}
	target := &url.URL{Scheme: "https", Host: host, Path: "/"}
	c.jar.SetCookies(target, []*http.Cookie{{
		Name:   name,
		Value:  value,
		Path:   "/",
		Domain: d,
	}})
}

// setCookiesFromHeader 解析 /login/import 传来的内容。
// 既接受完整的 Cookie 头（"SUB=xxx; SUBP=yyy"），也接受裸 SUB 值。
func (c *WeiboClient) setCookiesFromHeader(raw string) {
	text := strings.TrimSpace(raw)
	if text == "" {
		return
	}
	lower := strings.ToLower(text)
	if strings.HasPrefix(lower, "cookie:") {
		text = strings.TrimSpace(text[len("cookie:"):])
	}
	if !strings.Contains(text, "=") {
		// 只给了裸 SUB 值：登录票据本身，同时也补一份 SUBP 占位。
		c.loadCookieStore([]storedCookie{{Name: "SUB", Value: text, Domain: ".weibo.com"}})
		return
	}
	list := make([]storedCookie, 0, 8)
	for _, part := range strings.Split(text, ";") {
		piece := strings.TrimSpace(part)
		if piece == "" {
			continue
		}
		kv := strings.SplitN(piece, "=", 2)
		if len(kv) != 2 {
			continue
		}
		name := strings.TrimSpace(kv[0])
		value := strings.TrimSpace(kv[1])
		if name == "" || value == "" {
			continue
		}
		list = append(list, storedCookie{Name: name, Value: value, Domain: ".weibo.com"})
	}
	c.loadCookieStore(list)
}

// cookieHeader 拼出当前所有 Cookie（调试 / 日志用）。
func (c *WeiboClient) cookieHeader() string {
	seen := map[string]bool{}
	parts := make([]string, 0, 16)
	for _, raw := range cookieProbeURLs {
		u, err := url.Parse(raw)
		if err != nil {
			continue
		}
		for _, cookie := range c.jar.Cookies(u) {
			if cookie.Name == "" || seen[cookie.Name] {
				continue
			}
			seen[cookie.Name] = true
			parts = append(parts, cookie.Name+"="+cookie.Value)
		}
	}
	return strings.Join(parts, "; ")
}

// jarHasCookie 判断 Cookiejar 里是否存在某个非空 Cookie。
func (c *WeiboClient) jarHasCookie(name string) bool {
	for _, raw := range cookieProbeURLs {
		u, err := url.Parse(raw)
		if err != nil {
			continue
		}
		for _, cookie := range c.jar.Cookies(u) {
			if cookie.Name == name && cookie.Value != "" {
				return true
			}
		}
	}
	return false
}

// hasLogin 判断本地是否持有登录票据（不请求上游）。
func (c *WeiboClient) hasLogin() bool {
	c.mu.RLock()
	sub := c.subValue
	subp := c.subpValue
	c.mu.RUnlock()
	if sub != "" || subp != "" {
		return true
	}
	return c.jarHasCookie("SUB") || c.jarHasCookie("SUBP")
}

// uid 返回当前登录用户 uid（字符串，可能为空）。
func (c *WeiboClient) uid() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.uidValue
}

// sub 返回当前 SUB 票据。
func (c *WeiboClient) sub() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.subValue
}

// xsrfToken 读取并 url 解码 XSRF-TOKEN，weibo.com 写操作必须带上。
func (c *WeiboClient) xsrfToken() string {
	for _, raw := range cookieProbeURLs {
		u, err := url.Parse(raw)
		if err != nil {
			continue
		}
		for _, cookie := range c.jar.Cookies(u) {
			if cookie.Name != "XSRF-TOKEN" || cookie.Value == "" {
				continue
			}
			if decoded, err := url.QueryUnescape(cookie.Value); err == nil {
				return decoded
			}
			return cookie.Value
		}
	}
	return ""
}

// snapshot 返回一份并发安全的登录态快照。
func (c *WeiboClient) snapshot() clientSnapshot {
	c.mu.RLock()
	defer c.mu.RUnlock()
	uidNum, _ := strconv.ParseInt(c.uidValue, 10, 64)
	return clientSnapshot{
		LoggedIn:   c.loggedIn || c.subValue != "" || c.subpValue != "",
		Verified:   c.loggedIn,
		UID:        uidNum,
		ScreenName: c.screenName,
		Avatar:     c.avatar,
		ExpiresAt:  c.expiresAt,
	}
}

// setLoginState 更新缓存的登录态。
func (c *WeiboClient) setLoginState(loggedIn bool, uid, screenName, avatar string, expiresAt int64) {
	c.mu.Lock()
	c.loggedIn = loggedIn
	c.uidValue = strings.TrimSpace(uid)
	c.screenName = screenName
	c.avatar = avatar
	c.expiresAt = expiresAt
	if !loggedIn {
		c.uidValue = ""
		c.screenName = ""
		c.avatar = ""
		c.expiresAt = 0
	}
	c.mu.Unlock()
}

// resetCookies 清空全部 Cookie 与登录态（/logout 与重新导入前调用）。
func (c *WeiboClient) resetCookies() {
	c.jar.reset()
	c.mu.Lock()
	c.subValue = ""
	c.subpValue = ""
	c.uidValue = ""
	c.screenName = ""
	c.avatar = ""
	c.loggedIn = false
	c.expiresAt = 0
	c.mu.Unlock()
}

// snapshotCookies 把 Cookiejar 里的 Cookie 导出成可持久化的结构。
func (c *WeiboClient) snapshotCookies() []storedCookie {
	out := make([]storedCookie, 0, 16)
	seen := map[string]bool{}
	for _, raw := range cookieProbeURLs {
		u, err := url.Parse(raw)
		if err != nil {
			continue
		}
		fallbackDomain := ".weibo.com"
		if strings.HasSuffix(u.Host, "weibo.cn") {
			fallbackDomain = ".weibo.cn"
		}
		for _, cookie := range c.jar.Cookies(u) {
			if cookie.Name == "" || cookie.Value == "" {
				continue
			}
			domain := firstNonEmpty(cookie.Domain, fallbackDomain)
			key := cookie.Name + "|" + domain
			if seen[key] {
				continue
			}
			seen[key] = true
			out = append(out, storedCookie{
				Name:   cookie.Name,
				Value:  cookie.Value,
				Domain: domain,
				Path:   firstNonEmpty(cookie.Path, "/"),
			})
		}
	}
	return out
}

// persistCookies 把当前 Cookie 落盘。
func (c *WeiboClient) persistCookies() error {
	list := c.snapshotCookies()
	if len(list) == 0 {
		// 没有登录票据时不要写空文件覆盖已有内容。
		if !c.hasLogin() {
			return nil
		}
	}
	return saveCookies(list)
}

// requireLogin 在未登录时返回 SPEC 约定的 -100。
func (c *WeiboClient) requireLogin() error {
	if !c.hasLogin() {
		return errNotLoggedIn()
	}
	return nil
}

// ======================= 请求发送 =======================

// applyHeaders 注入统一请求头（SPEC 第 5 节「请求头」）。
func (c *WeiboClient) applyHeaders(req *http.Request, headers map[string]string) {
	if req.Header.Get("User-Agent") == "" {
		req.Header.Set("User-Agent", defaultUserAgent)
	}
	if req.Header.Get("Accept") == "" {
		req.Header.Set("Accept", "application/json, text/plain, */*")
	}
	req.Header.Set("Accept-Language", acceptLanguage)
	req.Header.Set("X-Requested-With", "XMLHttpRequest")
	if req.Header.Get("Referer") == "" {
		req.Header.Set("Referer", defaultReferer)
	}
	for key, value := range headers {
		if strings.TrimSpace(value) == "" {
			continue
		}
		req.Header.Set(key, value)
	}
}

// do 发送请求并裁出 JSON 对象。
func (c *WeiboClient) do(req *http.Request, label string) (json.RawMessage, error) {
	start := time.Now()
	path := req.URL.Path
	logRequest(req.Method, path, req.URL.RawQuery)
	resp, err := c.httpClient.Do(req)
	if err != nil {
		logError("请求上游失败 %s：%v", label, err)
		return nil, fmt.Errorf("访问微博接口失败：%w", err)
	}
	defer func() {
		_ = resp.Body.Close()
	}()
	body, err := io.ReadAll(io.LimitReader(resp.Body, maxUpstreamBody))
	if err != nil {
		logError("读取上游响应失败 %s：%v", label, err)
		return nil, fmt.Errorf("读取微博响应失败：%w", err)
	}
	logResponse(path, resp.StatusCode, time.Since(start))
	payload, ok := extractJSONObject(body)
	if !ok {
		if resp.StatusCode != http.StatusOK {
			return nil, fmt.Errorf("微博接口返回 HTTP %d", resp.StatusCode)
		}
		return nil, errors.New("微博接口返回了非 JSON 内容")
	}
	if resp.StatusCode != http.StatusOK {
		root := decodeJSONMap(payload)
		message := firstNonEmpty(asString(root["msg"]), asString(root["message"]), asString(root["error"]))
		if message == "" {
			message = fmt.Sprintf("微博接口返回 HTTP %d", resp.StatusCode)
		}
		if resp.StatusCode == http.StatusForbidden && isLoginErrorText(message) {
			return nil, errLoginExpired()
		}
		return nil, errors.New(message)
	}
	return json.RawMessage(payload), nil
}

// GetJSON 发送 GET 并返回 JSON 对象。
func (c *WeiboClient) GetJSON(rawURL string, headers map[string]string) (json.RawMessage, error) {
	req, err := http.NewRequest(http.MethodGet, rawURL, nil)
	if err != nil {
		return nil, fmt.Errorf("构造请求失败：%w", err)
	}
	c.applyHeaders(req, headers)
	return c.do(req, rawURL)
}

// PostForm 发送 application/x-www-form-urlencoded 请求。
func (c *WeiboClient) PostForm(rawURL string, form url.Values, headers map[string]string) (json.RawMessage, error) {
	body := ""
	if form != nil {
		body = form.Encode()
	}
	req, err := http.NewRequest(http.MethodPost, rawURL, strings.NewReader(body))
	if err != nil {
		return nil, fmt.Errorf("构造请求失败：%w", err)
	}
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	c.applyHeaders(req, headers)
	return c.do(req, rawURL)
}

// PostJSON 发送 JSON 请求体。
func (c *WeiboClient) PostJSON(rawURL string, body any, headers map[string]string) (json.RawMessage, error) {
	var reader io.Reader
	if body != nil {
		raw, err := json.Marshal(body)
		if err != nil {
			return nil, fmt.Errorf("序列化请求体失败：%w", err)
		}
		reader = bytes.NewReader(raw)
	}
	req, err := http.NewRequest(http.MethodPost, rawURL, reader)
	if err != nil {
		return nil, fmt.Errorf("构造请求失败：%w", err)
	}
	req.Header.Set("Content-Type", "application/json; charset=utf-8")
	c.applyHeaders(req, headers)
	return c.do(req, rawURL)
}

// mergeQuery 把参数拼到 URL 上（自动判断 ? 还是 &）。
func mergeQuery(rawURL string, params url.Values) string {
	if len(params) == 0 {
		return rawURL
	}
	encoded := params.Encode()
	if encoded == "" {
		return rawURL
	}
	if strings.Contains(rawURL, "?") {
		return rawURL + "&" + encoded
	}
	return rawURL + "?" + encoded
}

// mGet 访问 m.weibo.cn 的 GET 接口。
func (c *WeiboClient) mGet(path string, params url.Values) (json.RawMessage, error) {
	return c.GetJSON(mergeQuery(path, params), map[string]string{"Referer": defaultReferer})
}

// mPost 访问 m.weibo.cn 的 POST 接口（带 XSRF-TOKEN）。
func (c *WeiboClient) mPost(path string, form url.Values) (json.RawMessage, error) {
	headers := map[string]string{"Referer": defaultReferer}
	if token := c.xsrfToken(); token != "" {
		headers["X-XSRF-TOKEN"] = token
	}
	return c.PostForm(path, form, headers)
}

// webGet 访问 weibo.com 的 GET 接口（带 XSRF-TOKEN）。
func (c *WeiboClient) webGet(path string, params url.Values) (json.RawMessage, error) {
	headers := map[string]string{"Referer": webReferer}
	if token := c.xsrfToken(); token != "" {
		headers["X-XSRF-TOKEN"] = token
		headers["X-CSRF-Token"] = token
	}
	return c.GetJSON(mergeQuery(path, params), headers)
}

// webPost 访问 weibo.com 的 POST 接口（带 XSRF-TOKEN，写操作必需）。
func (c *WeiboClient) webPost(path string, form url.Values) (json.RawMessage, error) {
	headers := map[string]string{"Referer": webReferer}
	if token := c.xsrfToken(); token != "" {
		headers["X-XSRF-TOKEN"] = token
		headers["X-CSRF-Token"] = token
	}
	return c.PostForm(path, form, headers)
}
