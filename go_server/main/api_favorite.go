package main

import (
	"net/url"
	"strconv"
)

// ============================================================================
// 我的：收藏 / @我的 / 我的微博
//
// 收藏与 @我的 走 weibo.com 的 ajax 接口（SPEC §5 未列，属于路由表补充项），
// 响应形态是 { data: { list: [...] } }，parseStatusesFromContainer 能兼容。
// ============================================================================

// fetchFavorites 我的收藏（按页）。
func fetchFavorites(page int) (map[string]any, error) {
	if page < 1 {
		page = 1
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	params := url.Values{}
	params.Set("page", strconv.Itoa(page))
	raw, err := client.webGet(epFavoritesAllFav, params)
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

// fetchMentions @我的（按页）。
func fetchMentions(page int) (map[string]any, error) {
	if page < 1 {
		page = 1
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	params := url.Values{}
	params.Set("page", strconv.Itoa(page))
	raw, err := client.webGet(epMentionsTimeline, params)
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

// fetchMyStatuses 我的微博（等价于 fetchUserStatuses(自己的 uid)）。
func fetchMyStatuses(page int) (map[string]any, error) {
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	uid := client.uid()
	if uid == "" {
		client.refreshLoginState()
		uid = client.uid()
	}
	if uid == "" {
		return nil, errNotLoggedIn()
	}
	return fetchUserStatuses(uid, page, 0)
}
