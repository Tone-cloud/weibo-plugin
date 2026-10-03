package main

import (
	"bytes"
	"encoding/json"
	"net/url"
	"strconv"
	"strings"
)

// ============================================================================
// 登录：导入 Cookie / 登录态检查 / 退出登录 / 配置
//
// 微博没有开放 OAuth，插件走「用户粘贴 Cookie」的路线（SPEC 第 5 节）。
// ============================================================================

// refreshLoginState 请求 m.weibo.cn/api/config 并刷新本地登录态缓存。
func (c *WeiboClient) refreshLoginState() {
	raw, err := c.mGet(epMobileConfig, nil)
	if err != nil {
		logWarn("刷新登录态失败：%v", err)
		return
	}
	root := decodeJSONMap(raw)
	data := asMap(root["data"])
	if data == nil {
		data = root
	}
	if !asBool(data["login"]) {
		c.setLoginState(false, "", "", "", 0)
		logWarn("微博返回未登录状态，已清除本地登录信息")
		return
	}
	uid := asString(data["uid"])
	name := ""
	avatar := ""
	if user := parseUser(asMap(data["user"])); user != nil {
		if id := asInt64(user["id"]); id > 0 {
			uid = strconv.FormatInt(id, 10)
		}
		name = asString(user["name"])
		avatar = asString(user["avatar"])
	}
	c.setLoginState(true, uid, name, avatar, 0)
}

// importCookie 导入 Cookie。
//
// raw 可以是完整 Cookie 头（"SUB=...; SUBP=..."），也可以是裸 SUB 值。
//
// 语义（重要）：
//   - 只要解析出可用 Cookie 就先**落盘**。离线或上游风控时也能导入成功，
//     等联网后由监听/轮询自动生效；以前是「校验通过才存」，离线导入会白做。
//   - ok       = Cookie 已被接受并保存
//   - logged_in = 上游 /api/config 校验通过（离线时为 false）
func importCookie(raw string) (map[string]any, error) {
	text := strings.TrimSpace(raw)
	if text == "" {
		return nil, errBadRequest("Cookie 不能为空")
	}
	client := getClient()
	client.resetCookies()
	client.setCookiesFromHeader(text)
	if !client.hasLogin() {
		return nil, errBadRequest("Cookie 内容无法识别，请粘贴完整的 Cookie 或 SUB 值")
	}

	// 先落盘：这是「电脑端生成 JSON → 设备自动导入」的关键，
	// 不能因为一次上游请求失败就把用户的 Cookie 丢掉。
	if err := client.persistCookies(); err != nil {
		logWarn("保存 Cookie 失败：%v", err)
	} else {
		logSuccess("已保存 Cookie 到 %s", cookieStorePath())
	}

	client.refreshLoginState()
	snapshot := client.snapshot()

	out := map[string]any{}
	out["ok"] = true
	out["saved"] = true
	out["logged_in"] = snapshot.LoggedIn
	out["verified"] = snapshot.Verified
	out["uid"] = snapshot.UID
	out["screen_name"] = snapshot.ScreenName
	out["avatar"] = snapshot.Avatar
	out["cookie_file"] = cookieStorePath()
	out["cookie_rev"] = cookieFileRev()
	if !snapshot.Verified {
		out["message"] = "Cookie 已保存，但未通过登录校验（可能已过期，或当前无法访问微博）"
	}
	return out, nil
}

// importCookiesJSON 接受电脑端准备好的 JSON（POST /config/import 的请求体）。
//
// 支持四种写法，方便用户直接从浏览器/脚本里粘贴：
//  1. {"cookies":[{"name":"SUB","value":"...","domain":".weibo.com"}, ...]}  ← cookies.json 原文
//  2. [{"name":"SUB","value":"..."}]                                        ← 裸数组
//  3. {"cookie":"SUB=...; SUBP=..."}                                        ← Cookie 头
//  4. "SUB=...; SUBP=..." 或裸 SUB 值                                       ← 纯文本
func importCookiesJSON(raw []byte) (map[string]any, error) {
	text := strings.TrimSpace(string(stripBOM(raw)))
	if text == "" {
		return nil, errBadRequest("请求体为空")
	}
	if header, ok := cookieHeaderFromJSON([]byte(text)); ok {
		return importCookie(header)
	}
	// 不是 JSON 结构，按 Cookie 头 / 裸 SUB 处理
	return importCookie(text)
}

// cookieHeaderFromJSON 把 JSON 形式的 Cookie 转成 Cookie 头。
// ok=false 表示「这不是可识别的 JSON Cookie 结构」，由调用方按纯文本处理。
func cookieHeaderFromJSON(raw []byte) (string, bool) {
	trimmed := bytes.TrimSpace(raw)
	if len(trimmed) == 0 || (trimmed[0] != '{' && trimmed[0] != '[') {
		return "", false
	}

	// 1) 裸数组
	if trimmed[0] == '[' {
		var list []storedCookie
		if err := json.Unmarshal(trimmed, &list); err != nil {
			return "", false
		}
		return cookiesToHeader(list), true
	}

	// 2) 对象：cookies 数组 / cookie 字符串 / SUB+SUBP
	var payload struct {
		Cookies []storedCookie `json:"cookies"`
		Cookie  string         `json:"cookie"`
		Sub     string         `json:"SUB"`
		SubP    string         `json:"SUBP"`
	}
	if err := json.Unmarshal(trimmed, &payload); err != nil {
		return "", false
	}
	if len(payload.Cookies) > 0 {
		return cookiesToHeader(payload.Cookies), true
	}
	if s := strings.TrimSpace(payload.Cookie); s != "" {
		return s, true
	}
	if s := strings.TrimSpace(payload.Sub); s != "" {
		header := "SUB=" + s
		if p := strings.TrimSpace(payload.SubP); p != "" {
			header += "; SUBP=" + p
		}
		return header, true
	}
	return "", false
}

// cookiesToHeader 把结构化 Cookie 拼成 Cookie 头（丢弃空值项）。
func cookiesToHeader(list []storedCookie) string {
	clean := filterCookies(list)
	parts := make([]string, 0, len(clean))
	for _, c := range clean {
		parts = append(parts, c.Name+"="+c.Value)
	}
	return strings.Join(parts, "; ")
}

// checkLoginInfo 返回登录态详情（不修改状态，只刷新缓存）。
func checkLoginInfo() (map[string]any, error) {
	client := getClient()
	if client.hasLogin() {
		client.refreshLoginState()
	}
	snapshot := client.snapshot()
	out := map[string]any{}
	out["logged_in"] = snapshot.LoggedIn
	out["verified"] = snapshot.Verified
	out["uid"] = snapshot.UID
	out["screen_name"] = snapshot.ScreenName
	out["avatar"] = snapshot.Avatar
	out["expires_at"] = snapshot.ExpiresAt
	return out, nil
}

// logout 先通知上游注销（失败忽略），再清空本地 Cookie 与文件。
func logout() (map[string]any, error) {
	client := getClient()
	if client.hasLogin() {
		params := url.Values{}
		params.Set("backurl", "/")
		if _, err := client.webGet(epWebLogout, params); err != nil {
			logWarn("上游注销请求失败（忽略）：%v", err)
		}
	}
	client.resetCookies()
	if err := clearCookies(); err != nil {
		logWarn("删除 Cookie 文件失败：%v", err)
	}
	logSuccess("已退出登录")
	out := map[string]any{}
	out["ok"] = true
	return out, nil
}

// fetchConfig 读取登录态摘要（GET /config）。
func fetchConfig() (map[string]any, error) {
	client := getClient()
	if client.hasLogin() {
		client.refreshLoginState()
	}
	snapshot := client.snapshot()
	out := map[string]any{}
	out["logged_in"] = snapshot.LoggedIn
	out["verified"] = snapshot.Verified
	out["uid"] = snapshot.UID
	out["screen_name"] = snapshot.ScreenName
	out["avatar"] = snapshot.Avatar
	return out, nil
}
