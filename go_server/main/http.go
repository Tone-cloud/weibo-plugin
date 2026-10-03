package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/url"
	"runtime/debug"
	"strings"
	"sync"
	"time"
)

// ============================================================================
// HTTP 层：统一信封、全局客户端、中间件、参数解析。
//
// 信封契约（SPEC 第 3 节）：
//   { "code": 0, "message": "", "data": { ... } }
//   - code == 0 成功，data 永远是对象（数组统一放在 items 字段）；
//   - code == -100 未登录，-101 登录已过期（C++ 侧据此清理本地登录态）。
// ============================================================================

// appError 是带业务 code 的错误。
type appError struct {
	Code    int
	Status  int
	Message string
}

// Error 实现 error 接口。
func (e *appError) Error() string {
	return e.Message
}

// errBadRequest 参数校验失败 → HTTP 400 + code -1。
func errBadRequest(message string) error {
	return &appError{Code: -1, Status: http.StatusBadRequest, Message: message}
}

// errNotLoggedIn 未登录 → HTTP 200 + code -100（C++ 只认 code）。
func errNotLoggedIn() error {
	return &appError{Code: -100, Status: http.StatusOK, Message: "未登录"}
}

// errLoginExpired 登录已过期 → HTTP 200 + code -101。
func errLoginExpired() error {
	return &appError{Code: -101, Status: http.StatusOK, Message: "登录已过期"}
}

// writeJSONBytes 原样写出一段 JSON。
func writeJSONBytes(w http.ResponseWriter, status int, payload []byte) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	if _, err := w.Write(payload); err != nil {
		logWarn("写出响应失败：%v", err)
	}
}

// writeJSON 序列化并写出统一信封（调用方自行组好 code/message/data）。
func writeJSON(w http.ResponseWriter, status int, value any) {
	raw, err := json.Marshal(value)
	if err != nil {
		logError("序列化响应失败：%v", err)
		writeJSONBytes(w, http.StatusInternalServerError, []byte(`{"code":-1,"message":"响应序列化失败","data":{}}`))
		return
	}
	writeJSONBytes(w, status, raw)
}

// writeOK 输出成功信封（data 一定是对象）。
func writeOK(w http.ResponseWriter, data map[string]any) {
	if data == nil {
		data = map[string]any{}
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"code":    0,
		"message": "",
		"data":    data,
	})
}

// writeError 输出失败信封。参数错误走 HTTP 400，其余保持 200（C++ 只看 code）。
func writeError(w http.ResponseWriter, err error) {
	code := -1
	status := http.StatusOK
	message := "未知错误"
	if err != nil {
		message = err.Error()
	}
	var appErr *appError
	if errors.As(err, &appErr) {
		code = appErr.Code
		message = appErr.Message
		if appErr.Status > 0 {
			status = appErr.Status
		}
	}
	if strings.TrimSpace(message) == "" {
		message = "未知错误"
	}
	writeJSON(w, status, map[string]any{
		"code":    code,
		"message": message,
		"data":    map[string]any{},
	})
}

// ======================= 全局客户端 =======================

var (
	globalClient   *WeiboClient
	globalClientMu sync.Mutex
)

// setGlobalClient 注入全局客户端（main.go 启动时调用一次）。
func setGlobalClient(client *WeiboClient) {
	globalClientMu.Lock()
	globalClient = client
	globalClientMu.Unlock()
}

// getClient 取全局客户端；未初始化时惰性创建一个。
func getClient() *WeiboClient {
	globalClientMu.Lock()
	defer globalClientMu.Unlock()
	if globalClient == nil {
		globalClient = NewWeiboClient("", "")
	}
	return globalClient
}

// ======================= 中间件 =======================

// loggingResponseWriter 记录状态码 / 字节数，并透传 Flush 与 Unwrap。
type loggingResponseWriter struct {
	http.ResponseWriter

	status  int
	written int
	wrote   bool
}

// WriteHeader 记录首个状态码。
func (lw *loggingResponseWriter) WriteHeader(status int) {
	if !lw.wrote {
		lw.status = status
		lw.wrote = true
	}
	lw.ResponseWriter.WriteHeader(status)
}

// Write 记录写入字节数。
func (lw *loggingResponseWriter) Write(p []byte) (int, error) {
	if !lw.wrote {
		lw.status = http.StatusOK
		lw.wrote = true
	}
	n, err := lw.ResponseWriter.Write(p)
	lw.written += n
	return n, err
}

// Flush 透传底层 Flusher（图片 / 长响应需要）。
func (lw *loggingResponseWriter) Flush() {
	if flusher, ok := lw.ResponseWriter.(http.Flusher); ok {
		flusher.Flush()
	}
}

// Unwrap 让 http.ResponseController 能找到底层 ResponseWriter。
func (lw *loggingResponseWriter) Unwrap() http.ResponseWriter {
	return lw.ResponseWriter
}

// loggingMiddleware 记录每个本地请求的耗时与状态。
func loggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		lw := &loggingResponseWriter{ResponseWriter: w, status: http.StatusOK}
		next.ServeHTTP(lw, r)
		logResponse(r.URL.Path, lw.status, time.Since(start))
	})
}

// recoverMiddleware 兜底 panic，保证 sidecar 不会因为单个请求崩溃。
func recoverMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer func() {
			if rec := recover(); rec != nil {
				logError("处理 %s %s 时发生 panic：%v", r.Method, r.URL.Path, rec)
				if DEBUG {
					logPlain("%s", string(debug.Stack()))
				}
				writeError(w, &appError{Code: -1, Status: http.StatusOK, Message: "服务内部错误，请重试"})
			}
		}()
		next.ServeHTTP(w, r)
	})
}

// ======================= 处理器外壳 =======================

// handleAPI 统一的处理器外壳：校验方法 → 执行业务 → 输出信封。
// 每个 handleXxx 都通过它保证 code=0 / 405 / 错误映射的一致性。
func handleAPI(w http.ResponseWriter, r *http.Request, method string, fn func(*http.Request) (map[string]any, error)) {
	if r.Method != method {
		w.Header().Set("Allow", method)
		writeJSON(w, http.StatusMethodNotAllowed, map[string]any{
			"code":    -1,
			"message": "请求方法不允许，应使用 " + method,
			"data":    map[string]any{},
		})
		return
	}
	data, err := fn(r)
	if err != nil {
		writeError(w, err)
		return
	}
	writeOK(w, data)
}

// ======================= 参数解析 =======================

// getIntQuery 读取 query 里的整数，缺省或非法时返回 def。
func getIntQuery(r *http.Request, key string, def int) int {
	raw := strings.TrimSpace(r.URL.Query().Get(key))
	if raw == "" {
		return def
	}
	return int(parseIntLoose(raw))
}

// requireIntQuery 要求 query 里必须有合法整数。
func requireIntQuery(r *http.Request, key string) (int, error) {
	raw := strings.TrimSpace(r.URL.Query().Get(key))
	if raw == "" {
		return 0, errBadRequest("缺少参数 " + key)
	}
	if _, err := parseIntStrict(raw); err != nil {
		return 0, errBadRequest("参数 " + key + " 必须是数字")
	}
	return int(parseIntLoose(raw)), nil
}

// getInt64Query 读取 query 里的 64 位整数。
func getInt64Query(r *http.Request, key string, def int64) int64 {
	raw := strings.TrimSpace(r.URL.Query().Get(key))
	if raw == "" {
		return def
	}
	return parseIntLoose(raw)
}

// parseIntStrict 严格解析十进制整数（不接受小数）。
func parseIntStrict(raw string) (int64, error) {
	value := strings.TrimSpace(raw)
	if value == "" {
		return 0, errors.New("参数为空")
	}
	return parseDecimal(value)
}

// parseDecimal 只接受可选的负号 + 纯数字。
func parseDecimal(value string) (int64, error) {
	body := value
	if strings.HasPrefix(body, "-") || strings.HasPrefix(body, "+") {
		body = body[1:]
	}
	if body == "" {
		return 0, errors.New("参数非法")
	}
	for i := 0; i < len(body); i++ {
		if body[i] < '0' || body[i] > '9' {
			return 0, errors.New("参数非法")
		}
	}
	return parseIntLoose(value), nil
}

// getPageParams 读取评论接口用的一组分页参数。
func getPageParams(r *http.Request) (int, int64, int) {
	page := getIntQuery(r, "page", 1)
	if page < 1 {
		page = 1
	}
	maxID := getInt64Query(r, "max_id", 0)
	maxIDType := getIntQuery(r, "max_id_type", 0)
	return page, maxID, maxIDType
}

// getStr 从请求体 map 里取字符串。
func getStr(m map[string]any, key string) string {
	if m == nil {
		return ""
	}
	value, ok := m[key]
	if !ok || value == nil {
		return ""
	}
	return strings.TrimSpace(asString(value))
}

// requireStr 从请求体 map 里取必填字符串。
func requireStr(m map[string]any, key string) (string, error) {
	value := getStr(m, key)
	if value == "" {
		return "", errBadRequest("缺少参数 " + key)
	}
	return value, nil
}

// requireQueryStr 从 query 里取必填字符串。
func requireQueryStr(r *http.Request, key string) (string, error) {
	value := strings.TrimSpace(r.URL.Query().Get(key))
	if value == "" {
		return "", errBadRequest("缺少参数 " + key)
	}
	return value, nil
}

// intParam 从请求体 map 里取整数。
func intParam(m map[string]any, key string, def int) int {
	if m == nil {
		return def
	}
	value, ok := m[key]
	if !ok || value == nil {
		return def
	}
	raw := strings.TrimSpace(asString(value))
	if raw == "" {
		return def
	}
	return int(parseIntLoose(raw))
}

// boolParam 从请求体 map 里取布尔值。
func boolParam(m map[string]any, key string, def bool) bool {
	if m == nil {
		return def
	}
	value, ok := m[key]
	if !ok || value == nil {
		return def
	}
	if strings.TrimSpace(asString(value)) == "" {
		return def
	}
	return asBool(value)
}

// strSliceParam 从请求体 map 里取字符串数组（兼容逗号分隔的字符串）。
func strSliceParam(m map[string]any, key string) []string {
	if m == nil {
		return nil
	}
	value, ok := m[key]
	if !ok || value == nil {
		return nil
	}
	switch typed := value.(type) {
	case nil:
		return nil
	case []string:
		return typed
	case []any:
		out := make([]string, 0, len(typed))
		for _, item := range typed {
			if s := strings.TrimSpace(asString(item)); s != "" {
				out = append(out, s)
			}
		}
		return out
	case string:
		raw := strings.TrimSpace(typed)
		if raw == "" {
			return nil
		}
		out := make([]string, 0, 4)
		for _, part := range strings.Split(raw, ",") {
			if s := strings.TrimSpace(part); s != "" {
				out = append(out, s)
			}
		}
		return out
	default:
		return nil
	}
}

// bodyMap 把 POST 请求体解析成 map。
// 同时兼容 JSON 体、form-urlencoded 体，以及把参数放在 query 上的调用方；
// 请求体里的值优先于 query。
func bodyMap(r *http.Request) map[string]any {
	out := map[string]any{}
	for key, values := range r.URL.Query() {
		if len(values) > 0 {
			out[key] = values[0]
		}
	}
	if r.Body == nil {
		return out
	}
	raw, err := io.ReadAll(io.LimitReader(r.Body, 4<<20))
	_ = r.Body.Close()
	if err != nil {
		logWarn("读取请求体失败：%v", err)
		return out
	}
	trimmed := bytes.TrimSpace(raw)
	if len(trimmed) == 0 {
		return out
	}
	if trimmed[0] == '{' {
		for key, value := range jsonObjectToMap(trimmed) {
			out[key] = value
		}
		return out
	}
	values, err := url.ParseQuery(string(trimmed))
	if err != nil {
		return out
	}
	for key, list := range values {
		if len(list) > 0 {
			out[key] = list[0]
		}
	}
	return out
}
