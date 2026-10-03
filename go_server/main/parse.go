package main

import (
	"bytes"
	"encoding/json"
	"net/url"
	"strconv"
	"strings"
)

// ============================================================================
// m.weibo.cn / weibo.com 原始 JSON → 归一化结构（SPEC 第 3 节）
//
// 上游是私有 Web API，同一个字段在不同接口里可能是对象、字符串或缺失，
// 所以这里所有取值都走 nil 安全的 asXxx 辅助函数。
//
// 重要约定：
//   - 微博 / 评论的 id 一律输出 JSON 字符串（数值超过 int32）；
//   - 用户的 id 一律输出 JSON 数字（SPEC 3.2）。
// ============================================================================

// ======================= nil 安全的取值辅助 =======================

// asMap 尽力把任意值转成 map[string]any。
func asMap(v any) map[string]any {
	switch t := v.(type) {
	case nil:
		return nil
	case map[string]any:
		return t
	case json.RawMessage:
		return jsonObjectToMap(t)
	case []byte:
		return jsonObjectToMap(t)
	case string:
		s := strings.TrimSpace(t)
		if strings.HasPrefix(s, "{") {
			return jsonObjectToMap([]byte(s))
		}
		return nil
	default:
		return nil
	}
}

// asSlice 尽力把任意值转成 []any。
func asSlice(v any) []any {
	switch t := v.(type) {
	case nil:
		return nil
	case []any:
		return t
	case []map[string]any:
		out := make([]any, 0, len(t))
		for _, item := range t {
			out = append(out, item)
		}
		return out
	case []string:
		out := make([]any, 0, len(t))
		for _, item := range t {
			out = append(out, item)
		}
		return out
	default:
		return nil
	}
}

// asString 尽力把任意值转成字符串；json.Number 会保留原始十进制写法。
func asString(v any) string {
	switch t := v.(type) {
	case nil:
		return ""
	case string:
		return t
	case json.Number:
		return t.String()
	case float64:
		return strconv.FormatFloat(t, 'f', -1, 64)
	case float32:
		return strconv.FormatFloat(float64(t), 'f', -1, 32)
	case int:
		return strconv.Itoa(t)
	case int64:
		return strconv.FormatInt(t, 10)
	case bool:
		if t {
			return "true"
		}
		return "false"
	default:
		return ""
	}
}

// asInt64 尽力把任意值转成 int64。
func asInt64(v any) int64 {
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
	case json.Number:
		if n, err := t.Int64(); err == nil {
			return n
		}
		if f, err := t.Float64(); err == nil {
			return int64(f)
		}
		return 0
	case bool:
		if t {
			return 1
		}
		return 0
	case string:
		return parseIntLoose(t)
	default:
		return 0
	}
}

// asFloat 尽力把任意值转成 float64。
func asFloat(v any) float64 {
	switch t := v.(type) {
	case nil:
		return 0
	case float64:
		return t
	case float32:
		return float64(t)
	case int:
		return float64(t)
	case int64:
		return float64(t)
	case json.Number:
		if f, err := t.Float64(); err == nil {
			return f
		}
		return 0
	case string:
		if f, err := strconv.ParseFloat(strings.TrimSpace(t), 64); err == nil {
			return f
		}
		return 0
	default:
		return 0
	}
}

// asBool 尽力把任意值转成 bool（兼容 1/0、"true"、"是"）。
func asBool(v any) bool {
	switch t := v.(type) {
	case nil:
		return false
	case bool:
		return t
	case string:
		s := strings.ToLower(strings.TrimSpace(t))
		switch s {
		case "1", "true", "yes", "y", "on", "是":
			return true
		default:
			return false
		}
	case json.Number:
		return t.String() != "0" && t.String() != ""
	case float64:
		return t != 0
	case float32:
		return t != 0
	case int:
		return t != 0
	case int64:
		return t != 0
	default:
		return false
	}
}

// jsonObjectToMap 把字节流转成 map；失败返回空 map（绝不返回 nil）。
func jsonObjectToMap(raw []byte) map[string]any {
	if len(raw) == 0 {
		return map[string]any{}
	}
	if payload, ok := extractJSONObject(raw); ok {
		raw = payload
	}
	dec := json.NewDecoder(bytes.NewReader(raw))
	dec.UseNumber()
	var value any
	if err := dec.Decode(&value); err != nil {
		return map[string]any{}
	}
	if m := asMap(value); m != nil {
		return m
	}
	return map[string]any{}
}

// decodeJSONMap 把 json.RawMessage 转成 map（使用 json.Number 保留大整数精度）。
func decodeJSONMap(raw json.RawMessage) map[string]any {
	return jsonObjectToMap(raw)
}

// walkMaps 深度优先遍历任意 JSON 值里的所有对象，depth 用于兜底防止异常嵌套。
func walkMaps(v any, depth int, fn func(map[string]any)) {
	if depth > 12 || v == nil {
		return
	}
	switch t := v.(type) {
	case map[string]any:
		fn(t)
		for _, key := range sortedKeys(t) {
			walkMaps(t[key], depth+1, fn)
		}
	case []any:
		for _, item := range t {
			walkMaps(item, depth+1, fn)
		}
	default:
		return
	}
}

// ======================= 上游形态归一 =======================

// looksLikeStatus 粗略判断一个对象是不是微博。
func looksLikeStatus(m map[string]any) bool {
	if m == nil {
		return false
	}
	if asString(m["id"]) == "" && asString(m["idstr"]) == "" {
		return false
	}
	if asString(m["text"]) != "" {
		return true
	}
	if asMap(m["user"]) != nil {
		return true
	}
	return asString(m["created_at"]) != ""
}

// normalizeStatusMap 兼容三种上游形态：
//  1. getIndex 卡片（card_type==9，微博挂在 "mblog" 下）；
//  2. /statuses/show 响应（字段平铺在顶层）；
//  3. /statuses/show 响应包了一层 "data"。
func normalizeStatusMap(input map[string]any) map[string]any {
	if input == nil {
		return nil
	}
	if inner := asMap(input["mblog"]); inner != nil {
		return inner
	}
	data := asMap(input["data"])
	if data != nil {
		if inner := asMap(data["mblog"]); inner != nil {
			return inner
		}
		if inner := asMap(data["status"]); inner != nil {
			return inner
		}
		if looksLikeStatus(data) {
			return data
		}
	}
	if looksLikeStatus(input) {
		return input
	}
	if data != nil {
		return data
	}
	return input
}

// normalizeUserMap 兼容 {user:{...}} / {data:{user:{...}}} / 平铺三种形态。
func normalizeUserMap(input map[string]any) map[string]any {
	if input == nil {
		return nil
	}
	if inner := asMap(input["user"]); inner != nil {
		return inner
	}
	if data := asMap(input["data"]); data != nil {
		if inner := asMap(data["user"]); inner != nil {
			return inner
		}
		if inner := asMap(data["userInfo"]); inner != nil {
			return inner
		}
		return data
	}
	return input
}

// normalizeCommentMap 兼容 {comment:{...}} / {data:{comment:{...}}} / 平铺三种形态。
func normalizeCommentMap(input map[string]any) map[string]any {
	if input == nil {
		return nil
	}
	if inner := asMap(input["comment"]); inner != nil {
		return inner
	}
	if data := asMap(input["data"]); data != nil {
		if inner := asMap(data["comment"]); inner != nil {
			return inner
		}
	}
	return input
}

// normalizeTopicMap 兼容 {topic:{...}} / {topicInfo:{...}} / 平铺三种形态。
func normalizeTopicMap(input map[string]any) map[string]any {
	if input == nil {
		return nil
	}
	if inner := asMap(input["topic"]); inner != nil {
		return inner
	}
	if inner := asMap(input["topicInfo"]); inner != nil {
		return inner
	}
	if inner := asMap(input["topic_info"]); inner != nil {
		return inner
	}
	if data := asMap(input["data"]); data != nil {
		if inner := asMap(data["topic"]); inner != nil {
			return inner
		}
		if inner := asMap(data["topicInfo"]); inner != nil {
			return inner
		}
	}
	return input
}

// normalizeGender 把上游性别转成 m / f / n。
func normalizeGender(raw string) string {
	switch strings.ToLower(strings.TrimSpace(raw)) {
	case "m", "male", "男", "1":
		return "m"
	case "f", "female", "女", "0":
		return "f"
	default:
		return "n"
	}
}

// currentUID 返回当前登录 uid（没有客户端时返回空串）。
func currentUID() string {
	client := getClient()
	if client == nil {
		return ""
	}
	return client.uid()
}

// isMeUID 判断某个数字 uid 是否是当前登录用户。
func isMeUID(uid int64) bool {
	me := currentUID()
	if me == "" || uid <= 0 {
		return false
	}
	return me == strconv.FormatInt(uid, 10)
}

// emptyUser 返回一个字段齐全的空 UserItem。
// 用逐个赋值而不是复合字面量，避免 gofmt 的列对齐问题，也更易读。
func emptyUser() map[string]any {
	out := map[string]any{}
	out["id"] = int64(0)
	out["name"] = ""
	out["avatar"] = ""
	out["cover"] = ""
	out["verified"] = false
	out["verified_type"] = int64(-1)
	out["verified_reason"] = ""
	out["description"] = ""
	out["followers"] = int64(0)
	out["following"] = int64(0)
	out["statuses_count"] = int64(0)
	out["gender"] = "n"
	out["location"] = ""
	out["following_me"] = false
	out["follow_me"] = false
	out["is_following"] = false
	out["is_me"] = false
	return out
}

// ======================= UserItem =======================

// parseUser 把上游用户对象归一化成 UserItem（SPEC 3.2）。
//
// 字段口径（参考 C++ WeiboUser）：
//
//	following    —— 关注数（数字）
//	followers    —— 粉丝数（数字）
//	follow_me    —— 我是否已关注 TA（布尔，等价 is_following）
//	following_me —— TA 是否关注我（布尔）
func parseUser(u map[string]any) map[string]any {
	user := normalizeUserMap(u)
	if user == nil {
		return nil
	}
	out := map[string]any{}
	id := asInt64(user["id"])
	if id == 0 {
		id = parseIntLoose(asString(user["idstr"]))
	}
	out["id"] = id
	out["name"] = firstNonEmpty(asString(user["screen_name"]), asString(user["name"]), asString(user["nickname"]))
	out["avatar"] = firstNonEmpty(
		asString(user["avatar_hd"]),
		asString(user["avatar_large"]),
		asString(user["profile_image_url"]),
		asString(user["avatar"]),
	)
	out["cover"] = firstNonEmpty(asString(user["cover_image_phone"]), asString(user["cover_image"]))
	verifiedType := asInt64(user["verified_type"])
	if _, ok := user["verified_type"]; !ok {
		verifiedType = -1
	}
	out["verified_type"] = verifiedType
	verified := asBool(user["verified"])
	if !verified && verifiedType >= 0 {
		verified = true
	}
	out["verified"] = verified
	out["verified_reason"] = firstNonEmpty(asString(user["verified_reason"]), asString(user["verified_reason_modified"]))
	out["description"] = asString(user["description"])
	out["followers"] = asInt64(user["followers_count"])
	out["following"] = asInt64(user["follow_count"])
	out["statuses_count"] = asInt64(user["statuses_count"])
	out["gender"] = normalizeGender(asString(user["gender"]))
	out["location"] = firstNonEmpty(asString(user["location"]), asString(user["ip_location"]))
	followingMe := asBool(user["follow_me"])
	if !followingMe {
		followingMe = asBool(user["following_me"])
	}
	out["following_me"] = followingMe
	isFollowing := asBool(user["following"])
	out["follow_me"] = isFollowing
	out["is_following"] = isFollowing
	out["is_me"] = isMeUID(id)
	return out
}

// parseUserList 把原始用户 map 列表归一化成 []any（每个元素是 UserItem）。
func parseUserList(users []map[string]any) []any {
	out := make([]any, 0, len(users))
	for _, u := range users {
		if item := parseUser(u); item != nil {
			out = append(out, item)
		}
	}
	return out
}

// ======================= 图片 =======================

// pickPicURL 从 pic_infos 的一个条目里按优先级取 URL。
func pickPicURL(info map[string]any, keys ...string) string {
	for _, key := range keys {
		if sub := asMap(info[key]); sub != nil {
			if u := firstNonEmpty(asString(sub["url"]), asString(sub["large"])); u != "" {
				return u
			}
		}
		if u := asString(info[key]); u != "" {
			if strings.HasPrefix(u, "http") {
				return u
			}
		}
	}
	return ""
}

// appendPicture 追加一张图片（去重）。
func appendPicture(out []any, thumb, large string, width, height int64) []any {
	if thumb == "" && large == "" {
		return out
	}
	if thumb == "" {
		thumb = large
	}
	if large == "" {
		large = thumb
	}
	for _, item := range out {
		existing := asMap(item)
		if existing == nil {
			continue
		}
		if asString(existing["large"]) == large {
			return out
		}
	}
	pic := map[string]any{}
	pic["url"] = thumb
	pic["large"] = large
	pic["width"] = width
	pic["height"] = height
	return append(out, pic)
}

// pictureEntriesFromInfos 把 pic_infos（map）按 pic_ids 顺序展开成图片数组。
func pictureEntriesFromInfos(infos map[string]any, ids []any) []any {
	out := []any{}
	appendInfo := func(key string) {
		info := asMap(infos[key])
		if info == nil {
			return
		}
		large := pickPicURL(info, "largest", "large", "original", "bmiddle", "thumbnail")
		thumb := pickPicURL(info, "bmiddle", "large", "thumbnail", "largest")
		out = appendPicture(out, thumb, large, asInt64(info["width"]), asInt64(info["height"]))
	}
	for _, id := range ids {
		key := asString(id)
		if key == "" {
			continue
		}
		appendInfo(key)
	}
	if len(out) == 0 {
		for _, key := range sortedKeys(infos) {
			appendInfo(key)
		}
	}
	return out
}

// parsePictures 解析微博图片列表。
func parsePictures(m map[string]any) []any {
	out := []any{}
	if pics := asSlice(m["pics"]); len(pics) > 0 {
		for _, item := range pics {
			pic := asMap(item)
			if pic == nil {
				continue
			}
			large := firstNonEmpty(asString(pic["large"]), asString(pic["url"]))
			thumb := firstNonEmpty(asString(pic["url"]), large)
			out = appendPicture(out, thumb, large, asInt64(pic["width"]), asInt64(pic["height"]))
		}
	}
	if len(out) == 0 {
		if infos := asMap(m["pic_infos"]); infos != nil {
			out = pictureEntriesFromInfos(infos, asSlice(m["pic_ids"]))
		}
	}
	if len(out) == 0 {
		original := firstNonEmpty(asString(m["original_pic"]), asString(m["thumbnail_pic"]))
		if original != "" {
			out = appendPicture(out, firstNonEmpty(asString(m["thumbnail_pic"]), original), original, 0, 0)
		}
	}
	return out
}

// parseCommentPics 解析评论图片（pic 对象 / pic_ids / pic_infos 三种形态）。
func parseCommentPics(comment map[string]any) []any {
	out := []any{}
	if pic := asMap(comment["pic"]); pic != nil {
		large := firstNonEmpty(asString(pic["large"]), asString(pic["url"]), asString(pic["bmiddle_pic"]))
		thumb := firstNonEmpty(asString(pic["url"]), asString(pic["bmiddle_pic"]), large)
		out = appendPicture(out, thumb, large, asInt64(pic["width"]), asInt64(pic["height"]))
	}
	if len(out) == 0 {
		if infos := asMap(comment["pic_infos"]); infos != nil {
			out = pictureEntriesFromInfos(infos, asSlice(comment["pic_ids"]))
		}
	}
	if len(out) == 0 {
		for _, id := range asSlice(comment["pic_ids"]) {
			pid := asString(id)
			if pid == "" {
				continue
			}
			large := "https://wx1.sinaimg.cn/large/" + pid + ".jpg"
			out = appendPicture(out, large, large, 0, 0)
		}
	}
	return out
}

// ======================= PageInfo =======================

// pageInfoPic 从 page_info 里取封面。
func pageInfoPic(pageInfo map[string]any) string {
	if pic := asMap(pageInfo["page_pic"]); pic != nil {
		if u := firstNonEmpty(asString(pic["url"]), asString(pic["large"])); u != "" {
			return u
		}
	}
	if raw := asString(pageInfo["page_pic"]); raw != "" {
		return raw
	}
	return firstNonEmpty(asString(pageInfo["pic"]), asString(pageInfo["cover"]), asString(pageInfo["cover_image"]))
}

// pageInfoDuration 解析时长（可能是秒数，也可能是 "01:23"）。
func pageInfoDuration(v any) int64 {
	if s, ok := v.(string); ok && strings.Contains(s, ":") {
		return parseDurationText(s)
	}
	return asInt64(v)
}

// parsePageInfo 归一化 page_info（SPEC 3.1）：视频 / 直播 / 文章 / 音乐。
func parsePageInfo(m map[string]any) map[string]any {
	out := map[string]any{}
	out["type"] = "none"
	out["title"] = ""
	out["cover"] = ""
	out["url"] = ""
	out["media_url"] = ""
	out["duration"] = int64(0)
	out["live_status"] = int64(0)
	if m == nil {
		return out
	}
	pageInfo := asMap(m["page_info"])
	if pageInfo == nil {
		if data := asMap(m["data"]); data != nil {
			pageInfo = asMap(data["page_info"])
		}
	}
	if pageInfo == nil {
		return out
	}
	kind := strings.ToLower(firstNonEmpty(asString(pageInfo["type"]), asString(pageInfo["object_type"])))
	switch kind {
	case "video", "live", "article", "music":
		out["type"] = kind
	case "0", "1", "none", "":
		out["type"] = "none"
	default:
		out["type"] = kind
	}
	out["title"] = firstNonEmpty(asString(pageInfo["page_title"]), asString(pageInfo["title"]))
	out["cover"] = pageInfoPic(pageInfo)
	out["url"] = firstNonEmpty(
		asString(pageInfo["page_url"]),
		asString(pageInfo["short_url"]),
		asString(pageInfo["url"]),
		asString(pageInfo["object_id"]),
	)
	if media := asMap(pageInfo["media_info"]); media != nil {
		out["media_url"] = firstNonEmpty(
			asString(media["stream_url_hd"]),
			asString(media["stream_url"]),
			asString(media["mp4_1080p_mp4"]),
			asString(media["mp4_720p_mp4"]),
			asString(media["mp4_hd_url"]),
			asString(media["mp4_sd_url"]),
			asString(media["h5_mp4_url"]),
			asString(media["url"]),
		)
		if duration := pageInfoDuration(media["duration"]); duration > 0 {
			out["duration"] = duration
		}
		if liveStatus := asInt64(media["live_status"]); liveStatus != 0 {
			out["live_status"] = liveStatus
		}
	}
	if asString(out["media_url"]) == "" {
		out["media_url"] = firstNonEmpty(
			asString(pageInfo["stream_url_hd"]),
			asString(pageInfo["stream_url"]),
			asString(pageInfo["mp4_720p_mp4"]),
			asString(pageInfo["mp4_hd_url"]),
			asString(pageInfo["mp4_sd_url"]),
			asString(pageInfo["h5_mp4_url"]),
		)
	}
	if duration := pageInfoDuration(firstNonEmptyAny(pageInfo["duration"], pageInfo["video_duration"])); duration > 0 && asInt64(out["duration"]) == 0 {
		out["duration"] = duration
	}
	// 直播：live_info 可能是对象，也可能是被转义过的 JSON 字符串。
	live := asMap(pageInfo["live_info"])
	if live == nil {
		if raw := strings.TrimSpace(asString(pageInfo["live_info"])); strings.HasPrefix(raw, "{") {
			live = jsonObjectToMap([]byte(raw))
		}
	}
	if live != nil {
		if asString(out["type"]) == "none" {
			out["type"] = "live"
		}
		if asString(out["title"]) == "" {
			out["title"] = firstNonEmpty(asString(live["title"]), asString(live["name"]))
		}
		if asString(out["cover"]) == "" {
			out["cover"] = firstNonEmpty(asString(live["cover"]), asString(live["cover_url"]), asString(live["pic"]))
		}
		if asInt64(out["live_status"]) == 0 {
			out["live_status"] = asInt64(live["status"])
		}
		liveURL := firstNonEmpty(
			asString(live["stream_url"]),
			asString(live["stream_url_hd"]),
			asString(live["hls_url"]),
			asString(live["rtmp_url"]),
			asString(live["url"]),
		)
		if liveURL != "" {
			if asString(out["media_url"]) == "" {
				out["media_url"] = liveURL
			}
			if asString(out["url"]) == "" {
				out["url"] = liveURL
			}
		}
	}
	return out
}

// ======================= BlogItem =======================

// parseTopicIDs 提取微博关联的话题 id。
func parseTopicIDs(m map[string]any) []any {
	out := []any{}
	seen := map[string]bool{}
	for _, item := range asSlice(m["topic_ids"]) {
		id := asString(item)
		if id == "" || seen[id] {
			continue
		}
		seen[id] = true
		out = append(out, id)
	}
	if len(out) == 0 {
		id := firstNonEmpty(asString(m["topic_id"]), asString(m["topicid"]))
		if id != "" {
			out = append(out, id)
		}
	}
	return out
}

// canDeleteStatus 判断当前登录用户能否删除这条微博。
func canDeleteStatus(m map[string]any, author map[string]any) bool {
	if asBool(m["can_delete"]) {
		return true
	}
	me := currentUID()
	if me == "" {
		return false
	}
	authorID := asString(author["id"])
	return authorID != "" && authorID == me
}

// parseBlog 归一化一条微博（SPEC 3.1）。只保留一层转发。
func parseBlog(mblog map[string]any) map[string]any {
	return parseBlogDepth(mblog, 0)
}

// parseBlogDepth depth==0 时解析 retweeted，depth>0 时把 retweeted 置空。
func parseBlogDepth(mblog map[string]any, depth int) map[string]any {
	m := normalizeStatusMap(mblog)
	if m == nil {
		return nil
	}
	out := map[string]any{}
	id := firstNonEmpty(asString(m["id"]), asString(m["idstr"]))
	out["id"] = id
	out["bid"] = firstNonEmpty(asString(m["bid"]), asString(m["mblogid"]))
	rawHTML := firstNonEmpty(asString(m["text"]), asString(m["longText"]), asString(m["text_raw"]))
	out["text"] = trimHTML(rawHTML)
	out["text_html"] = sanitizeWeiboHTML(rawHTML)
	createdRaw := strings.TrimSpace(asString(m["created_at"]))
	createdTS := parseWeiboTimeToUnix(createdRaw)
	if createdTS <= 0 {
		createdTS = asInt64(m["created_timestamp"])
	}
	if createdTS <= 0 {
		createdTS = asInt64(m["created_ts"])
	}
	out["created_ts"] = createdTS
	if createdTS > 0 {
		out["created_at"] = formatTimeAbsolute(createdTS)
		out["created_text"] = formatTimeText(createdTS)
	} else {
		out["created_at"] = createdRaw
		out["created_text"] = createdRaw
	}
	out["source"] = trimHTML(asString(m["source"]))
	out["region_name"] = firstNonEmpty(asString(m["region_name"]), asString(m["region"]), asString(m["status_region"]))
	out["is_long_text"] = asBool(m["isLongText"]) || asBool(m["is_long_text"])
	author := parseUser(asMap(m["user"]))
	if author == nil {
		author = emptyUser()
	}
	out["author"] = author
	out["pics"] = parsePictures(m)
	out["page_info"] = parsePageInfo(m)
	if depth == 0 {
		out["retweeted"] = parseRetweeted(m)
	} else {
		out["retweeted"] = nil
	}
	out["reposts_count"] = asInt64(m["reposts_count"])
	out["comments_count"] = asInt64(m["comments_count"])
	out["attitudes_count"] = asInt64(m["attitudes_count"])
	out["attitudes_status"] = asInt64(m["attitudes_status"])
	out["favorited"] = asBool(m["favorited"])
	out["can_delete"] = canDeleteStatus(m, author)
	out["topic_ids"] = parseTopicIDs(m)
	return out
}

// parseRetweeted 解析转发原微博；只保留一层，第三层直接丢弃。
func parseRetweeted(mblog map[string]any) map[string]any {
	m := normalizeStatusMap(mblog)
	if m == nil {
		return nil
	}
	inner := asMap(m["retweeted_status"])
	if inner == nil {
		return nil
	}
	return parseBlogDepth(inner, 1)
}

// parseBlogList 把原始微博 map 列表归一化成 []any。
func parseBlogList(raws []map[string]any) []any {
	out := make([]any, 0, len(raws))
	for _, raw := range raws {
		if item := parseBlog(raw); item != nil {
			out = append(out, item)
		}
	}
	return out
}

// ======================= CommentItem =======================

// replyToName 取被回复者昵称。
func replyToName(comment map[string]any) string {
	reply := asMap(comment["reply_comment"])
	if reply == nil {
		return ""
	}
	user := asMap(reply["user"])
	if user == nil {
		return ""
	}
	return firstNonEmpty(asString(user["screen_name"]), asString(user["name"]))
}

// canDeleteComment 判断当前登录用户能否删除这条评论。
func canDeleteComment(user map[string]any) bool {
	if user == nil {
		return false
	}
	return isMeUID(asInt64(user["id"]))
}

// parseComment 归一化一条评论（SPEC 3.3）。
func parseComment(c map[string]any) map[string]any {
	comment := normalizeCommentMap(c)
	if comment == nil {
		return nil
	}
	out := map[string]any{}
	out["id"] = firstNonEmpty(asString(comment["idstr"]), asString(comment["id"]))
	rawHTML := firstNonEmpty(asString(comment["text"]), asString(comment["text_raw"]))
	plain := trimHTML(rawHTML)
	out["text"] = plain
	out["text_raw"] = plain
	out["text_html"] = sanitizeWeiboHTML(rawHTML)
	createdRaw := strings.TrimSpace(asString(comment["created_at"]))
	createdTS := parseWeiboTimeToUnix(createdRaw)
	out["created_ts"] = createdTS
	if createdTS > 0 {
		out["created_at"] = formatTimeAbsolute(createdTS)
		out["created_text"] = formatTimeText(createdTS)
	} else {
		out["created_at"] = createdRaw
		out["created_text"] = createdRaw
	}
	out["like_count"] = asInt64(comment["like_count"])
	out["liked"] = asBool(comment["liked"])
	replyCount := asInt64(comment["reply_count"])
	totalNumber := asInt64(comment["total_number"])
	if replyCount == 0 {
		replyCount = totalNumber
	}
	out["reply_count"] = replyCount
	out["total_number"] = totalNumber
	user := parseUser(asMap(comment["user"]))
	if user == nil {
		user = emptyUser()
	}
	out["user"] = user
	out["reply_to"] = replyToName(comment)
	out["pics"] = parseCommentPics(comment)
	out["can_delete"] = canDeleteComment(user)
	return out
}

// parseCommentItems 把原始评论 map 列表归一化成 []any。
func parseCommentItems(raws []map[string]any) []any {
	out := make([]any, 0, len(raws))
	for _, raw := range raws {
		if item := parseComment(raw); item != nil {
			out = append(out, item)
		}
	}
	return out
}

// ======================= 转发列表 / 微博列表容器 =======================

// parseStatusesFromContainer 从 getIndex / repostTimeline 等响应里取出微博列表。
//
// 依次尝试：data.cards（含 card_group 递归）、data.list、data.statuses、data.data。
func parseStatusesFromContainer(root map[string]any) []map[string]any {
	if root == nil {
		return []map[string]any{}
	}
	// 上游直接返回数组时 extractJSONObject 会包成 {"data":[...]}
	if list := asSlice(root["data"]); len(list) > 0 {
		return dedupeStatusMaps(normalizeStatusMaps(list))
	}
	data := asMap(root["data"])
	if data == nil {
		data = root
	}
	out := []map[string]any{}
	if cards := asSlice(data["cards"]); len(cards) > 0 {
		out = append(out, flattenCards(cards)...)
	}
	if len(out) == 0 {
		for _, key := range []string{"list", "statuses", "data", "reposts", "mblogs"} {
			if list := asSlice(data[key]); len(list) > 0 {
				out = append(out, normalizeStatusMaps(list)...)
			}
		}
	}
	if len(out) == 0 {
		if cards := asSlice(root["cards"]); len(cards) > 0 {
			out = append(out, flattenCards(cards)...)
		}
	}
	return dedupeStatusMaps(out)
}

// flattenCards 递归展开 card_group，取出所有 mblog（按 id 去重并保持顺序）。
func flattenCards(cards []any) []map[string]any {
	collected := []map[string]any{}
	var walk func(list []any, depth int)
	walk = func(list []any, depth int) {
		if depth > 8 {
			return
		}
		for _, item := range list {
			card := asMap(item)
			if card == nil {
				continue
			}
			if group := asSlice(card["card_group"]); len(group) > 0 {
				walk(group, depth+1)
			}
			if mblog := asMap(card["mblog"]); mblog != nil {
				collected = append(collected, mblog)
				continue
			}
			if asInt64(card["card_type"]) == 9 && looksLikeStatus(card) {
				collected = append(collected, card)
			}
		}
	}
	walk(cards, 0)
	return dedupeStatusMaps(collected)
}

// normalizeStatusMaps 把任意 map 列表统一成「微博对象」列表。
func normalizeStatusMaps(list []any) []map[string]any {
	out := make([]map[string]any, 0, len(list))
	for _, item := range list {
		m := asMap(item)
		if m == nil {
			continue
		}
		if inner := asMap(m["mblog"]); inner != nil {
			out = append(out, inner)
			continue
		}
		if inner := asMap(m["status"]); inner != nil {
			out = append(out, inner)
			continue
		}
		if looksLikeStatus(m) {
			out = append(out, m)
		}
	}
	return out
}

// dedupeStatusMaps 按 id 去重（保持顺序）。
func dedupeStatusMaps(list []map[string]any) []map[string]any {
	seen := map[string]bool{}
	out := make([]map[string]any, 0, len(list))
	for _, m := range list {
		if m == nil {
			continue
		}
		id := firstNonEmpty(asString(m["id"]), asString(m["idstr"]))
		if id != "" {
			if seen[id] {
				continue
			}
			seen[id] = true
		}
		out = append(out, m)
	}
	return out
}

// containerSinceID 读取容器响应的 since_id（数字或字符串）。
func containerSinceID(root map[string]any) string {
	if root == nil {
		return ""
	}
	data := asMap(root["data"])
	if data == nil {
		data = root
	}
	return strings.TrimSpace(asString(data["since_id"]))
}

// ======================= 评论（hotflow） =======================

// parseCommentsFromHotflow 解析评论列表。
//
// 返回：原始评论 map 列表、max_id、max_id_type、是否还有下一页。
func parseCommentsFromHotflow(root map[string]any) ([]map[string]any, int64, int, bool) {
	if root == nil {
		return []map[string]any{}, 0, 0, false
	}
	data := asMap(root["data"])
	if data == nil {
		data = root
	}
	list := asSlice(data["data"])
	if len(list) == 0 {
		list = asSlice(data["comments"])
	}
	if len(list) == 0 {
		list = asSlice(data["list"])
	}
	out := make([]map[string]any, 0, len(list))
	for _, item := range list {
		m := asMap(item)
		if m == nil {
			continue
		}
		if inner := asMap(m["comment"]); inner != nil {
			m = inner
		}
		out = append(out, m)
	}
	maxID := asInt64(data["max_id"])
	maxIDType := int(asInt64(data["max_id_type"]))
	hasMore := maxID > 0
	if _, ok := data["max_id"]; !ok {
		hasMore = len(out) > 0
	}
	return out, maxID, maxIDType, hasMore
}

// ======================= 用户列表 =======================

// parseUserListFromRoot 从 data.data / data.users / data.list 里取用户列表。
func parseUserListFromRoot(root map[string]any) []map[string]any {
	if root == nil {
		return []map[string]any{}
	}
	data := asMap(root["data"])
	if data == nil {
		data = root
	}
	lists := [][]any{
		asSlice(data["data"]),
		asSlice(data["users"]),
		asSlice(data["list"]),
		asSlice(root["users"]),
	}
	out := []map[string]any{}
	for _, list := range lists {
		if len(list) == 0 {
			continue
		}
		for _, item := range list {
			m := asMap(item)
			if m == nil {
				continue
			}
			if user := asMap(m["user"]); user != nil {
				out = append(out, user)
				continue
			}
			out = append(out, m)
		}
		if len(out) > 0 {
			break
		}
	}
	return out
}

// parseUserCards 从 getIndex 的卡片（card_group[*].user）里取用户列表。
func parseUserCards(root map[string]any) []map[string]any {
	if root == nil {
		return []map[string]any{}
	}
	data := asMap(root["data"])
	if data == nil {
		data = root
	}
	out := []map[string]any{}
	seen := map[string]bool{}
	var walk func(list []any, depth int)
	walk = func(list []any, depth int) {
		if depth > 8 {
			return
		}
		for _, item := range list {
			card := asMap(item)
			if card == nil {
				continue
			}
			if group := asSlice(card["card_group"]); len(group) > 0 {
				walk(group, depth+1)
			}
			user := asMap(card["user"])
			if user == nil {
				continue
			}
			key := firstNonEmpty(asString(user["id"]), asString(user["idstr"]), asString(user["screen_name"]))
			if key != "" {
				if seen[key] {
					continue
				}
				seen[key] = true
			}
			out = append(out, user)
		}
	}
	walk(asSlice(data["cards"]), 0)
	if len(out) == 0 {
		return parseUserListFromRoot(root)
	}
	return out
}

// collectGroupMaps 遍历响应，找出所有形如 {gid, title} 的分组对象。
func collectGroupMaps(root map[string]any) []map[string]any {
	out := []map[string]any{}
	seen := map[string]bool{}
	walkMaps(root, 0, func(m map[string]any) {
		gid := firstNonEmpty(asString(m["gid"]), asString(m["group_id"]))
		if gid == "" || seen[gid] {
			return
		}
		seen[gid] = true
		group := map[string]any{}
		group["gid"] = gid
		group["title"] = firstNonEmpty(asString(m["title"]), asString(m["name"]), asString(m["group_name"]))
		out = append(out, group)
	})
	return out
}

// extractUserMap 从资料响应里取出用户对象。
func extractUserMap(root map[string]any) map[string]any {
	if root == nil {
		return nil
	}
	if data := asMap(root["data"]); data != nil {
		if user := asMap(data["user"]); user != nil {
			return user
		}
		if user := asMap(data["userInfo"]); user != nil {
			return user
		}
		if data["id"] != nil && data["screen_name"] != nil {
			return data
		}
	}
	if user := asMap(root["user"]); user != nil {
		return user
	}
	if root["id"] != nil && root["screen_name"] != nil {
		return root
	}
	return nil
}

// ======================= 热搜 / 话题 =======================

// hotSearchURL 生成热搜词对应的搜索页地址。
func hotSearchURL(word string) string {
	w := strings.TrimSpace(word)
	if w == "" {
		return "https://s.weibo.com/weibo"
	}
	return "https://s.weibo.com/weibo?q=" + url.QueryEscape(w)
}

// parseHotItem 归一化一条热搜（SPEC 3.4）。
func parseHotItem(rank int, m map[string]any) map[string]any {
	if m == nil {
		return nil
	}
	word := firstNonEmpty(
		asString(m["word"]),
		asString(m["note"]),
		asString(m["word_scheme"]),
		asString(m["name"]),
		asString(m["title"]),
	)
	word = strings.TrimSpace(strings.Trim(word, "#"))
	if word == "" {
		return nil
	}
	rawHot := parseLooseCount(m["raw_hot"])
	if rawHot == 0 {
		rawHot = parseLooseCount(m["num"])
	}
	if rawHot == 0 {
		rawHot = parseLooseCount(m["fun_word"])
	}
	label := firstNonEmpty(asString(m["label_name"]), asString(m["icon_desc"]), asString(m["label"]))
	out := map[string]any{}
	out["rank"] = rank
	out["word"] = word
	out["raw_hot"] = rawHot
	out["raw_hot_text"] = formatCountText(rawHot)
	out["label"] = label
	out["url"] = hotSearchURL(word)
	out["category"] = firstNonEmpty(asString(m["category"]), asString(m["flag_desc"]))
	if scheme := asString(m["word_scheme"]); scheme != "" {
		out["word_scheme"] = scheme
	}
	return out
}

// parseHotListFromRoot 从热搜榜响应里取出热搜条目。
func parseHotListFromRoot(root map[string]any) []map[string]any {
	if root == nil {
		return []map[string]any{}
	}
	data := asMap(root["data"])
	if data == nil {
		data = root
	}
	out := []map[string]any{}
	seen := map[string]bool{}
	appendItems := func(list []any) {
		for _, item := range list {
			m := asMap(item)
			if m == nil {
				continue
			}
			word := firstNonEmpty(asString(m["word"]), asString(m["note"]), asString(m["word_scheme"]))
			if word == "" {
				continue
			}
			key := strings.TrimSpace(strings.Trim(word, "#"))
			if seen[key] {
				continue
			}
			seen[key] = true
			out = append(out, m)
		}
	}
	for _, card := range asSlice(data["cards"]) {
		cardMap := asMap(card)
		if cardMap == nil {
			continue
		}
		appendItems(asSlice(cardMap["card_group"]))
	}
	appendItems(asSlice(data["realtime"]))
	if len(out) == 0 {
		walkMaps(root, 0, func(m map[string]any) {
			if asString(m["word"]) == "" && asString(m["note"]) == "" {
				return
			}
			if m["raw_hot"] == nil && m["desc"] == nil && m["word_scheme"] == nil {
				return
			}
			appendItems([]any{m})
		})
	}
	return out
}

// normalizeTopicMaps 把任意 map 列表统一成「话题对象」列表。
func normalizeTopicMaps(list []any) []map[string]any {
	out := make([]map[string]any, 0, len(list))
	for _, item := range list {
		m := asMap(item)
		if m == nil {
			continue
		}
		topic := normalizeTopicMap(m)
		if topic == nil {
			continue
		}
		if !looksLikeTopic(topic) {
			continue
		}
		out = append(out, topic)
	}
	return out
}

// looksLikeTopic 粗略判断一个对象是不是话题 / 超话。
func looksLikeTopic(m map[string]any) bool {
	if m == nil {
		return false
	}
	if asString(m["container_id"]) != "" || asString(m["containerid"]) != "" {
		return true
	}
	if strings.Contains(asString(m["scheme"]), "containerid=100808") {
		return true
	}
	if strings.HasPrefix(asString(m["id"]), "100808") {
		return true
	}
	hasName := firstNonEmpty(asString(m["name"]), asString(m["title"]), asString(m["word"]), asString(m["topic_name"])) != ""
	if !hasName {
		return false
	}
	return m["desc"] != nil || m["desc1"] != nil || m["read_count"] != nil || m["discuss_count"] != nil || m["object_id"] != nil
}

// collectTopicCards 从 getIndex / ajax 响应里递归找出话题对象。
func collectTopicCards(root map[string]any) []map[string]any {
	if root == nil {
		return []map[string]any{}
	}
	out := []map[string]any{}
	seen := map[string]bool{}
	walkMaps(root, 0, func(m map[string]any) {
		topic := normalizeTopicMap(m)
		if !looksLikeTopic(topic) {
			return
		}
		key := firstNonEmpty(asString(topic["id"]), asString(topic["container_id"]), asString(topic["name"]), asString(topic["title"]))
		if key != "" {
			if seen[key] {
				return
			}
			seen[key] = true
		}
		out = append(out, topic)
	})
	return out
}

// parseTopicList 从 data.list / data.topics / 卡片里取出话题列表。
func parseTopicList(root map[string]any) []map[string]any {
	if root == nil {
		return []map[string]any{}
	}
	data := asMap(root["data"])
	if data == nil {
		data = root
	}
	for _, key := range []string{"list", "topics", "topic_list"} {
		if list := asSlice(data[key]); len(list) > 0 {
			if topics := normalizeTopicMaps(list); len(topics) > 0 {
				return topics
			}
		}
	}
	return collectTopicCards(root)
}

// parseTopic 归一化一个话题 / 超话（SPEC 3.4）。
func parseTopic(m map[string]any) map[string]any {
	topic := normalizeTopicMap(m)
	if topic == nil {
		return nil
	}
	rawID := firstNonEmpty(
		asString(topic["container_id"]),
		asString(topic["containerid"]),
		asString(topic["topic_id"]),
		asString(topic["id"]),
		asString(topic["object_id"]),
	)
	if rawID == "" {
		rawID = extractContainerIDFromScheme(asString(topic["scheme"]))
	}
	container := topicContainerID(rawID)
	id := container
	if id == "" {
		id = rawID
	}
	name := firstNonEmpty(
		asString(topic["name"]),
		asString(topic["title"]),
		asString(topic["word"]),
		asString(topic["topic_name"]),
	)
	if name == "" && container == "" {
		return nil
	}
	isSuper := asBool(topic["is_super"])
	if !isSuper && strings.HasPrefix(container, "100808") {
		isSuper = true
	}
	if topic["level"] != nil || topic["exp"] != nil || topic["signed_days"] != nil || topic["continue_sign_days"] != nil {
		isSuper = true
	}
	kind := "topic"
	if isSuper {
		kind = "super"
	}
	out := map[string]any{}
	out["id"] = id
	out["container_id"] = container
	out["name"] = name
	out["desc"] = firstNonEmpty(asString(topic["desc"]), asString(topic["description"]), asString(topic["desc1"]))
	out["cover"] = firstNonEmpty(asString(topic["cover"]), asString(topic["pic"]), asString(topic["cover_image"]), asString(topic["avatar"]))
	out["read_count"] = parseLooseCount(firstNonEmptyAny(topic["read_count"], topic["read"]))
	out["discuss_count"] = parseLooseCount(firstNonEmptyAny(topic["discuss_count"], topic["discuss"]))
	out["fans_count"] = parseLooseCount(firstNonEmptyAny(topic["fans_count"], topic["followers_count"]))
	out["is_super"] = isSuper
	out["level"] = asInt64(topic["level"])
	out["exp"] = asInt64(topic["exp"])
	out["rank"] = asInt64(topic["rank"])
	checked := asBool(topic["checked"])
	if !checked {
		checked = asBool(topic["is_signed"]) || asBool(topic["signed"])
	}
	out["checked"] = checked
	signedDays := asInt64(topic["signed_days"])
	if signedDays == 0 {
		signedDays = asInt64(topic["continue_sign_days"])
	}
	out["signed_days"] = signedDays
	out["type"] = kind
	return out
}

// firstNonEmptyAny 返回第一个非 nil 的值（用于 read_count / discuss_count 这类多字段兜底）。
func firstNonEmptyAny(values ...any) any {
	for _, v := range values {
		if v == nil {
			continue
		}
		if s := asString(v); strings.TrimSpace(s) != "" {
			return v
		}
	}
	return nil
}

// parseTopicItems 把原始话题 map 列表归一化成 []any。
func parseTopicItems(topics []map[string]any) []any {
	out := make([]any, 0, len(topics))
	for _, topic := range topics {
		if item := parseTopic(topic); item != nil {
			out = append(out, item)
		}
	}
	return out
}

// ======================= 上游写操作响应辅助 =======================

// extractCreatedID 从写操作响应里取出新对象的 id。
func extractCreatedID(root map[string]any) string {
	if root == nil {
		return ""
	}
	if data := asMap(root["data"]); data != nil {
		if id := firstNonEmpty(asString(data["id"]), asString(data["idstr"]), asString(data["mid"])); id != "" {
			return id
		}
	}
	return firstNonEmpty(asString(root["id"]), asString(root["idstr"]), asString(root["mid"]))
}

// extractCreatedBID 从写操作响应里取出新微博的 bid。
func extractCreatedBID(root map[string]any) string {
	if root == nil {
		return ""
	}
	if data := asMap(root["data"]); data != nil {
		if bid := firstNonEmpty(asString(data["bid"]), asString(data["mblogid"])); bid != "" {
			return bid
		}
	}
	return firstNonEmpty(asString(root["bid"]), asString(root["mblogid"]))
}

// ensureUpstreamOK 检查 m.weibo.cn 风格的 {"ok":1} 响应。
func ensureUpstreamOK(root map[string]any, fallback string) error {
	if root == nil {
		return nil
	}
	raw, exists := root["ok"]
	if !exists || raw == nil {
		return nil
	}
	if asBool(raw) {
		return nil
	}
	message := firstNonEmpty(asString(root["msg"]), asString(root["message"]), asString(root["error"]))
	if message == "" {
		message = fallback
	}
	if isLoginErrorText(message) {
		return errLoginExpired()
	}
	return &appError{Code: -2, Status: 200, Message: message}
}

// ensureWebOK 检查 weibo.com 风格的 {"code":"100000"} 响应（也兼容 {"ok":1}）。
func ensureWebOK(root map[string]any, fallback string) error {
	if root == nil {
		return nil
	}
	if raw, exists := root["code"]; exists && raw != nil {
		code := asString(raw)
		if code != "" && code != "100000" && code != "0" {
			message := firstNonEmpty(asString(root["msg"]), asString(root["message"]), asString(root["error"]))
			if message == "" {
				message = fallback + "（上游返回 code=" + code + "）"
			}
			if isLoginErrorText(message) {
				return errLoginExpired()
			}
			return &appError{Code: -2, Status: 200, Message: message}
		}
	}
	if err := ensureUpstreamOK(root, fallback); err != nil {
		return err
	}
	return nil
}

// extractPicID 从 picupload 的响应里取出 pic_id。
func extractPicID(raw json.RawMessage) string {
	root := decodeJSONMap(raw)
	if len(root) == 0 {
		return ""
	}
	found := ""
	walkMaps(root, 0, func(m map[string]any) {
		if found != "" {
			return
		}
		for _, key := range []string{"pid", "pic_id", "picId", "picid"} {
			if value := asString(m[key]); value != "" {
				found = value
				return
			}
		}
	})
	return found
}
