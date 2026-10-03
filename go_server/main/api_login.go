package main

import (
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
// 导入后立即用 /api/config 验证，并把结果落盘。
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
	client.refreshLoginState()
	snapshot := client.snapshot()
	if snapshot.LoggedIn {
		if err := client.persistCookies(); err != nil {
			logWarn("保存 Cookie 失败：%v", err)
		} else {
			logSuccess("已保存 Cookie 到 %s", cookieStorePath())
		}
	}
	out := map[string]any{}
	out["ok"] = snapshot.LoggedIn
	out["logged_in"] = snapshot.LoggedIn
	out["uid"] = snapshot.UID
	out["screen_name"] = snapshot.ScreenName
	return out, nil
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
	out["uid"] = snapshot.UID
	out["screen_name"] = snapshot.ScreenName
	out["avatar"] = snapshot.Avatar
	return out, nil
}
