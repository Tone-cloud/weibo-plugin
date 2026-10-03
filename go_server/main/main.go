package main

import (
	"context"
	"errors"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"
)

// main 启动 weibo-server（weibopocket/server）。
//
// 启动顺序刻意与 BiliPocket 保持一致：
//  1. 先 net.Listen —— C++ 侧的 probeServer 只有在端口真正可连时才认为 sidecar 就绪，
//     所以必须先把监听建立起来，再去加载 Cookie / 校验登录态（可能耗时数秒甚至超时）；
//  2. 再 go globalClient.Init() 异步初始化；
//  3. 最后 srv.Serve(ln)：出错也不返回，避免 bring-up 丢完成回调后界面一直转圈；
//  4. 收到 SIGINT / SIGTERM 时用 10s 上下文优雅退出，并刷一次日志。
func main() {
	port := strings.TrimSpace(os.Getenv("PORT"))
	if port == "" {
		port = defaultPort
	}
	host := "127.0.0.1"
	if DEBUG {
		// 调试时监听所有网卡，方便桌面浏览器直接访问。
		host = "0.0.0.0"
	}
	addr := host + ":" + port

	printBanner(addr)

	client := NewWeiboClient("", "")
	setGlobalClient(client)

	mux := http.NewServeMux()
	setupRoutes(mux)
	handler := recoverMiddleware(loggingMiddleware(mux))

	server := &http.Server{
		Handler:           handler,
		ReadHeaderTimeout: 10 * time.Second,
	}

	listener, err := net.Listen("tcp", addr)
	if err != nil {
		logError("监听 %s 失败：%v", addr, err)
		asyncLogFlush(asyncLogShutdownTimeout)
		os.Exit(1)
	}
	logSuccess("weibo-server 已就绪：http://%s（版本 %s）", addr, appVersion)

	// 端口已就绪，登录态初始化放到后台，避免拖慢 C++ 的探测。
	go client.Init()

	serveErr := make(chan error, 1)
	go func() {
		if err := server.Serve(listener); err != nil && !errors.Is(err, http.ErrServerClosed) {
			serveErr <- err
		}
	}()

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)

	select {
	case sig := <-stop:
		logWarn("收到信号 %s，开始优雅退出…", sig.String())
	case err := <-serveErr:
		logError("HTTP 服务异常退出：%v", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := server.Shutdown(ctx); err != nil {
		logError("优雅退出超时：%v", err)
	}
	asyncLogFlush(asyncLogShutdownTimeout)
	logPlain("weibo-server 已退出")
}

// printBanner 打印启动横幅与路由清单。
func printBanner(addr string) {
	logPlain("==========================================================")
	logPlain(" WeiboPocket sidecar  v%s", appVersion)
	logPlain(" 监听地址 : http://%s", addr)
	logPlain(" 调试模式 : %v", DEBUG)
	logPlain(" Cookie   : %s", cookieStorePath())
	logPlain(" 历史文件 : %s", searchHistoryPath())
	logPlain("----------------------------------------------------------")
	for _, line := range startupEndpoints {
		logPlain(" %s", line)
	}
	logPlain("==========================================================")
}
