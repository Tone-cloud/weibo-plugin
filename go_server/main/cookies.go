package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// storedCookie 是持久化到磁盘的一条 Cookie（SPEC 第 5 节）。
type storedCookie struct {
	Name   string `json:"name"`
	Value  string `json:"value"`
	Domain string `json:"domain,omitempty"`
	Path   string `json:"path,omitempty"`
}

// cookieFilePayload 是 cookies.json 的顶层结构：
// { "cookies": [...], "updated_at": 0 }
type cookieFilePayload struct {
	Cookies   []storedCookie `json:"cookies"`
	UpdatedAt int64          `json:"updated_at"`
}

// cookiePathOnce 保证可写性探测只做一次（探测本身会建目录、写文件）。
var (
	cookiePathOnce sync.Once
	cookiePathVal  string
)

// cookieStorePath 返回 Cookie 落盘路径。
//
// 优先级：
//  1. 环境变量 WEIBO_COOKIE_FILE；
//  2. <WEIBO_PLUGIN_DIR 或 /userdisk/PenMods/plugins/weibo_plugin>/cookies.json（需目录可写）；
//  3. 当前工作目录下的 ./cookies.json（桌面调试 / CI 回落）。
func cookieStorePath() string {
	cookiePathOnce.Do(func() {
		cookiePathVal = resolveCookieStorePath()
	})
	return cookiePathVal
}

// resolveCookieStorePath 执行真实路径探测。
func resolveCookieStorePath() string {
	if custom := strings.TrimSpace(os.Getenv("WEIBO_COOKIE_FILE")); custom != "" {
		return custom
	}
	dir := strings.TrimSpace(os.Getenv("WEIBO_PLUGIN_DIR"))
	if dir == "" {
		dir = defaultPluginDir
	}
	if dirWritable(dir) {
		return filepath.Join(dir, "cookies.json")
	}
	return "cookies.json"
}

// dirWritable 通过 MkdirAll + 真实写文件来判断目录是否可写。
func dirWritable(dir string) bool {
	if strings.TrimSpace(dir) == "" {
		return false
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return false
	}
	probe := filepath.Join(dir, ".weibo_write_probe")
	file, err := os.OpenFile(probe, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
	if err != nil {
		return false
	}
	_, writeErr := file.WriteString("ok")
	closeErr := file.Close()
	_ = os.Remove(probe)
	return writeErr == nil && closeErr == nil
}

// filterCookies 丢掉 name 为空的脏数据。
func filterCookies(list []storedCookie) []storedCookie {
	out := make([]storedCookie, 0, len(list))
	for _, item := range list {
		name := strings.TrimSpace(item.Name)
		if name == "" || item.Value == "" {
			continue
		}
		out = append(out, storedCookie{
			Name:   name,
			Value:  item.Value,
			Domain: strings.TrimSpace(item.Domain),
			Path:   strings.TrimSpace(item.Path),
		})
	}
	return out
}

// loadCookies 读取持久化 Cookie；文件不存在时返回空列表而不是错误。
func loadCookies() ([]storedCookie, error) {
	path := cookieStorePath()
	raw, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return []storedCookie{}, nil
		}
		return nil, err
	}
	if len(bytes.TrimSpace(raw)) == 0 {
		return []storedCookie{}, nil
	}
	var payload cookieFilePayload
	if err := json.Unmarshal(raw, &payload); err != nil {
		// 兼容裸数组写法 [{"name":...}]
		var list []storedCookie
		if err2 := json.Unmarshal(raw, &list); err2 == nil {
			return filterCookies(list), nil
		}
		return nil, err
	}
	return filterCookies(payload.Cookies), nil
}

// saveCookies 原子化落盘（先写临时文件再改名），避免掉电写坏。
func saveCookies(list []storedCookie) error {
	payload := cookieFilePayload{
		Cookies:   filterCookies(list),
		UpdatedAt: time.Now().Unix(),
	}
	raw, err := json.MarshalIndent(payload, "", "  ")
	if err != nil {
		return err
	}
	raw = append(raw, '\n')
	path := cookieStorePath()
	dir := filepath.Dir(path)
	if dir != "" && dir != "." {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, raw, 0o600); err != nil {
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return nil
}

// clearCookies 删除持久化文件（不存在视为成功）。
func clearCookies() error {
	err := os.Remove(cookieStorePath())
	if err != nil && !os.IsNotExist(err) {
		return err
	}
	return nil
}
