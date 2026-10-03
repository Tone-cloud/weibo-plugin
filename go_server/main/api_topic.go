package main

import (
	"net/url"
	"strings"
)

// ============================================================================
// 超话 / 话题：详情 / 微博流 / 签到 / 一键签到 / 搜索
// ============================================================================

// extractTopicDetail 从话题容器响应里取出话题对象。
func extractTopicDetail(root map[string]any, container string) map[string]any {
	candidates := []map[string]any{}
	if data := asMap(root["data"]); data != nil {
		for _, key := range []string{"topic", "topicInfo", "topic_info", "pageInfo", "card", "card_group"} {
			switch typed := data[key].(type) {
			case map[string]any:
				candidates = append(candidates, typed)
			case []any:
				for _, item := range typed {
					if m := asMap(item); m != nil {
						candidates = append(candidates, m)
					}
				}
			default:
				continue
			}
		}
		if cards := asSlice(data["cards"]); len(cards) > 0 {
			if first := asMap(cards[0]); first != nil {
				if group := asSlice(first["card_group"]); len(group) > 0 {
					if card := asMap(group[0]); card != nil {
						candidates = append(candidates, card)
					}
				}
			}
		}
	}
	for _, candidate := range candidates {
		topic := parseTopic(candidate)
		if topic == nil {
			continue
		}
		if asString(topic["name"]) == "" {
			continue
		}
		if asString(topic["container_id"]) == "" {
			topic["container_id"] = container
		}
		if asString(topic["id"]) == "" {
			topic["id"] = container
		}
		return topic
	}
	// 兜底：上游没给结构化话题信息时，至少返回 container_id，页面还能显示标题。
	fallback := map[string]any{}
	fallback["id"] = container
	fallback["container_id"] = container
	fallback["name"] = ""
	fallback["desc"] = ""
	fallback["cover"] = ""
	fallback["read_count"] = int64(0)
	fallback["discuss_count"] = int64(0)
	fallback["fans_count"] = int64(0)
	fallback["is_super"] = strings.HasPrefix(container, "100808")
	fallback["level"] = int64(0)
	fallback["exp"] = int64(0)
	fallback["rank"] = int64(0)
	fallback["checked"] = false
	fallback["signed_days"] = int64(0)
	fallback["type"] = "super"
	return fallback
}

// fetchTopicDetail 话题 / 超话详情。
func fetchTopicDetail(containerID string) (map[string]any, error) {
	container := topicContainerID(containerID)
	if container == "" {
		return nil, errBadRequest("缺少话题 container_id")
	}
	params := url.Values{}
	params.Set("containerid", container)
	raw, err := getClient().mGet(epContainerIndex, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	out := map[string]any{}
	out["topic"] = extractTopicDetail(root, container)
	return out, nil
}

// fetchTopicStatuses 话题 / 超话微博流。
func fetchTopicStatuses(containerID, sinceID string) (map[string]any, error) {
	container := topicContainerID(containerID)
	if container == "" {
		return nil, errBadRequest("缺少话题 container_id")
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
	root := decodeJSONMap(raw)
	raws := parseStatusesFromContainer(root)
	next := containerSinceID(root)
	out := map[string]any{}
	out["items"] = parseBlogList(raws)
	out["since_id"] = next
	out["has_more"] = len(raws) > 0 && next != "0"
	return out, nil
}

// checkinTopic 单个超话签到。
func checkinTopic(id, name string) (map[string]any, error) {
	topicID := topicIDFromContainer(id)
	if topicID == "" {
		return nil, errBadRequest("缺少超话 id")
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	params := url.Values{}
	params.Set("ajwvr", "6")
	params.Set("api", checkinAPIPath)
	params.Set("id", topicID)
	params.Set("status", "0")
	params.Set("pageid", topicContainerID(topicID))
	raw, err := client.webGet(epSuperCheckin, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	message := firstNonEmpty(asString(root["msg"]), asString(root["message"]), asString(root["error"]))
	code := asString(root["code"])
	failed := code != "" && code != "100000" && code != "0"
	if strings.Contains(message, "失败") || strings.Contains(message, "不存在") || strings.Contains(message, "未登录") {
		failed = true
	}
	signed := strings.Contains(message, "已签") || strings.Contains(message, "成功")
	ok := !failed
	if ok {
		signed = true
	}
	if message == "" {
		if ok {
			message = "签到成功"
		} else {
			message = "签到失败"
		}
	}
	expAdd := int64(0)
	if data := asMap(root["data"]); data != nil {
		expAdd = asInt64(data["exp_add"])
		if expAdd == 0 {
			expAdd = asInt64(data["exp"])
		}
	}
	if expAdd == 0 {
		expAdd = asInt64(root["exp_add"])
	}
	out := map[string]any{}
	out["ok"] = ok
	out["message"] = message
	out["exp_add"] = expAdd
	out["signed"] = signed
	out["id"] = topicID
	out["name"] = strings.TrimSpace(name)
	return out, nil
}

// checkinAllTopics 串行签到我关注的全部超话。
//
// 刻意保持串行且不 sleep：词典笔上的上游对并发很敏感，
// 而「一键签到」本身超话数量不多（通常 < 30）。
func checkinAllTopics() (map[string]any, error) {
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	result, err := fetchMyTopics(1)
	if err != nil {
		return nil, err
	}
	items := asSlice(result["items"])
	details := make([]any, 0, len(items))
	signed := 0
	failed := 0
	for _, item := range items {
		topic := asMap(item)
		if topic == nil {
			continue
		}
		id := topicIDFromContainer(firstNonEmpty(asString(topic["container_id"]), asString(topic["id"])))
		name := asString(topic["name"])
		if id == "" {
			continue
		}
		detail := map[string]any{}
		detail["id"] = id
		detail["name"] = name
		detail["ok"] = false
		detail["message"] = ""
		checkinResult, checkinErr := checkinTopic(id, name)
		if checkinErr != nil {
			detail["message"] = checkinErr.Error()
			failed++
		} else {
			success := asBool(checkinResult["ok"])
			detail["ok"] = success
			detail["message"] = asString(checkinResult["message"])
			if success {
				signed++
			} else {
				failed++
			}
		}
		details = append(details, detail)
	}
	out := map[string]any{}
	out["ok"] = failed == 0
	out["signed"] = signed
	out["failed"] = failed
	out["details"] = details
	return out, nil
}

// searchTopicsAPI 超话搜索（/topic/search 专用入口，直接复用综合搜索实现）。
func searchTopicsAPI(q string, page int) (map[string]any, error) {
	return searchTopics(q, page)
}
