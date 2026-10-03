package main

import (
	"net/url"
	"strconv"
	"strings"
)

// ============================================================================
// 用户：资料 / 我的资料 / 微博 / 关注 / 粉丝 / 关注操作 / 分组 / 我的超话
// ============================================================================

// fetchUserProfile 取用户资料。优先 weibo.com ajax，失败回落到 m.weibo.cn 主页容器。
func fetchUserProfile(uid string) (map[string]any, error) {
	target := strings.TrimSpace(uid)
	if target == "" {
		return nil, errBadRequest("缺少用户 uid")
	}
	client := getClient()
	params := url.Values{}
	params.Set("uid", target)
	userMap := map[string]any(nil)
	if raw, err := client.webGet(epProfileInfo, params); err == nil {
		userMap = extractUserMap(decodeJSONMap(raw))
	} else {
		logWarn("weibo.com 资料接口失败，回落到 m.weibo.cn：%v", err)
	}
	if userMap == nil {
		fallback := url.Values{}
		fallback.Set("containerid", profileInfoContainerID(target))
		if raw, err := client.mGet(epContainerIndex, fallback); err == nil {
			root := decodeJSONMap(raw)
			if data := asMap(root["data"]); data != nil {
				userMap = asMap(data["userInfo"])
			}
			if userMap == nil {
				userMap = extractUserMap(root)
			}
		}
	}
	if userMap == nil {
		return nil, errNotFoundUser()
	}
	user := parseUser(userMap)
	if user == nil {
		return nil, errNotFoundUser()
	}
	out := map[string]any{}
	out["user"] = user
	return out, nil
}

// errNotFoundUser 统一的「用户不存在」中文错误。
func errNotFoundUser() error {
	return &appError{Code: -3, Status: 200, Message: "未找到该用户"}
}

// fetchMe 取当前登录用户资料。
func fetchMe() (map[string]any, error) {
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
	return fetchUserProfile(uid)
}

// fetchUserStatuses 用户微博列表（containerid=107603<uid>）。
func fetchUserStatuses(uid string, page int, feature int) (map[string]any, error) {
	target := strings.TrimSpace(uid)
	if target == "" {
		return nil, errBadRequest("缺少用户 uid")
	}
	if page < 1 {
		page = 1
	}
	params := url.Values{}
	params.Set("containerid", profileContainerID(target))
	if page > 1 {
		params.Set("page", strconv.Itoa(page))
	}
	if feature > 0 {
		params.Set("feature", strconv.Itoa(feature))
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

// fetchFollowing 关注列表。uid 为空或等于自己时用 231093_-_selffollowed。
func fetchFollowing(uid string, page int) (map[string]any, error) {
	return fetchRelationList(uid, page, true)
}

// fetchFollowers 粉丝列表。uid 为空或等于自己时用 231093_-_followers。
func fetchFollowers(uid string, page int) (map[string]any, error) {
	return fetchRelationList(uid, page, false)
}

// fetchRelationList 关注 / 粉丝列表的公共实现。
func fetchRelationList(uid string, page int, following bool) (map[string]any, error) {
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	target := strings.TrimSpace(uid)
	container := containerIDFollowers
	if following {
		container = containerIDSelfFollowed
	}
	params := url.Values{}
	params.Set("containerid", container)
	if page > 1 {
		params.Set("page", strconv.Itoa(page))
	}
	// m.weibo.cn 只提供「我的」关注 / 粉丝容器；看别人时补一个 uid 参数，
	// 上游若能识别就直接返回对应用户的列表。
	if target != "" {
		params.Set("uid", target)
	}
	raw, err := client.mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	users := parseUserCards(root)
	total := int64(0)
	if data := asMap(root["data"]); data != nil {
		total = asInt64(data["total_number"])
		if total == 0 {
			total = asInt64(data["total"])
		}
	}
	out := map[string]any{}
	out["items"] = parseUserList(users)
	out["page"] = page
	out["total"] = total
	out["has_more"] = len(users) > 0
	return out, nil
}

// followUser 关注 / 取关。
func followUser(uid string, follow bool) (map[string]any, error) {
	target := strings.TrimSpace(uid)
	if target == "" {
		return nil, errBadRequest("缺少用户 uid")
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	endpoint := epFriendshipDestroy
	if follow {
		endpoint = epFriendshipCreate
	}
	form := url.Values{}
	form.Set("uid", target)
	raw, err := client.mPost(endpoint, form)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	fallback := "取关失败"
	if follow {
		fallback = "关注失败"
	}
	if err := ensureUpstreamOK(root, fallback); err != nil {
		return nil, err
	}
	out := map[string]any{}
	out["ok"] = true
	out["following"] = follow
	return out, nil
}

// fetchUserGroups 我的分组列表。
// 上游把分组信息放在关注容器的卡片里，这里用宽松扫描兜底。
func fetchUserGroups() (map[string]any, error) {
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	params := url.Values{}
	params.Set("containerid", containerIDSelfFollowed)
	raw, err := client.mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	groups := collectGroupMaps(root)
	items := make([]any, 0, len(groups)+1)
	for _, group := range groups {
		gid := asString(group["gid"])
		if gid == "" {
			continue
		}
		item := map[string]any{}
		item["gid"] = gid
		item["title"] = firstNonEmpty(asString(group["title"]), "未命名分组")
		items = append(items, item)
	}
	if len(items) == 0 {
		item := map[string]any{}
		item["gid"] = containerIDFollow
		item["title"] = "默认分组"
		items = append(items, item)
	}
	out := map[string]any{}
	out["items"] = items
	return out, nil
}

// fetchMyTopics 我的超话列表（weibo.com/ajax/profile/topicList）。
func fetchMyTopics(page int) (map[string]any, error) {
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	if page < 1 {
		page = 1
	}
	params := url.Values{}
	params.Set("page", strconv.Itoa(page))
	raw, err := client.webGet(epSuperTopicList, params)
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
