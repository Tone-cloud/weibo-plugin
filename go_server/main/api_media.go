package main

import (
	"net/url"
	"strings"
)

// ============================================================================
// 媒体：解析微博里的视频 / 直播直链
// ============================================================================

// buildMediaQualities 从 page_info.media_info 里整理出可选清晰度列表。
func buildMediaQualities(status, page map[string]any) []any {
	out := []any{}
	appendQuality := func(label, target string) {
		link := strings.TrimSpace(target)
		if link == "" {
			return
		}
		for _, item := range out {
			existing := asMap(item)
			if existing == nil {
				continue
			}
			if asString(existing["url"]) == link {
				return
			}
		}
		quality := map[string]any{}
		quality["label"] = label
		quality["url"] = link
		out = append(out, quality)
	}
	pageInfo := asMap(status["page_info"])
	if pageInfo == nil {
		pageInfo = asMap(status["pageInfo"])
	}
	if media := asMap(pageInfo["media_info"]); media != nil {
		appendQuality("原画", asString(media["mp4_1080p_mp4"]))
		appendQuality("超清", asString(media["stream_url_hd"]))
		appendQuality("高清", asString(media["mp4_720p_mp4"]))
		appendQuality("高清", asString(media["mp4_hd_url"]))
		appendQuality("标清", asString(media["stream_url"]))
		appendQuality("标清", asString(media["mp4_sd_url"]))
		appendQuality("流畅", asString(media["h5_mp4_url"]))
	}
	if live := asMap(pageInfo["live_info"]); live != nil {
		appendQuality("直播", asString(live["stream_url"]))
		appendQuality("直播高清", asString(live["stream_url_hd"]))
		appendQuality("HLS", asString(live["hls_url"]))
	}
	appendQuality("默认", asString(page["media_url"]))
	appendQuality("原始", asString(page["url"]))
	return out
}

// fetchMediaInfo 解析一条微博的视频 / 直播直链。
func fetchMediaInfo(id string) (map[string]any, error) {
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
	status := normalizeStatusMap(root)
	if status == nil {
		return nil, &appError{Code: -3, Status: 200, Message: "未找到该微博"}
	}
	page := parsePageInfo(status)
	kind := asString(page["type"])
	if kind == "" || kind == "none" {
		return nil, &appError{Code: -3, Status: 200, Message: "该微博不包含视频或直播内容"}
	}
	primary := firstNonEmpty(asString(page["media_url"]), asString(page["url"]))
	out := map[string]any{}
	out["type"] = kind
	out["title"] = asString(page["title"])
	out["cover"] = asString(page["cover"])
	out["url"] = primary
	out["qualities"] = buildMediaQualities(status, page)
	return out, nil
}
