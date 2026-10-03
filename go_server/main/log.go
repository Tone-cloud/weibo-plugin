package main

import (
	"fmt"
	"os"
	"strings"
	"sync"
	"time"
)

// asyncLogShutdownTimeout 是进程退出时等待日志落盘的兜底超时。
// main.go 在优雅退出阶段会把它传给 asyncLogFlush。
const asyncLogShutdownTimeout = 2 * time.Second

// DEBUG 为 true 时输出详细调试日志（环境变量 DEBUG=true/1/yes/on 打开）。
// 非调试模式下只输出关键信息，避免在词典笔上频繁写日志。
var DEBUG = parseDebugEnv()

// logMu 保证多 goroutine 写日志时不会互相打断（每个请求一个 goroutine）。
var logMu sync.Mutex

// parseDebugEnv 解析 DEBUG 环境变量。
func parseDebugEnv() bool {
	v := strings.ToLower(strings.TrimSpace(os.Getenv("DEBUG")))
	switch v {
	case "true", "1", "yes", "on", "y":
		return true
	default:
		return false
	}
}

// logWrite 是唯一的落盘入口，带时间戳与级别标签，并保证互斥。
func logWrite(level, message string) {
	line := fmt.Sprintf("[%s] [%s] %s\n", time.Now().Format("2006-01-02 15:04:05.000"), level, message)
	logMu.Lock()
	_, _ = os.Stdout.WriteString(line)
	logMu.Unlock()
}

// logInfo 输出调试信息，仅在 DEBUG=true 时可见。
func logInfo(format string, args ...any) {
	if !DEBUG {
		return
	}
	logWrite("INFO", fmt.Sprintf(format, args...))
}

// logWarn 输出警告，任何模式下都可见。
func logWarn(format string, args ...any) {
	logWrite("WARN", fmt.Sprintf(format, args...))
}

// logError 输出错误，任何模式下都可见。
func logError(format string, args ...any) {
	logWrite("ERROR", fmt.Sprintf(format, args...))
}

// logSuccess 输出成功提示，任何模式下都可见。
func logSuccess(format string, args ...any) {
	logWrite("OK", fmt.Sprintf(format, args...))
}

// logPlain 输出不带级别的原始行（启动横幅、接口清单等）。
func logPlain(format string, args ...any) {
	logWrite("", fmt.Sprintf(format, args...))
}

// logRequest 记录一条上游请求（仅 DEBUG）。
func logRequest(method, path, params string) {
	if !DEBUG {
		return
	}
	if params == "" {
		logWrite("REQ", fmt.Sprintf("%s %s", method, path))
		return
	}
	logWrite("REQ", fmt.Sprintf("%s %s?%s", method, path, params))
}

// logResponse 记录一条上游响应：DEBUG 下全部记录，非 DEBUG 只记录失败。
func logResponse(path string, code int, duration time.Duration) {
	if !DEBUG && code < 400 {
		return
	}
	logWrite("RESP", fmt.Sprintf("%s -> %d (%s)", path, code, duration.Round(time.Millisecond)))
}

// asyncLogFlush 刷写日志缓冲。当前实现是同步写 stdout，
// 这里额外做一次 Sync 并带超时兜底，避免退出时丢日志。
func asyncLogFlush(timeout time.Duration) {
	if timeout <= 0 {
		timeout = asyncLogShutdownTimeout
	}
	done := make(chan struct{})
	go func() {
		logMu.Lock()
		_ = os.Stdout.Sync()
		_ = os.Stderr.Sync()
		logMu.Unlock()
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(timeout):
	}
}
