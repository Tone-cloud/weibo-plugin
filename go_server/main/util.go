package main

import (
	"bytes"
	"encoding/json"
	"net/url"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

// ======================= 通用小工具 =======================

// firstNonEmpty 返回第一个去空白后非空的字符串。
func firstNonEmpty(values ...string) string {
	for _, v := range values {
		if s := strings.TrimSpace(v); s != "" {
			return s
		}
	}
	return ""
}

// clampInt 把 v 限制在 [lo, hi] 区间内。
func clampInt(v, lo, hi int) int {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

// toAnySlice 把 []string 转成 []any，便于直接塞进 map[string]any 输出。
func toAnySlice(list []string) []any {
	out := make([]any, 0, len(list))
	for _, s := range list {
		out = append(out, s)
	}
	return out
}

// sortedKeys 返回 map 的键（升序），保证遍历结果稳定可复现。
func sortedKeys(m map[string]any) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

// dedupeStrings 去重并保持顺序。
func dedupeStrings(list []string) []string {
	seen := map[string]bool{}
	out := make([]string, 0, len(list))
	for _, s := range list {
		v := strings.TrimSpace(s)
		if v == "" || seen[v] {
			continue
		}
		seen[v] = true
		out = append(out, v)
	}
	return out
}

// extractContainerIDFromScheme 从 sinaweibo://pageinfo?containerid=100808xxx 这类 scheme 里取 containerid。
func extractContainerIDFromScheme(scheme string) string {
	idx := strings.Index(scheme, "containerid=")
	if idx < 0 {
		return ""
	}
	rest := scheme[idx+len("containerid="):]
	if end := strings.IndexAny(rest, "&#"); end >= 0 {
		rest = rest[:end]
	}
	if decoded, err := url.QueryUnescape(rest); err == nil {
		return strings.TrimSpace(decoded)
	}
	return strings.TrimSpace(rest)
}

// ======================= HTML 处理 =======================

// htmlEntityReplacer 处理微博文本里最常见的命名实体。
var htmlEntityReplacer = strings.NewReplacer(
	"&nbsp;", " ",
	"&amp;", "&",
	"&lt;", "<",
	"&gt;", ">",
	"&quot;", "\"",
	"&#39;", "'",
	"&apos;", "'",
	"&hellip;", "…",
	"&mdash;", "—",
	"&ndash;", "–",
	"&ldquo;", "“",
	"&rdquo;", "”",
	"&lsquo;", "‘",
	"&rsquo;", "’",
	"&middot;", "·",
	"&times;", "×",
	"&laquo;", "«",
	"&raquo;", "»",
)

// reNumericEntity 匹配 &#123; / &#x1F600; 形式的实体。
var reNumericEntity = regexp.MustCompile(`&#(x?[0-9a-fA-F]+);`)

// htmlUnescapeBasic 只处理常见实体与数字实体，不引入 html 包以外的语义。
func htmlUnescapeBasic(s string) string {
	if s == "" || !strings.Contains(s, "&") {
		return s
	}
	s = htmlEntityReplacer.Replace(s)
	if strings.Contains(s, "&#") {
		s = reNumericEntity.ReplaceAllStringFunc(s, func(match string) string {
			body := match[2 : len(match)-1]
			base := 10
			if len(body) > 0 && (body[0] == 'x' || body[0] == 'X') {
				base = 16
				body = body[1:]
			}
			n, err := strconv.ParseInt(body, base, 32)
			if err != nil || n <= 0 {
				return match
			}
			return string(rune(n))
		})
	}
	return s
}

// stripHTMLTags 去掉全部标签，并把 <br> / </p> / </div> 变成换行。
func stripHTMLTags(s string) string {
	if s == "" {
		return ""
	}
	var b strings.Builder
	b.Grow(len(s))
	i := 0
	for i < len(s) {
		if s[i] != '<' {
			b.WriteByte(s[i])
			i++
			continue
		}
		end := strings.IndexByte(s[i:], '>')
		if end < 0 {
			break
		}
		tag := strings.ToLower(s[i : i+end+1])
		if strings.HasPrefix(tag, "<br") || strings.HasPrefix(tag, "</p") || strings.HasPrefix(tag, "</div") {
			b.WriteByte('\n')
		}
		i += end + 1
	}
	return b.String()
}

// trimHTML 是 text 字段的归一化入口：剥标签 → 解实体 → 压空白。
func trimHTML(s string) string {
	if s == "" {
		return ""
	}
	plain := htmlUnescapeBasic(stripHTMLTags(s))
	plain = strings.ReplaceAll(plain, "\r\n", "\n")
	plain = strings.ReplaceAll(plain, "\r", "\n")
	lines := strings.Split(plain, "\n")
	out := make([]string, 0, len(lines))
	blank := 0
	for _, line := range lines {
		trimmed := strings.TrimRight(line, " \t\u3000")
		if strings.TrimSpace(trimmed) == "" {
			blank++
			if blank > 1 {
				continue
			}
			out = append(out, "")
			continue
		}
		blank = 0
		out = append(out, trimmed)
	}
	return strings.TrimSpace(strings.Join(out, "\n"))
}

// extractAttr 从标签属性串里取一个属性值（支持双引号 / 单引号 / 无引号）。
func extractAttr(attrs, key string) string {
	lower := strings.ToLower(attrs)
	idx := 0
	for idx < len(lower) {
		p := strings.Index(lower[idx:], key)
		if p < 0 {
			return ""
		}
		p += idx
		if p > 0 {
			prev := lower[p-1]
			if prev != ' ' && prev != '\t' && prev != '\r' && prev != '\n' && prev != '"' && prev != '\'' {
				idx = p + len(key)
				continue
			}
		}
		rest := strings.TrimLeft(attrs[p+len(key):], " \t\r\n")
		if !strings.HasPrefix(rest, "=") {
			idx = p + len(key)
			continue
		}
		rest = strings.TrimLeft(rest[1:], " \t\r\n")
		if rest == "" {
			return ""
		}
		if rest[0] == '"' || rest[0] == '\'' {
			quote := rest[0]
			if end := strings.IndexByte(rest[1:], quote); end >= 0 {
				return rest[1 : 1+end]
			}
			return rest[1:]
		}
		if end := strings.IndexAny(rest, " \t\r\n"); end >= 0 {
			return rest[:end]
		}
		return rest
	}
	return ""
}

// escapeAttr 只转义会破坏 HTML 属性的双引号。
func escapeAttr(s string) string {
	return strings.ReplaceAll(s, "\"", "&quot;")
}

// sanitizeWeiboHTML 生成 text_html：只保留 <a href> / <img alt src> / <br>，其余标签丢弃但保留内部文本。
func sanitizeWeiboHTML(s string) string {
	if s == "" {
		return ""
	}
	var b strings.Builder
	b.Grow(len(s))
	i := 0
	for i < len(s) {
		if s[i] != '<' {
			b.WriteByte(s[i])
			i++
			continue
		}
		end := strings.IndexByte(s[i:], '>')
		if end < 0 {
			break
		}
		inner := strings.TrimSpace(s[i+1 : i+end])
		closing := strings.HasPrefix(inner, "/")
		body := strings.TrimSpace(strings.TrimPrefix(inner, "/"))
		name := body
		if sp := strings.IndexAny(body, " \t\r\n"); sp >= 0 {
			name = body[:sp]
		}
		name = strings.ToLower(name)
		attrs := ""
		if len(body) > len(name) {
			attrs = strings.TrimSpace(body[len(name):])
		}
		switch name {
		case "br":
			b.WriteString("<br/>")
		case "a":
			if closing {
				b.WriteString("</a>")
				break
			}
			href := extractAttr(attrs, "href")
			if href != "" {
				b.WriteString("<a href=\"" + escapeAttr(href) + "\">")
			}
		case "img":
			src := extractAttr(attrs, "src")
			alt := extractAttr(attrs, "alt")
			if src != "" {
				b.WriteString("<img src=\"" + escapeAttr(src) + "\" alt=\"" + escapeAttr(alt) + "\"/>")
			}
		default:
			// 其余标签一律丢弃，内部文本保留。
		}
		i += end + 1
	}
	return b.String()
}

// ======================= JSON 提取 =======================

// extractJSONObject 把上游响应里 JSON 对象前后的非 JSON 内容裁掉
// （picupload 之类会返回 window.parent.cb({...}) 形式）。
// 若上游直接返回数组，则包一层 {"data": [...]} 以便统一处理。
func extractJSONObject(raw []byte) ([]byte, bool) {
	if len(raw) == 0 {
		return nil, false
	}
	start := bytes.IndexAny(raw, "{[")
	if start < 0 {
		return nil, false
	}
	dec := json.NewDecoder(bytes.NewReader(raw[start:]))
	var value json.RawMessage
	if err := dec.Decode(&value); err != nil {
		return nil, false
	}
	trimmed := bytes.TrimSpace(value)
	if len(trimmed) == 0 {
		return nil, false
	}
	switch trimmed[0] {
	case '{':
		return trimmed, true
	case '[':
		wrapped := make([]byte, 0, len(trimmed)+11)
		wrapped = append(wrapped, `{"data":`...)
		wrapped = append(wrapped, trimmed...)
		wrapped = append(wrapped, '}')
		return wrapped, true
	default:
		return nil, false
	}
}

// ======================= 数字与时间 =======================

// parseIntLoose 宽松解析整数（允许小数与前后空白）。
func parseIntLoose(s string) int64 {
	v := strings.TrimSpace(s)
	if v == "" {
		return 0
	}
	if n, err := strconv.ParseInt(v, 10, 64); err == nil {
		return n
	}
	if f, err := strconv.ParseFloat(v, 64); err == nil {
		return int64(f)
	}
	return 0
}

// parseLooseCount 把上游各种形态的计数（数字 / 字符串 / "1.2万" / "3.4亿"）解析成 int64。
func parseLooseCount(v any) int64 {
	switch t := v.(type) {
	case nil:
		return 0
	case int:
		return int64(t)
	case int64:
		return t
	case float64:
		return int64(t)
	case float32:
		return int64(t)
	case bool:
		if t {
			return 1
		}
		return 0
	case json.Number:
		if n, err := t.Int64(); err == nil {
			return n
		}
		if f, err := t.Float64(); err == nil {
			return int64(f)
		}
		return 0
	case string:
		s := strings.TrimSpace(t)
		if s == "" {
			return 0
		}
		s = strings.ReplaceAll(s, ",", "")
		s = strings.ReplaceAll(s, " ", "")
		multiplier := 1.0
		switch {
		case strings.HasSuffix(s, "万"):
			multiplier = 10000
			s = strings.TrimSuffix(s, "万")
		case strings.HasSuffix(s, "亿"):
			multiplier = 100000000
			s = strings.TrimSuffix(s, "亿")
		case strings.HasSuffix(s, "w"), strings.HasSuffix(s, "W"):
			multiplier = 10000
			s = s[:len(s)-1]
		}
		if f, err := strconv.ParseFloat(strings.TrimSpace(s), 64); err == nil {
			return int64(f * multiplier)
		}
		return 0
	default:
		return 0
	}
}

// formatCountText 把计数格式化成 "1.2万" / "3.4亿"。
func formatCountText(n int64) string {
	if n < 0 {
		n = 0
	}
	switch {
	case n < 10000:
		return strconv.FormatInt(n, 10)
	case n < 100000000:
		return trimTrailingZero(float64(n)/10000) + "万"
	default:
		return trimTrailingZero(float64(n)/100000000) + "亿"
	}
}

// trimTrailingZero 保留一位小数并去掉无意义的 ".0"。
func trimTrailingZero(f float64) string {
	s := strconv.FormatFloat(f, 'f', 1, 64)
	return strings.TrimSuffix(s, ".0")
}

// leadingInt 读取字符串开头连续的十进制数字。
func leadingInt(s string) int64 {
	i := 0
	for i < len(s) && s[i] >= '0' && s[i] <= '9' {
		i++
	}
	if i == 0 {
		return 0
	}
	n, err := strconv.ParseInt(s[:i], 10, 64)
	if err != nil {
		return 0
	}
	return n
}

// weiboTimeLayouts 覆盖 m.weibo.cn / weibo.com 出现过的所有时间写法。
var weiboTimeLayouts = []string{
	"2006-01-02 15:04:05",
	"2006-01-02 15:04",
	"2006-01-02",
	"2006/01/02 15:04:05",
	"2006/01/02 15:04",
	"2006/01/02",
	"2006年01月02日 15:04",
	"2006年01月02日",
	"01-02 15:04",
	"01-02",
}

// parseWeiboTimeToUnix 把微博的各种时间字符串解析成 Unix 秒；失败返回 0。
//
// 支持的写法：
//
//	"刚刚" / "3分钟前" / "2小时前" / "5天前"
//	"今天 12:30" / "昨天 12:30" / "前天 12:30" / "12:30"
//	"12-25" / "12-25 12:30"
//	"2024-01-01 12:00:00" / "2024-01-01"
//	"Mon Jan 02 15:04:05 +0800 2006"
func parseWeiboTimeToUnix(raw string) int64 {
	s := strings.TrimSpace(raw)
	if s == "" {
		return 0
	}
	now := time.Now()
	switch s {
	case "刚刚", "刚才", "刚刚发布", "just now":
		return now.Unix()
	}
	if strings.HasSuffix(s, "秒前") {
		return now.Add(-time.Duration(leadingInt(s)) * time.Second).Unix()
	}
	if strings.HasSuffix(s, "分钟前") {
		return now.Add(-time.Duration(leadingInt(s)) * time.Minute).Unix()
	}
	if strings.HasSuffix(s, "小时前") {
		return now.Add(-time.Duration(leadingInt(s)) * time.Hour).Unix()
	}
	if strings.HasSuffix(s, "天前") {
		return now.Add(-time.Duration(leadingInt(s)) * 24 * time.Hour).Unix()
	}
	for _, prefix := range []string{"今天", "昨天", "前天"} {
		if !strings.HasPrefix(s, prefix) {
			continue
		}
		rest := strings.TrimSpace(strings.TrimPrefix(s, prefix))
		rest = strings.TrimSpace(strings.TrimPrefix(rest, "上午"))
		rest = strings.TrimSpace(strings.TrimPrefix(rest, "下午"))
		clock, err := time.ParseInLocation("15:04", rest, time.Local)
		if err != nil {
			clock, err = time.ParseInLocation("15:04:05", rest, time.Local)
		}
		if err != nil {
			continue
		}
		offset := 0
		switch prefix {
		case "昨天":
			offset = -1
		case "前天":
			offset = -2
		}
		day := now.AddDate(0, 0, offset)
		y, mo, d := day.Date()
		return time.Date(y, mo, d, clock.Hour(), clock.Minute(), clock.Second(), 0, time.Local).Unix()
	}
	if t, err := time.ParseInLocation("15:04", s, time.Local); err == nil {
		y, mo, d := now.Date()
		candidate := time.Date(y, mo, d, t.Hour(), t.Minute(), t.Second(), 0, time.Local)
		if candidate.After(now) {
			candidate = candidate.AddDate(0, 0, -1)
		}
		return candidate.Unix()
	}
	for _, layout := range weiboTimeLayouts {
		t, err := time.ParseInLocation(layout, s, time.Local)
		if err != nil {
			continue
		}
		if t.Year() == 0 {
			// "12-25" / "12-25 12:30" 这类没有年份的写法：默认今年，若晚于当前则算去年。
			candidate := time.Date(now.Year(), t.Month(), t.Day(), t.Hour(), t.Minute(), t.Second(), 0, time.Local)
			if candidate.After(now.Add(24 * time.Hour)) {
				candidate = time.Date(now.Year()-1, t.Month(), t.Day(), t.Hour(), t.Minute(), t.Second(), 0, time.Local)
			}
			return candidate.Unix()
		}
		return t.Unix()
	}
	for _, layout := range []string{
		"Mon Jan 02 15:04:05 -0700 2006",
		"Mon Jan 2 15:04:05 -0700 2006",
		"Mon Jan 02 15:04:05 MST 2006",
	} {
		if t, err := time.Parse(layout, s); err == nil {
			return t.Unix()
		}
	}
	for _, layout := range []string{time.RFC3339, time.RFC1123Z, time.RFC1123, time.ANSIC} {
		if t, err := time.Parse(layout, s); err == nil {
			return t.Unix()
		}
	}
	if n := parseIntLoose(s); n > 1000000000 {
		return n
	}
	return 0
}

// formatTimeAbsolute 生成 SPEC 里 created_at 使用的绝对时间写法。
func formatTimeAbsolute(unix int64) string {
	if unix <= 0 {
		return ""
	}
	return time.Unix(unix, 0).Local().Format("2006-01-02 15:04:05")
}

// formatTimeText 生成适合 320x170 小屏展示的相对时间文案。
func formatTimeText(unix int64) string {
	if unix <= 0 {
		return ""
	}
	now := time.Now()
	t := time.Unix(unix, 0).Local()
	diff := now.Sub(t)
	switch {
	case diff < 0:
		return t.Format("01-02 15:04")
	case diff < time.Minute:
		return "刚刚"
	case diff < time.Hour:
		return strconv.FormatInt(int64(diff.Minutes()), 10) + "分钟前"
	case sameDay(now, t):
		return strconv.FormatInt(int64(diff.Hours()), 10) + "小时前"
	case sameDay(now.AddDate(0, 0, -1), t):
		return "昨天 " + t.Format("15:04")
	case now.Year() == t.Year():
		return t.Format("01-02 15:04")
	default:
		return t.Format("2006-01-02")
	}
}

// sameDay 判断两个时间是否是同一个自然日。
func sameDay(a, b time.Time) bool {
	ay, am, ad := a.Date()
	by, bm, bd := b.Date()
	return ay == by && am == bm && ad == bd
}

// parseDurationText 解析 "01:23" / "1:02:03" 形式的时长，返回秒。
func parseDurationText(s string) int64 {
	v := strings.TrimSpace(s)
	if v == "" {
		return 0
	}
	if !strings.Contains(v, ":") {
		return parseIntLoose(v)
	}
	total := int64(0)
	for _, part := range strings.Split(v, ":") {
		total = total*60 + parseIntLoose(part)
	}
	return total
}

// isLoginErrorText 判断上游文案是否属于登录态失效。
func isLoginErrorText(msg string) bool {
	if strings.TrimSpace(msg) == "" {
		return false
	}
	lower := strings.ToLower(msg)
	for _, keyword := range []string{"未登录", "登录已过期", "请先登录", "登录过期", "not login", "login required", "not logged in", "expired"} {
		if strings.Contains(lower, keyword) {
			return true
		}
	}
	return false
}
