package main

import (
	"encoding/json"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// ============================================================================
// 搜索：综合 / 用户 / 话题 + 搜索历史持久化
// ============================================================================

// searchStatus 综合搜索（containerid=100103type=1&q=...&page_type=searchall）。
func searchStatus(q string, page int) (map[string]any, error) {
	keyword := strings.TrimSpace(q)
	if keyword == "" {
		return nil, errBadRequest("搜索关键词不能为空")
	}
	if page < 1 {
		page = 1
	}
	params := url.Values{}
	params.Set("containerid", searchContainerID(searchKindStatus, keyword))
	params.Set("page_type", "searchall")
	if page > 1 {
		params.Set("page", strconv.Itoa(page))
	}
	raw, err := getClient().mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	raws := parseStatusesFromContainer(root)
	out := map[string]any{}
	out["items"] = parseBlogList(raws)
	out["page"] = page
	out["has_more"] = len(raws) > 0
	return out, nil
}

// searchUsers 用户搜索（containerid=100103type=3&q=...）。
func searchUsers(q string, page int) (map[string]any, error) {
	keyword := strings.TrimSpace(q)
	if keyword == "" {
		return nil, errBadRequest("搜索关键词不能为空")
	}
	if page < 1 {
		page = 1
	}
	params := url.Values{}
	params.Set("containerid", searchContainerID(searchKindUser, keyword))
	params.Set("page_type", "searchall")
	if page > 1 {
		params.Set("page", strconv.Itoa(page))
	}
	raw, err := getClient().mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	users := parseUserCards(root)
	out := map[string]any{}
	out["items"] = parseUserList(users)
	out["page"] = page
	out["has_more"] = len(users) > 0
	return out, nil
}

// searchTopics 话题 / 超话搜索（containerid=100103type=60&q=...）。
func searchTopics(q string, page int) (map[string]any, error) {
	keyword := strings.TrimSpace(q)
	if keyword == "" {
		return nil, errBadRequest("搜索关键词不能为空")
	}
	if page < 1 {
		page = 1
	}
	params := url.Values{}
	params.Set("containerid", searchContainerID(searchKindTopic, keyword))
	params.Set("page_type", "searchall")
	if page > 1 {
		params.Set("page", strconv.Itoa(page))
	}
	raw, err := getClient().mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	topics := parseTopicList(root)
	out := map[string]any{}
	out["items"] = parseTopicItems(topics)
	out["page"] = page
	out["has_more"] = len(topics) > 0
	return out, nil
}

// ======================= 搜索历史 =======================

// searchHistoryLimit 最多保留 30 条（最新的在前）。
const searchHistoryLimit = 30

// searchHistoryPayload 是 search_history.json 的结构。
type searchHistoryPayload struct {
	Words     []string `json:"words"`
	UpdatedAt int64    `json:"updated_at"`
}

// searchHistoryPath 与 cookies.json 放在同一个目录。
func searchHistoryPath() string {
	dir := filepath.Dir(cookieStorePath())
	if dir == "" {
		dir = "."
	}
	return filepath.Join(dir, "search_history.json")
}

// normalizeWords 去空白、去重（保持顺序）、截断到上限。
func normalizeWords(words []string) []string {
	seen := map[string]bool{}
	out := make([]string, 0, len(words))
	for _, word := range words {
		w := strings.TrimSpace(word)
		if w == "" || seen[w] {
			continue
		}
		seen[w] = true
		out = append(out, w)
		if len(out) >= searchHistoryLimit {
			break
		}
	}
	return out
}

// loadSearchHistory 读取搜索历史；文件不存在时返回空列表。
func loadSearchHistory() ([]string, error) {
	raw, err := os.ReadFile(searchHistoryPath())
	if err != nil {
		if os.IsNotExist(err) {
			return []string{}, nil
		}
		return nil, err
	}
	if len(strings.TrimSpace(string(raw))) == 0 {
		return []string{}, nil
	}
	var payload searchHistoryPayload
	if err := json.Unmarshal(raw, &payload); err != nil {
		var list []string
		if err2 := json.Unmarshal(raw, &list); err2 == nil {
			return normalizeWords(list), nil
		}
		return []string{}, nil
	}
	return normalizeWords(payload.Words), nil
}

// saveSearchHistory 落盘。
func saveSearchHistory(words []string) error {
	payload := searchHistoryPayload{
		Words:     normalizeWords(words),
		UpdatedAt: time.Now().Unix(),
	}
	raw, err := json.MarshalIndent(payload, "", "  ")
	if err != nil {
		return err
	}
	raw = append(raw, '\n')
	path := searchHistoryPath()
	if dir := filepath.Dir(path); dir != "" && dir != "." {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
	}
	return os.WriteFile(path, raw, 0o600)
}

// appendSearchHistory 把关键词插到最前面并落盘，返回最新列表。
func appendSearchHistory(word string) ([]string, error) {
	target := strings.TrimSpace(word)
	current, err := loadSearchHistory()
	if err != nil {
		current = []string{}
	}
	if target == "" {
		return current, nil
	}
	next := make([]string, 0, len(current)+1)
	next = append(next, target)
	for _, item := range current {
		if item == target {
			continue
		}
		next = append(next, item)
	}
	next = normalizeWords(next)
	if err := saveSearchHistory(next); err != nil {
		return next, err
	}
	return next, nil
}

// clearSearchHistory 清空搜索历史文件。
func clearSearchHistory() error {
	err := os.Remove(searchHistoryPath())
	if err != nil && !os.IsNotExist(err) {
		return err
	}
	return nil
}
