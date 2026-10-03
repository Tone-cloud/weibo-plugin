package main

import (
	"errors"
	"net/url"
	"strconv"
	"strings"
)

// ============================================================================
// 微博：详情 / 转发列表 / 点赞列表 / 点赞 / 收藏 / 转发 / 删除
// ============================================================================

// fetchStatusDetail 取单条微博详情（/statuses/show）。
func fetchStatusDetail(id string) (map[string]any, error) {
	statusID := strings.TrimSpace(id)
	if statusID == "" {
		return nil, errBadRequest("缺少微博 id")
	}
	params := url.Values{}
	params.Set("id", statusID)
	raw, err := getClient().mGet(epStatusShow, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	status := parseBlog(root)
	if status == nil || asString(status["id"]) == "" {
		return nil, errors.New("未找到该微博，可能已被删除或需要登录")
	}
	out := map[string]any{}
	out["status"] = status
	return out, nil
}

// fetchReposts 转发列表（按页）。
func fetchReposts(id string, page int) (map[string]any, error) {
	statusID := strings.TrimSpace(id)
	if statusID == "" {
		return nil, errBadRequest("缺少微博 id")
	}
	if page < 1 {
		page = 1
	}
	params := url.Values{}
	params.Set("id", statusID)
	params.Set("page", strconv.Itoa(page))
	raw, err := getClient().mGet(epRepostTimeline, params)
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

// fetchLikers 点赞列表（按页）。
func fetchLikers(id string, page int) (map[string]any, error) {
	statusID := strings.TrimSpace(id)
	if statusID == "" {
		return nil, errBadRequest("缺少微博 id")
	}
	if page < 1 {
		page = 1
	}
	params := url.Values{}
	params.Set("id", statusID)
	params.Set("page", strconv.Itoa(page))
	raw, err := getClient().mGet(epAttitudesList, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	users := parseUserListFromRoot(root)
	out := map[string]any{}
	out["items"] = parseUserList(users)
	out["page"] = page
	out["has_more"] = len(users) > 0
	return out, nil
}

// fetchLikesToggle 点赞 / 取消赞。
func fetchLikesToggle(id string, liked bool) (map[string]any, error) {
	statusID := strings.TrimSpace(id)
	if statusID == "" {
		return nil, errBadRequest("缺少微博 id")
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	target := epStatusUnlike
	if liked {
		target = epStatusLike
	}
	form := url.Values{}
	form.Set("id", statusID)
	raw, err := client.mPost(target, form)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	if err := ensureUpstreamOK(root, "操作失败，请稍后重试"); err != nil {
		return nil, err
	}
	out := map[string]any{}
	out["ok"] = true
	out["liked"] = liked
	return out, nil
}

// fetchFavoriteToggle 收藏 / 取消收藏（weibo.com 写操作，需要 XSRF-TOKEN）。
func fetchFavoriteToggle(id string, fav bool) (map[string]any, error) {
	statusID := strings.TrimSpace(id)
	if statusID == "" {
		return nil, errBadRequest("缺少微博 id")
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	target := epStatusUnfavorite
	if fav {
		target = epStatusFavorite
	}
	form := url.Values{}
	form.Set("id", statusID)
	raw, err := client.webPost(target, form)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	if err := ensureWebOK(root, "收藏操作失败"); err != nil {
		return nil, err
	}
	out := map[string]any{}
	out["ok"] = true
	out["favorited"] = fav
	return out, nil
}

// fetchStatusDelete 删除自己发的微博。
func fetchStatusDelete(id string) (map[string]any, error) {
	statusID := strings.TrimSpace(id)
	if statusID == "" {
		return nil, errBadRequest("缺少微博 id")
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	form := url.Values{}
	form.Set("id", statusID)
	raw, err := client.webPost(epStatusDestroy, form)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	if err := ensureWebOK(root, "删除失败"); err != nil {
		return nil, err
	}
	out := map[string]any{}
	out["ok"] = true
	return out, nil
}

// postRepost 转发微博（content 可以为空 → 直接转发）。
func postRepost(id, content string) (map[string]any, error) {
	statusID := strings.TrimSpace(id)
	if statusID == "" {
		return nil, errBadRequest("缺少微博 id")
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	form := url.Values{}
	form.Set("id", statusID)
	form.Set("content", strings.TrimSpace(content))
	form.Set("mid", statusID)
	raw, err := client.mPost(epStatusRepost, form)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	if err := ensureUpstreamOK(root, "转发失败"); err != nil {
		return nil, err
	}
	out := map[string]any{}
	out["ok"] = true
	out["id"] = extractCreatedID(root)
	return out, nil
}
