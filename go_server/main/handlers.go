package main

import (
	"bytes"
	"io"
	"net/http"
)

// ============================================================================
// 路由处理器：每个 SPEC 第 4 节的路由一个 handleXxx。
//
// 统一流程：
//   handleAPI 校验 HTTP 方法 → 解析并校验参数（失败 400 + 中文提示）
//   → 调用 api_* 函数 → 输出 { code:0, message:"", data:{...} }。
// ============================================================================

// handleRoot GET / —— 接口索引。
func handleRoot(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		out := map[string]any{}
		out["name"] = "WeiboPocket sidecar"
		out["version"] = appVersion
		out["logged_in"] = getClient().hasLogin()
		out["endpoints"] = toAnySlice(rootEndpoints)
		return out, nil
	})
}

// handlePing GET /server/ping —— sidecar 存活探测。
func handlePing(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		snapshot := getClient().snapshot()
		out := map[string]any{}
		out["ok"] = true
		out["version"] = appVersion
		out["logged_in"] = snapshot.LoggedIn
		return out, nil
	})
}

// handleConfig GET /config —— 登录态摘要。
func handleConfig(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return fetchConfig()
	})
}

// ======================= 信息流 =======================

// handleFeedHome GET /feed/home。
func handleFeedHome(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		sinceID := req.URL.Query().Get("since_id")
		freshType := getIntQuery(req, "fresh_type", 0)
		return fetchHome(sinceID, freshType)
	})
}

// handleFeedFollow GET /feed/follow。
func handleFeedFollow(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return fetchFollow(req.URL.Query().Get("since_id"))
	})
}

// handleFeedGroup GET /feed/group。
func handleFeedGroup(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		gid, err := requireQueryStr(req, "gid")
		if err != nil {
			return nil, err
		}
		return fetchGroup(gid, req.URL.Query().Get("since_id"))
	})
}

// handleFeedHot GET /feed/hot。
func handleFeedHot(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return fetchHotList()
	})
}

// handleFeedHotStatus GET /feed/hot/status。
func handleFeedHotStatus(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		word, err := requireQueryStr(req, "word")
		if err != nil {
			return nil, err
		}
		return fetchHotStatus(word, req.URL.Query().Get("since_id"))
	})
}

// ======================= 搜索 =======================

// handleSearchStatus GET /search/status。
func handleSearchStatus(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		keyword, err := requireQueryStr(req, "q")
		if err != nil {
			return nil, err
		}
		result, err := searchStatus(keyword, getIntQuery(req, "page", 1))
		if err != nil {
			return nil, err
		}
		if _, historyErr := appendSearchHistory(keyword); historyErr != nil {
			logWarn("写入搜索历史失败：%v", historyErr)
		}
		return result, nil
	})
}

// handleSearchUser GET /search/user。
func handleSearchUser(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		keyword, err := requireQueryStr(req, "q")
		if err != nil {
			return nil, err
		}
		result, err := searchUsers(keyword, getIntQuery(req, "page", 1))
		if err != nil {
			return nil, err
		}
		if _, historyErr := appendSearchHistory(keyword); historyErr != nil {
			logWarn("写入搜索历史失败：%v", historyErr)
		}
		return result, nil
	})
}

// handleSearchTopic GET /search/topic。
func handleSearchTopic(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		keyword, err := requireQueryStr(req, "q")
		if err != nil {
			return nil, err
		}
		result, err := searchTopics(keyword, getIntQuery(req, "page", 1))
		if err != nil {
			return nil, err
		}
		if _, historyErr := appendSearchHistory(keyword); historyErr != nil {
			logWarn("写入搜索历史失败：%v", historyErr)
		}
		return result, nil
	})
}

// handleSearchHistory GET /search/history。
func handleSearchHistory(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		words, err := loadSearchHistory()
		if err != nil {
			return nil, err
		}
		out := map[string]any{}
		out["items"] = toAnySlice(words)
		return out, nil
	})
}

// handleSearchHistoryClear POST /search/history/clear。
func handleSearchHistoryClear(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		if err := clearSearchHistory(); err != nil {
			return nil, err
		}
		out := map[string]any{}
		out["ok"] = true
		return out, nil
	})
}

// ======================= 微博 =======================

// handleStatusDetail GET /status/detail。
func handleStatusDetail(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		id, err := requireQueryStr(req, "id")
		if err != nil {
			return nil, err
		}
		return fetchStatusDetail(id)
	})
}

// handleStatusComments GET /status/comments。
func handleStatusComments(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		id, err := requireQueryStr(req, "id")
		if err != nil {
			return nil, err
		}
		page, maxID, maxIDType := getPageParams(req)
		return fetchComments(id, page, maxID, maxIDType)
	})
}

// handleStatusCommentReplies GET /status/comments/replies。
func handleStatusCommentReplies(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		id, err := requireQueryStr(req, "id")
		if err != nil {
			return nil, err
		}
		cid, err := requireQueryStr(req, "cid")
		if err != nil {
			return nil, err
		}
		return fetchCommentReplies(id, cid, getIntQuery(req, "page", 1))
	})
}

// handleStatusReposts GET /status/reposts。
func handleStatusReposts(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		id, err := requireQueryStr(req, "id")
		if err != nil {
			return nil, err
		}
		return fetchReposts(id, getIntQuery(req, "page", 1))
	})
}

// handleStatusLikers GET /status/likers。
func handleStatusLikers(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		id, err := requireQueryStr(req, "id")
		if err != nil {
			return nil, err
		}
		return fetchLikers(id, getIntQuery(req, "page", 1))
	})
}

// handleStatusRepost POST /status/repost。
func handleStatusRepost(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		id, err := requireStr(body, "id")
		if err != nil {
			return nil, err
		}
		return postRepost(id, getStr(body, "content"))
	})
}

// handleStatusComment POST /status/comment。
func handleStatusComment(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		id, err := requireStr(body, "id")
		if err != nil {
			return nil, err
		}
		content, err := requireStr(body, "content")
		if err != nil {
			return nil, err
		}
		cid := getStr(body, "cid")
		alsoRepost := boolParam(body, "also_repost", false)
		return postComment(id, content, cid, alsoRepost)
	})
}

// handleStatusCommentDelete POST /status/comment/delete。
func handleStatusCommentDelete(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		cid, err := requireStr(body, "cid")
		if err != nil {
			return nil, err
		}
		return deleteComment(cid)
	})
}

// handleStatusCommentLike POST /status/comment/like。
func handleStatusCommentLike(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		cid, err := requireStr(body, "cid")
		if err != nil {
			return nil, err
		}
		return likeComment(cid, boolParam(body, "liked", true))
	})
}

// handleStatusLike POST /status/like。
func handleStatusLike(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		id, err := requireStr(body, "id")
		if err != nil {
			return nil, err
		}
		return fetchLikesToggle(id, boolParam(body, "liked", true))
	})
}

// handleStatusFavorite POST /status/favorite。
func handleStatusFavorite(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		id, err := requireStr(body, "id")
		if err != nil {
			return nil, err
		}
		return fetchFavoriteToggle(id, boolParam(body, "fav", true))
	})
}

// handleStatusDelete POST /status/delete。
func handleStatusDelete(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		id, err := requireStr(body, "id")
		if err != nil {
			return nil, err
		}
		return fetchStatusDelete(id)
	})
}

// handleStatusPublish POST /status/publish。
func handleStatusPublish(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		content := getStr(body, "content")
		visible := intParam(body, "visible", 0)
		picIDs := strSliceParam(body, "pic_ids")
		return publishStatus(content, visible, picIDs)
	})
}

// handleStatusUploadPic POST /status/upload_pic。
func handleStatusUploadPic(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		data, err := requireStr(body, "data")
		if err != nil {
			return nil, err
		}
		return uploadPicture(data, getStr(body, "filename"))
	})
}

// handleStatusFavorites GET /status/favorites。
func handleStatusFavorites(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return fetchFavorites(getIntQuery(req, "page", 1))
	})
}

// handleStatusMentions GET /status/mentions。
func handleStatusMentions(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return fetchMentions(getIntQuery(req, "page", 1))
	})
}

// ======================= 用户 =======================

// handleUserProfile GET /user/profile。
func handleUserProfile(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		uid, err := requireQueryStr(req, "uid")
		if err != nil {
			return nil, err
		}
		return fetchUserProfile(uid)
	})
}

// handleUserMe GET /user/me。
func handleUserMe(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return fetchMe()
	})
}

// handleUserStatuses GET /user/statuses。
func handleUserStatuses(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		uid, err := requireQueryStr(req, "uid")
		if err != nil {
			return nil, err
		}
		page := getIntQuery(req, "page", 1)
		feature := getIntQuery(req, "feature", 0)
		return fetchUserStatuses(uid, page, feature)
	})
}

// handleUserFollowing GET /user/following。
func handleUserFollowing(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return fetchFollowing(req.URL.Query().Get("uid"), getIntQuery(req, "page", 1))
	})
}

// handleUserFollowers GET /user/followers。
func handleUserFollowers(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return fetchFollowers(req.URL.Query().Get("uid"), getIntQuery(req, "page", 1))
	})
}

// handleUserSearch GET /user/search。
func handleUserSearch(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		keyword, err := requireQueryStr(req, "q")
		if err != nil {
			return nil, err
		}
		result, err := searchUsers(keyword, getIntQuery(req, "page", 1))
		if err != nil {
			return nil, err
		}
		if _, historyErr := appendSearchHistory(keyword); historyErr != nil {
			logWarn("写入搜索历史失败：%v", historyErr)
		}
		return result, nil
	})
}

// handleUserFollow POST /user/follow。
func handleUserFollow(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		uid, err := requireStr(body, "uid")
		if err != nil {
			return nil, err
		}
		return followUser(uid, boolParam(body, "follow", true))
	})
}

// handleUserGroups GET /user/groups。
func handleUserGroups(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return fetchUserGroups()
	})
}

// handleUserTopics GET /user/topics。
func handleUserTopics(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return fetchMyTopics(getIntQuery(req, "page", 1))
	})
}

// ======================= 话题 / 超话 =======================

// handleTopicDetail GET /topic/detail。
func handleTopicDetail(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		container, err := requireQueryStr(req, "container_id")
		if err != nil {
			return nil, err
		}
		return fetchTopicDetail(container)
	})
}

// handleTopicStatuses GET /topic/statuses。
func handleTopicStatuses(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		container, err := requireQueryStr(req, "container_id")
		if err != nil {
			return nil, err
		}
		return fetchTopicStatuses(container, req.URL.Query().Get("since_id"))
	})
}

// handleTopicCheckin POST /topic/checkin。
func handleTopicCheckin(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		id, err := requireStr(body, "id")
		if err != nil {
			return nil, err
		}
		return checkinTopic(id, getStr(body, "name"))
	})
}

// handleTopicCheckinAll POST /topic/checkin/all。
func handleTopicCheckinAll(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		return checkinAllTopics()
	})
}

// handleTopicSearch GET /topic/search。
func handleTopicSearch(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		keyword, err := requireQueryStr(req, "q")
		if err != nil {
			return nil, err
		}
		result, err := searchTopicsAPI(keyword, getIntQuery(req, "page", 1))
		if err != nil {
			return nil, err
		}
		if _, historyErr := appendSearchHistory(keyword); historyErr != nil {
			logWarn("写入搜索历史失败：%v", historyErr)
		}
		return result, nil
	})
}

// ======================= 媒体 =======================

// handleMediaInfo GET /media/info。
func handleMediaInfo(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		id, err := requireQueryStr(req, "id")
		if err != nil {
			return nil, err
		}
		return fetchMediaInfo(id)
	})
}

// ======================= 登录 =======================

// handleLoginImport POST /login/import。
func handleLoginImport(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		body := bodyMap(req)
		cookie := getStr(body, "cookie")
		if cookie == "" {
			return nil, errBadRequest("缺少参数 cookie")
		}
		return importCookie(cookie)
	})
}

// handleLoginInfo GET /login/info。
func handleLoginInfo(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return checkLoginInfo()
	})
}

// handleLogout POST /logout。
func handleLogout(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		return logout()
	})
}

// handleConfigImport POST /config/import —— 电脑端把 cookies.json 直接发过来。
//
// 请求体支持四种写法（见 importCookiesJSON）：
//
//	{"cookies":[{...}]}  /  [{...}]  /  {"cookie":"SUB=..."}  /  "SUB=..."
//
// 导入后立即落盘到插件目录，设备侧无需任何操作即可自动生效。
func handleConfigImport(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodPost, func(req *http.Request) (map[string]any, error) {
		// 256 KiB 足够放几百条 Cookie，同时挡住异常大的请求体
		raw, err := io.ReadAll(io.LimitReader(req.Body, 256*1024))
		if err != nil {
			return nil, errBadRequest("读取请求体失败")
		}
		if len(bytes.TrimSpace(raw)) == 0 {
			return nil, errBadRequest("请求体为空")
		}
		return importCookiesJSON(raw)
	})
}

// handleServerState GET /server/state —— 纯本地状态快照，**不请求上游**。
//
// C++ 侧每几秒轮询一次，用来发现「电脑端刚把 Cookie 导进来了」。
// 之所以不复用 /config：那个接口会顺带请求 m.weibo.cn/api/config，
// 每几秒打一次上游容易被风控。
func handleServerState(w http.ResponseWriter, r *http.Request) {
	handleAPI(w, r, http.MethodGet, func(req *http.Request) (map[string]any, error) {
		return localState()
	})
}
