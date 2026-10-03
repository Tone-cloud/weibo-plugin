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

// utf8BOM 是 UTF-8 字节序标记。Windows 记事本/PowerShell 5.1 的
// `Set-Content -Encoding UTF8` 都会写上它，而 encoding/json 遇到 BOM 会直接
// 报 "invalid character 'ï'"。用户手动编辑 cookies.json 太常见了，
// 所以这里统一先剥掉。
var utf8BOM = []byte{0xEF, 0xBB, 0xBF}

// stripBOM 去掉开头的 UTF-8 BOM（没有则原样返回）。
func stripBOM(raw []byte) []byte {
	return bytes.TrimPrefix(raw, utf8BOM)
}

// loadCookies 读取持久化 Cookie；文件不存在时返回空列表而不是错误。
func loadCookies() ([]storedCookie, error) {
	return readCookieFile(cookieStorePath())
}

// readCookieFile 从指定路径读取 Cookie（loadCookies 的带参版本，供监听使用）。
//
// 兼容三种写法：
//  1. {"cookies":[{name,value,domain,path}], "updated_at":0}   ← 规范格式
//  2. [{"name":"SUB","value":"..."}]                          ← 裸数组
//  3. 空文件 / 不存在                                          ← 空列表
//
// 另外容忍开头带 UTF-8 BOM（记事本存过的文件）。
func readCookieFile(path string) ([]storedCookie, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return []storedCookie{}, nil
		}
		return nil, err
	}
	raw = stripBOM(raw)
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
	// 自己写的文件：同步监听指纹，免得被当成外部改动再处理一遍
	noteCookieFileWritten()
	return nil
}

// clearCookies 删除持久化文件（不存在视为成功）。
func clearCookies() error {
	err := os.Remove(cookieStorePath())
	if err != nil && !os.IsNotExist(err) {
		return err
	}
	noteCookieFileWritten()
	return nil
}
