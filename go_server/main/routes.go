package main

import "net/http"

// setupRoutes 注册 SPEC 第 4 节的全部路由。
//
// 约定：GET 走 query 参数，写操作走 POST（JSON 或 form-urlencoded 都接受）。
// 路径与 config.go 的 rootEndpoints 必须保持一致。
func setupRoutes(mux *http.ServeMux) {
	// ---- 元信息 ----
	mux.HandleFunc("/", handleRoot)
	mux.HandleFunc("/server/ping", handlePing)
	mux.HandleFunc("/config", handleConfig)

	// ---- 信息流 ----
	mux.HandleFunc("/feed/home", handleFeedHome)
	mux.HandleFunc("/feed/follow", handleFeedFollow)
	mux.HandleFunc("/feed/group", handleFeedGroup)
	mux.HandleFunc("/feed/hot", handleFeedHot)
	mux.HandleFunc("/feed/hot/status", handleFeedHotStatus)

	// ---- 搜索 ----
	mux.HandleFunc("/search/status", handleSearchStatus)
	mux.HandleFunc("/search/user", handleSearchUser)
	mux.HandleFunc("/search/topic", handleSearchTopic)
	mux.HandleFunc("/search/history", handleSearchHistory)
	mux.HandleFunc("/search/history/clear", handleSearchHistoryClear)

	// ---- 微博 ----
	mux.HandleFunc("/status/detail", handleStatusDetail)
	mux.HandleFunc("/status/comments", handleStatusComments)
	mux.HandleFunc("/status/comments/replies", handleStatusCommentReplies)
	mux.HandleFunc("/status/reposts", handleStatusReposts)
	mux.HandleFunc("/status/likers", handleStatusLikers)
	mux.HandleFunc("/status/repost", handleStatusRepost)
	mux.HandleFunc("/status/comment", handleStatusComment)
	mux.HandleFunc("/status/comment/delete", handleStatusCommentDelete)
	mux.HandleFunc("/status/comment/like", handleStatusCommentLike)
	mux.HandleFunc("/status/like", handleStatusLike)
	mux.HandleFunc("/status/favorite", handleStatusFavorite)
	mux.HandleFunc("/status/delete", handleStatusDelete)
	mux.HandleFunc("/status/publish", handleStatusPublish)
	mux.HandleFunc("/status/upload_pic", handleStatusUploadPic)
	mux.HandleFunc("/status/favorites", handleStatusFavorites)
	mux.HandleFunc("/status/mentions", handleStatusMentions)

	// ---- 用户 ----
	mux.HandleFunc("/user/profile", handleUserProfile)
	mux.HandleFunc("/user/me", handleUserMe)
	mux.HandleFunc("/user/statuses", handleUserStatuses)
	mux.HandleFunc("/user/following", handleUserFollowing)
	mux.HandleFunc("/user/followers", handleUserFollowers)
	mux.HandleFunc("/user/search", handleUserSearch)
	mux.HandleFunc("/user/follow", handleUserFollow)
	mux.HandleFunc("/user/groups", handleUserGroups)
	mux.HandleFunc("/user/topics", handleUserTopics)

	// ---- 话题 / 超话 ----
	mux.HandleFunc("/topic/detail", handleTopicDetail)
	mux.HandleFunc("/topic/statuses", handleTopicStatuses)
	mux.HandleFunc("/topic/checkin", handleTopicCheckin)
	mux.HandleFunc("/topic/checkin/all", handleTopicCheckinAll)
	mux.HandleFunc("/topic/search", handleTopicSearch)

	// ---- 媒体 ----
	mux.HandleFunc("/media/info", handleMediaInfo)

	// ---- 登录 ----
	mux.HandleFunc("/login/import", handleLoginImport)
	mux.HandleFunc("/login/info", handleLoginInfo)
	mux.HandleFunc("/logout", handleLogout)
}
