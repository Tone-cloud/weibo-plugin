package main

import "strings"

// ============================================================================
// 全部上游 URL 常量（SPEC 第 5 节）。
//
// 上游是私有 Web API，字段/路径随时可能变化；改地址只需要动这个文件。
// 这里把「移动端 m.weibo.cn」与「网页端 weibo.com」分开命名，便于排查。
//
// 刻意使用独立的 const 声明（而不是 const 块），这样增删一行不会影响其它行。
// ============================================================================

// ---- m.weibo.cn：信息流、微博、评论、搜索、关注 ----

// epContainerIndex 容器统一入口（信息流 / 搜索 / 用户微博 / 超话流）。
const epContainerIndex = "https://m.weibo.cn/api/container/getIndex"

// epStatusShow 微博详情。
const epStatusShow = "https://m.weibo.cn/statuses/show"

// epCommentsHotflow 热门评论。
const epCommentsHotflow = "https://m.weibo.cn/comments/hotflow"

// epCommentsHotChild 子评论。
const epCommentsHotChild = "https://m.weibo.cn/comments/hotFlowChild"

// epRepostTimeline 转发列表。
const epRepostTimeline = "https://m.weibo.cn/api/statuses/repostTimeline"

// epAttitudesList 点赞列表。
const epAttitudesList = "https://m.weibo.cn/api/attitudes/attitudesList"

// epStatusRepost 转发。
const epStatusRepost = "https://m.weibo.cn/api/statuses/repost"

// epCommentCreate 发评论。
const epCommentCreate = "https://m.weibo.cn/api/comments/create"

// epCommentReply 回复评论。
const epCommentReply = "https://m.weibo.cn/api/comments/reply"

// epCommentDestroy 删评论。
const epCommentDestroy = "https://m.weibo.cn/api/comments/destroy"

// epStatusLike 点赞。
const epStatusLike = "https://m.weibo.cn/api/statuses/like"

// epStatusUnlike 取消赞。
const epStatusUnlike = "https://m.weibo.cn/api/statuses/unlike"

// epCommentLike 评论点赞。
const epCommentLike = "https://m.weibo.cn/api/comments/like"

// epCommentUnlike 评论取消赞。
const epCommentUnlike = "https://m.weibo.cn/api/comments/unlike"

// epFriendshipCreate 关注。
const epFriendshipCreate = "https://m.weibo.cn/api/friendships/create"

// epFriendshipDestroy 取关。
const epFriendshipDestroy = "https://m.weibo.cn/api/friendships/destroy"

// epMobileConfig 登录态检查（/api/config）。
const epMobileConfig = "https://m.weibo.cn/api/config"

// ---- weibo.com：资料、发布、收藏、超话 ----
//
// 注意：SPEC 第 5 节只列了发微博 / 用户资料 / 我的超话 / 超话签到；
// 路由表里还有收藏、@我的、删微博、收藏开关，§5 未列，这里补网页端 ajax 接口。
// 若上游调整，只需改下面这几行。

// epProfileInfo 用户资料。
const epProfileInfo = "https://weibo.com/ajax/profile/info"

// epStatusUpdate 发微博。
const epStatusUpdate = "https://weibo.com/ajax/statuses/update"

// epStatusDestroy 删微博。
const epStatusDestroy = "https://weibo.com/ajax/statuses/destroy"

// epStatusFavorite 收藏微博。
const epStatusFavorite = "https://weibo.com/ajax/statuses/favorite"

// epStatusUnfavorite 取消收藏。
const epStatusUnfavorite = "https://weibo.com/ajax/statuses/unfavorite"

// epFavoritesAllFav 我的收藏列表。
const epFavoritesAllFav = "https://weibo.com/ajax/favorites/all_fav"

// epMentionsTimeline @我的。
const epMentionsTimeline = "https://weibo.com/ajax/statuses/mentions"

// epSuperTopicList 我的超话。
const epSuperTopicList = "https://weibo.com/ajax/profile/topicList"

// epSuperCheckin 超话签到。
const epSuperCheckin = "https://weibo.com/aj/general/button"

// epWebLogout 退出登录。
const epWebLogout = "https://weibo.com/logout.php"

// epPicUpload 传图（data=base64 走 query，主体是 b64_data 表单字段）。
const epPicUpload = "https://picupload.weibo.com/interface/pic_upload.php"

// ---- 容器 ID 常量 ----

// containerIDHome 首页推荐流。
const containerIDHome = "102803"

// containerIDFollow 关注流。
const containerIDFollow = "102803_ctg1_4288_-_ctg1_4288"

// containerIDSelfFollowed 我的关注列表。
const containerIDSelfFollowed = "231093_-_selffollowed"

// containerIDFollowers 我的粉丝列表。
const containerIDFollowers = "231093_-_followers"

// containerIDHotEncoded 是 SPEC 第 5 节原样给出的热搜 containerid（已 URL 编码）。
const containerIDHotEncoded = "106003type%3D25%26t%3D3%26disable_hot%3D1%26filter_type%3Drealtimehot"

// containerIDHot 与上面等价但未编码 —— url.Values 会再编码一次，
// 实际请求必须用这个版本，否则 %3D 会变成 %253D。
const containerIDHot = "106003type=25&t=3&disable_hot=1&filter_type=realtimehot"

// checkinAPIPath 是超话签到接口的 api 参数值（作为 query 参数整体传递）。
const checkinAPIPath = "http://i.huati.weibo.com/aj/super/checkin"

// searchKindStatus 综合搜索。
const searchKindStatus = "1"

// searchKindUser 用户搜索。
const searchKindUser = "3"

// searchKindTopic 超话搜索。
const searchKindTopic = "60"

// searchContainerID 拼出搜索用的 containerid（形如 100103type=1&q=关键词）。
// kind 取 searchKindStatus / searchKindUser / searchKindTopic。
func searchContainerID(kind, q string) string {
	k := strings.TrimSpace(kind)
	if k == "" {
		k = searchKindStatus
	}
	return "100103type=" + k + "&q=" + strings.TrimSpace(q)
}

// hotSearchContainerID 热搜微博流的 containerid。
func hotSearchContainerID(word string) string {
	return searchContainerID(searchKindStatus, strings.TrimSpace(word))
}

// profileContainerID 用户微博流的 containerid（107603 + uid）。
func profileContainerID(uid string) string {
	u := strings.TrimSpace(uid)
	if u == "" {
		return ""
	}
	if strings.HasPrefix(u, "107603") {
		return u
	}
	return "107603" + u
}

// profileInfoContainerID 个人主页的 containerid（100505 + uid），用于资料回落查询。
func profileInfoContainerID(uid string) string {
	u := strings.TrimSpace(uid)
	if u == "" {
		return ""
	}
	if strings.HasPrefix(u, "100505") {
		return u
	}
	return "100505" + u
}

// topicContainerID 话题 / 超话的 containerid（100808 + topic_id）。
func topicContainerID(topicID string) string {
	id := strings.TrimSpace(topicID)
	if id == "" {
		return ""
	}
	if strings.HasPrefix(id, "100808") {
		return id
	}
	return "100808" + id
}

// topicIDFromContainer 反向取出纯 topic_id（去掉 100808 前缀）。
func topicIDFromContainer(containerID string) string {
	c := strings.TrimSpace(containerID)
	if c == "" {
		return ""
	}
	if strings.HasPrefix(c, "100808") {
		return strings.TrimPrefix(c, "100808")
	}
	return c
}

// groupContainerID 关注分组流：gid=4288 → 102803_ctg1_4288_-_ctg1_4288。
// 如果调用方直接给了完整 containerid 则原样返回。
func groupContainerID(gid string) string {
	g := strings.TrimSpace(gid)
	if g == "" {
		return ""
	}
	if strings.Contains(g, "_-_") || strings.HasPrefix(g, "102803") || strings.HasPrefix(g, "100505") {
		return g
	}
	return "102803_ctg1_" + g + "_-_ctg1_" + g
}
