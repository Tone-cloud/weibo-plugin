package main

import (
	"net/url"
	"strconv"
	"strings"
)

// ============================================================================
// 评论：列表 / 子评论 / 发表 / 回复 / 删除 / 点赞
// ============================================================================

// fetchComments 热门评论列表（/comments/hotflow）。
// maxID / maxIDType 来自上一页响应，首页传 0。
func fetchComments(id string, page int, maxID int64, maxIDType int) (map[string]any, error) {
	statusID := strings.TrimSpace(id)
	if statusID == "" {
		return nil, errBadRequest("缺少微博 id")
	}
	if page < 1 {
		page = 1
	}
	params := url.Values{}
	params.Set("id", statusID)
	params.Set("mid", statusID)
	params.Set("max_id_type", strconv.Itoa(maxIDType))
	if maxID > 0 {
		params.Set("max_id", strconv.FormatInt(maxID, 10))
	}
	if page > 1 {
		params.Set("page", strconv.Itoa(page))
	}
	raw, err := getClient().mGet(epCommentsHotflow, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	raws, newMaxID, newMaxIDType, hasMore := parseCommentsFromHotflow(root)
	total := int64(0)
	if data := asMap(root["data"]); data != nil {
		total = asInt64(data["total_number"])
	}
	out := map[string]any{}
	out["items"] = parseCommentItems(raws)
	out["total"] = total
	out["max_id"] = newMaxID
	out["max_id_type"] = newMaxIDType
	out["has_more"] = hasMore
	return out, nil
}

// fetchCommentReplies 子评论列表（/comments/hotFlowChild）。
func fetchCommentReplies(id, cid string, page int) (map[string]any, error) {
	commentID := strings.TrimSpace(cid)
	if commentID == "" {
		return nil, errBadRequest("缺少评论 id")
	}
	if page < 1 {
		page = 1
	}
	params := url.Values{}
	params.Set("cid", commentID)
	params.Set("max_id", "0")
	params.Set("max_id_type", "0")
	if statusID := strings.TrimSpace(id); statusID != "" {
		params.Set("id", statusID)
	}
	if page > 1 {
		params.Set("page", strconv.Itoa(page))
	}
	raw, err := getClient().mGet(epCommentsHotChild, params)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	raws, _, _, hasMore := parseCommentsFromHotflow(root)
	total := int64(0)
	if data := asMap(root["data"]); data != nil {
		total = asInt64(data["total_number"])
	}
	out := map[string]any{}
	out["items"] = parseCommentItems(raws)
	out["total"] = total
	out["has_more"] = hasMore
	return out, nil
}

// postComment 发表评论；cid 非空时走「回复评论」接口。
func postComment(id, content, cid string, alsoRepost bool) (map[string]any, error) {
	statusID := strings.TrimSpace(id)
	if statusID == "" {
		return nil, errBadRequest("缺少微博 id")
	}
	text := strings.TrimSpace(content)
	if text == "" {
		return nil, errBadRequest("评论内容不能为空")
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	form := url.Values{}
	form.Set("id", statusID)
	form.Set("content", text)
	form.Set("mid", statusID)
	if alsoRepost {
		form.Set("also_repost", "1")
	} else {
		form.Set("also_repost", "0")
	}
	target := epCommentCreate
	commentID := strings.TrimSpace(cid)
	if commentID != "" {
		target = epCommentReply
		form.Set("cid", commentID)
		form.Set("reply", commentID)
	}
	raw, err := client.mPost(target, form)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	if err := ensureUpstreamOK(root, "评论失败"); err != nil {
		return nil, err
	}
	out := map[string]any{}
	out["ok"] = true
	out["id"] = extractCreatedID(root)
	return out, nil
}

// deleteComment 删除自己的评论。
func deleteComment(cid string) (map[string]any, error) {
	commentID := strings.TrimSpace(cid)
	if commentID == "" {
		return nil, errBadRequest("缺少评论 id")
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	form := url.Values{}
	form.Set("cid", commentID)
	raw, err := client.mPost(epCommentDestroy, form)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	if err := ensureUpstreamOK(root, "删除评论失败"); err != nil {
		return nil, err
	}
	out := map[string]any{}
	out["ok"] = true
	return out, nil
}

// likeComment 评论点赞 / 取消赞。
func likeComment(cid string, liked bool) (map[string]any, error) {
	commentID := strings.TrimSpace(cid)
	if commentID == "" {
		return nil, errBadRequest("缺少评论 id")
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	target := epCommentUnlike
	if liked {
		target = epCommentLike
	}
	form := url.Values{}
	form.Set("cid", commentID)
	if liked {
		form.Set("like", "1")
	} else {
		form.Set("like", "0")
	}
	raw, err := client.mPost(target, form)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	if err := ensureUpstreamOK(root, "评论点赞失败"); err != nil {
		return nil, err
	}
	out := map[string]any{}
	out["ok"] = true
	out["liked"] = liked
	return out, nil
}
