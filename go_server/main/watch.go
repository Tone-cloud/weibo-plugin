package main

import (
	"fmt"
	"net"
	"os"
	"sort"
	"strings"
	"sync"
	"time"
)

// ============================================================================
// cookies.json 外部改动监听 + 本地状态快照
//
// 目的：让「在电脑上准备好 JSON」这件事不需要在词典笔上做任何操作。
//
// 两条路径都靠这里生效：
//   1. 文件路径：电脑端用 adb push / PenManager / 挂载 U 盘把 cookies.json 写进
//      插件目录 → 本文件监听 mtime+size 变化 → 自动应用到运行中的 client；
//   2. HTTP 路径：电脑端 POST /config/import → 服务端落盘（写的就是同一个文件）
//      → 监听会把指纹同步过去，不会重复处理。
//
// 监听只做 stat、不读内容，开销可以忽略。
// ============================================================================

const cookieWatchInterval = 2 * time.Second

var (
	cookieWatchMu   sync.Mutex
	cookieWatchLast string
)

// pluginDir 返回插件安装目录（与 cookies.go 的探测口径一致）。
func pluginDir() string {
	if dir := strings.TrimSpace(os.Getenv("WEIBO_PLUGIN_DIR")); dir != "" {
		return dir
	}
	return defaultPluginDir
}

// fileSignature 返回文件的 (mtime, size) 指纹；文件不存在时 ok=false。
func fileSignature(path string) (string, bool) {
	info, err := os.Stat(path)
	if err != nil {
		return "", false
	}
	return fmt.Sprintf("%d:%d", info.ModTime().UnixNano(), info.Size()), true
}

// noteCookieFileWritten 在服务端自己写完 Cookie 文件后同步指纹，
// 避免监听协程把「自己刚写的」当成外部改动再处理一遍。
func noteCookieFileWritten() {
	sig, ok := fileSignature(cookieStorePath())
	if !ok {
		return
	}
	cookieWatchMu.Lock()
	cookieWatchLast = sig
	cookieWatchMu.Unlock()
}

// cookieFileRev 返回当前 Cookie 文件指纹，供 GET /server/state 上报，
// 便于 C++ 侧判断「电脑端到底改没改过」。
func cookieFileRev() string {
	sig, ok := fileSignature(cookieStorePath())
	if !ok {
		return ""
	}
	return sig
}

// startCookieWatcher 每 interval 检查一次 cookies.json 是否被外部改动，
// 改了就立刻应用到运行中的 client 上（不需要重启 sidecar）。
func startCookieWatcher(interval time.Duration) {
	if interval <= 0 {
		interval = cookieWatchInterval
	}
	path := cookieStorePath()
	if sig, ok := fileSignature(path); ok {
		cookieWatchMu.Lock()
		cookieWatchLast = sig
		cookieWatchMu.Unlock()
	}
	logInfo("Cookie 监听已启动：%s（每 %s 检查一次，改动自动生效）", path, interval)

	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for range ticker.C {
		sig, ok := fileSignature(path)
		if !ok {
			continue // 文件被删（例如退出登录），保持现状
		}
		cookieWatchMu.Lock()
		changed := sig != cookieWatchLast
		if changed {
			cookieWatchLast = sig
		}
		cookieWatchMu.Unlock()
		if changed {
			applyCookiesFromDisk(path, "检测到 cookies.json 被外部修改")
		}
	}
}

// applyCookiesFromDisk 读取磁盘上的 Cookie 并应用到运行中的 client。
func applyCookiesFromDisk(path, reason string) {
	list, err := readCookieFile(path)
	if err != nil {
		logWarn("%s，但读取失败：%v", reason, err)
		return
	}
	client := getClient()
	if client == nil {
		return
	}
	if len(list) == 0 {
		logWarn("%s：文件里没有可用 Cookie，已清空登录态", reason)
		client.resetCookies()
		client.setLoginState(false, "", "", "", 0)
		return
	}
	client.loadCookieStore(list)
	if !client.hasLogin() {
		logWarn("%s：没有解析到 SUB，忽略", reason)
		return
	}
	logSuccess("%s，正在校验登录态…", reason)
	client.refreshLoginState()
	snapshot := client.snapshot()
	if snapshot.LoggedIn {
		logSuccess("自动导入成功：%s（uid=%d）", snapshot.ScreenName, snapshot.UID)
	} else {
		logWarn("自动导入的 Cookie 未通过登录校验（可能已过期，或当前无法访问微博）")
	}
}

// localState 是 GET /server/state 的返回：**只读本地缓存，不请求上游**。
//
// C++ 侧每几秒轮询这个接口来发现「电脑端改完 Cookie 了」，
// 所以这里绝不能触发任何微博请求（否则等于每几秒打一次上游，容易被风控）。
func localState() (map[string]any, error) {
	out := map[string]any{}
	out["cookie_rev"] = cookieFileRev()
	out["cookie_file"] = cookieStorePath()
	out["plugin_dir"] = pluginDir()
	out["version"] = appVersion
	// 词典笔界面要显示「在电脑浏览器打开这个链接」，所以这里把完整 URL 带上
	out["login_url"] = loginURL()
	out["lan_ips"] = toAnySlice(lanIPv4())

	client := getClient()
	if client == nil {
		out["logged_in"] = false
		return out, nil
	}
	snapshot := client.snapshot()
	out["logged_in"] = snapshot.LoggedIn
	out["verified"] = snapshot.Verified
	out["uid"] = snapshot.UID
	out["screen_name"] = snapshot.ScreenName
	out["avatar"] = snapshot.Avatar
	out["expires_at"] = snapshot.ExpiresAt
	return out, nil
}

// ipPriority 给候选局域网 IPv4 打分，越小越可能是「电脑能连上」的地址。
//
// 为什么需要：设备上常常同时存在好几个地址，实测这台机器就有
// 169.254.x（link-local，电脑根本连不上）、172.21.48.1（Hyper-V 虚拟网卡）、
// 以及真正的 192.168.1.x。如果按枚举顺序取第一个，词典笔就会显示一个
// 用户打不开的链接。
func ipPriority(ip net.IP) int {
	switch {
	case ip[0] == 192 && ip[1] == 168:
		return 0 // 家用路由器最常见
	case ip[0] == 10:
		return 1
	case ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31:
		return 2
	case ip.IsLinkLocalUnicast():
		return 90 // 169.254.x：链路本地，电脑连不上
	case ip.IsLoopback():
		return 99
	default:
		return 50
	}
}

// lanIPv4 列出本机非回环 IPv4，**按「电脑能否连上」排序**（最可能可用的在前）。
func lanIPv4() []string {
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return nil
	}
	type cand struct {
		ip    string
		score int
	}
	var cands []cand
	seen := map[string]bool{}
	for _, addr := range addrs {
		ipnet, ok := addr.(*net.IPNet)
		if !ok || ipnet.IP == nil || ipnet.IP.IsLoopback() {
			continue
		}
		ip4 := ipnet.IP.To4()
		if ip4 == nil {
			continue
		}
		text := ip4.String()
		if seen[text] {
			continue
		}
		seen[text] = true
		cands = append(cands, cand{ip: text, score: ipPriority(ip4)})
	}
	sort.SliceStable(cands, func(i, j int) bool { return cands[i].score < cands[j].score })
	out := make([]string, 0, len(cands))
	for _, c := range cands {
		out = append(out, c.ip)
	}
	return out
}
