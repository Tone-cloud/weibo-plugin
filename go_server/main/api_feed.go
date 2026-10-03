package main

import (
	"net/url"
	"strconv"
	"strings"
)

// ============================================================================
// 信息流：首页推荐 / 关注 / 分组 / 热搜榜 / 热搜微博流
//
// 统一返回 {"items": [...], "since_id": "", "has_more": bool}。
// ============================================================================

// feedResult 把容器响应组装成统一的信息流结果。
func feedResult(root map[string]any, page int) map[string]any {
	raws := parseStatusesFromContainer(root)
	sinceID := containerSinceID(root)
	out := map[string]any{}
	out["items"] = parseBlogList(raws)
	out["since_id"] = sinceID
	out["has_more"] = len(raws) > 0 && sinceID != "" && sinceID != "0"
	if page > 0 {
		out["page"] = page
	}
	return out
}

// fetchHome 首页推荐流：containerid=102803。
func fetchHome(sinceID string, freshType int) (map[string]any, error) {
	params := url.Values{}
	params.Set("containerid", containerIDHome)
	if strings.TrimSpace(sinceID) != "" {
		params.Set("since_id", strings.TrimSpace(sinceID))
	}
	if freshType > 0 {
		params.Set("fresh_type", strconv.Itoa(freshType))
	}
	raw, err := getClient().mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	return feedResult(decodeJSONMap(raw), 0), nil
}

// fetchFollow 关注流：containerid=102803_ctg1_4288_-_ctg1_4288。
func fetchFollow(sinceID string) (map[string]any, error) {
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	params := url.Values{}
	params.Set("containerid", containerIDFollow)
	if strings.TrimSpace(sinceID) != "" {
		params.Set("since_id", strings.TrimSpace(sinceID))
	}
	if uid := client.uid(); uid != "" {
		params.Set("uid", uid)
	}
	raw, err := client.mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	return feedResult(decodeJSONMap(raw), 0), nil
}

// fetchGroup 分组流：gid 既可以是 "4288" 也可以是完整 containerid。
func fetchGroup(gid, sinceID string) (map[string]any, error) {
	container := groupContainerID(gid)
	if container == "" {
		return nil, errBadRequest("缺少分组 id")
	}
	params := url.Values{}
	params.Set("containerid", container)
	if strings.TrimSpace(sinceID) != "" {
		params.Set("since_id", strings.TrimSpace(sinceID))
	}
	raw, err := getClient().mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	return feedResult(decodeJSONMap(raw), 0), nil
}

// fetchHotList 热搜榜：containerid=106003type=25&t=3&disable_hot=1&filter_type=realtimehot。
func fetchHotList() (map[string]any, error) {
	params := url.Values{}
	params.Set("containerid", containerIDHot)
	raw, err := getClient().mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	raws := parseHotListFromRoot(root)
	items := make([]any, 0, len(raws))
	for index, item := range raws {
		if parsed := parseHotItem(index+1, item); parsed != nil {
			items = append(items, parsed)
		}
	}
	out := map[string]any{}
	out["items"] = items
	return out, nil
}

// fetchHotStatus 热搜微博流：containerid=100103type=1&q=<word>。
func fetchHotStatus(word, sinceID string) (map[string]any, error) {
	keyword := strings.TrimSpace(word)
	if keyword == "" {
		return nil, errBadRequest("缺少热搜关键词")
	}
	params := url.Values{}
	params.Set("containerid", hotSearchContainerID(keyword))
	params.Set("page_type", "searchall")
	if strings.TrimSpace(sinceID) != "" {
		params.Set("since_id", strings.TrimSpace(sinceID))
	}
	raw, err := getClient().mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	return feedResult(decodeJSONMap(raw), 0), nil
}
