package main

// 全局常量与启动期静态数据。
//
// 说明：这里刻意把所有常量放进同一个 const 块，便于和 SPEC 第 1 节逐条对照。
//
//	appVersion        —— 与 metadata.json / SPEC 第 1 节保持一致
//	defaultPort       —— sidecar 默认监听端口（PORT 环境变量可覆盖）
//	defaultUserAgent  —— 移动端 Safari UA，m.weibo.cn 对移动 UA 更宽容
//	defaultReferer    —— m.weibo.cn 接口默认 Referer
//	webReferer        —— weibo.com 写操作（发微博 / 超话签到）需要的 Referer
//	acceptLanguage    —— 固定中文，避免上游返回繁体或英文文案
const (
	appVersion       = "1.0.0"
	defaultPort      = "8010"
	defaultUserAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1"
	defaultReferer   = "https://m.weibo.cn/"
	webReferer       = "https://weibo.com/"
	acceptLanguage   = "zh-CN,zh;q=0.9,en;q=0.8"
)

// defaultPluginDir 是设备上的插件根目录（SPEC 第 1 节）。
// 只有被环境变量覆盖或该目录不可写时才会回落到当前工作目录。
const defaultPluginDir = "/userdisk/PenMods/plugins/weibo_plugin"

// cookieProbeURLs 是读取 Cookiejar 时使用的代表 URL。
// Cookiejar 只能按 URL 取 Cookie，这里覆盖微博的所有主域。
var cookieProbeURLs = []string{
	"https://weibo.com/",
	"https://m.weibo.cn/",
	"https://passport.weibo.com/",
	"https://picupload.weibo.com/",
}

// rootEndpoints 是 GET / 返回的接口索引，与 routes.go 中的注册一一对应。
var rootEndpoints = []string{
	"GET    /                         接口索引",
	"GET    /server/ping              sidecar 存活探测",
	"GET    /config                   登录态与用户摘要",
	"GET    /feed/home                首页推荐流（since_id, fresh_type）",
	"GET    /feed/follow              关注流（since_id）",
	"GET    /feed/group               分组流（gid, since_id）",
	"GET    /feed/hot                 热搜榜",
	"GET    /feed/hot/status          热搜微博流（word, since_id）",
	"GET    /search/status            综合搜索（q, page）",
	"GET    /search/user              用户搜索（q, page）",
	"GET    /search/topic             话题搜索（q, page）",
	"GET    /search/history           搜索历史",
	"POST   /search/history/clear     清空搜索历史",
	"GET    /status/detail            微博详情（id）",
	"GET    /status/comments          评论列表（id, page, max_id, max_id_type）",
	"GET    /status/comments/replies  子评论（id, cid, page）",
	"GET    /status/reposts           转发列表（id, page）",
	"GET    /status/likers            点赞列表（id, page）",
	"POST   /status/repost            转发微博（id, content）",
	"POST   /status/comment           发表评论（id, content, cid, also_repost）",
	"POST   /status/comment/delete    删除评论（cid）",
	"POST   /status/comment/like      评论点赞（cid, liked）",
	"POST   /status/like              微博点赞（id, liked）",
	"POST   /status/favorite          收藏微博（id, fav）",
	"POST   /status/delete            删除微博（id）",
	"POST   /status/publish           发微博（content, visible, pic_ids）",
	"POST   /status/upload_pic        上传图片（data, filename）",
	"GET    /status/favorites         我的收藏（page）",
	"GET    /status/mentions          @我的（page）",
	"GET    /user/profile             用户资料（uid）",
	"GET    /user/me                  我的资料",
	"GET    /user/statuses            用户微博（uid, page, feature）",
	"GET    /user/following           关注列表（uid, page）",
	"GET    /user/followers           粉丝列表（uid, page）",
	"GET    /user/search              用户搜索（q, page）",
	"POST   /user/follow              关注 / 取关（uid, follow）",
	"GET    /user/groups              我的分组",
	"GET    /user/topics              我的超话（page）",
	"GET    /topic/detail             话题详情（container_id）",
	"GET    /topic/statuses           话题微博流（container_id, since_id）",
	"POST   /topic/checkin            超话签到（id, name）",
	"POST   /topic/checkin/all        一键签到全部超话",
	"GET    /topic/search             超话搜索（q, page）",
	"GET    /media/info               视频 / 直播直链（id）",
	"POST   /login/import             导入 Cookie（cookie）",
	"GET    /login/info               登录态详情",
	"POST   /logout                   退出登录",
}

// startupEndpoints 是启动横幅里打印的分组摘要（人类可读）。
var startupEndpoints = []string{
	"信息流： /feed/home  /feed/follow  /feed/group  /feed/hot  /feed/hot/status",
	"搜索：   /search/status  /search/user  /search/topic  /search/history",
	"微博：   /status/detail  /status/comments  /status/reposts  /status/likers",
	"写操作： /status/repost  /status/comment  /status/like  /status/favorite",
	"发布：   /status/publish  /status/upload_pic  /status/delete",
	"用户：   /user/profile  /user/me  /user/statuses  /user/following  /user/followers",
	"超话：   /topic/detail  /topic/statuses  /topic/checkin  /topic/checkin/all  /topic/search",
	"媒体：   /media/info",
	"登录：   /login/import  /login/info  /logout  /config  /server/ping",
}
