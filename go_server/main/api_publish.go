package main

import (
	"net/url"
	"strconv"
	"strings"
	"time"
)

// ============================================================================
// 发布：发微博 / 传图
//
// 发微博走 weibo.com/ajax/statuses/update（需要 XSRF-TOKEN，且从 Cookie 里
// url 解码得到）；传图走 picupload.weibo.com。
// ============================================================================

// publishStatus 发一条微博。
//
// visible：0 公开 / 1 仅自己可见 / 6 好友圈（透传给上游）。
// picIDs：先通过 uploadPicture 拿到的 pic_id 列表。
func publishStatus(content string, visible int, picIDs []string) (map[string]any, error) {
	text := strings.TrimSpace(content)
	pics := make([]string, 0, len(picIDs))
	for _, id := range picIDs {
		if trimmed := strings.TrimSpace(id); trimmed != "" {
			pics = append(pics, trimmed)
		}
	}
	if text == "" && len(pics) == 0 {
		return nil, errBadRequest("微博内容不能为空")
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	if client.xsrfToken() == "" {
		// XSRF-TOKEN 一般随 /api/config 或 weibo.com 首页下发，这里补刷一次。
		client.refreshLoginState()
	}
	if client.xsrfToken() == "" {
		return nil, &appError{Code: -101, Status: 200, Message: "缺少 XSRF-TOKEN，请在设置中重新导入 Cookie"}
	}
	form := url.Values{}
	form.Set("content", text)
	form.Set("visible", strconv.Itoa(visible))
	form.Set("share", "0")
	if len(pics) > 0 {
		form.Set("pic_id", strings.Join(pics, ","))
	}
	raw, err := client.webPost(epStatusUpdate, form)
	if err != nil {
		return nil, err
	}
	root := decodeJSONMap(raw)
	if err := ensureWebOK(root, "发布失败"); err != nil {
		return nil, err
	}
	out := map[string]any{}
	out["ok"] = true
	out["id"] = extractCreatedID(root)
	out["bid"] = extractCreatedBID(root)
	return out, nil
}

// uploadPicture 上传一张图片（base64），返回 pic_id。
//
// 上游 picupload 的响应可能是 window.parent.cb({...}) 形式，
// extractJSONObject + extractPicID 会尽力把它解析出来；解析不出来时返回明确的中文错误。
func uploadPicture(base64Data, filename string) (map[string]any, error) {
	data := strings.TrimSpace(base64Data)
	if data == "" {
		return nil, errBadRequest("图片数据不能为空")
	}
	if strings.HasPrefix(data, "data:") {
		if index := strings.Index(data, ","); index >= 0 {
			data = strings.TrimSpace(data[index+1:])
		}
	}
	if data == "" {
		return nil, errBadRequest("图片 base64 数据无效")
	}
	name := strings.TrimSpace(filename)
	if name == "" {
		name = "weibo_" + strconv.FormatInt(time.Now().Unix(), 10) + ".jpg"
	}
	client := getClient()
	if err := client.requireLogin(); err != nil {
		return nil, err
	}
	form := url.Values{}
	form.Set("b64_data", data)
	form.Set("mime", "image/jpeg")
	form.Set("file", name)
	form.Set("cb", "https://weibo.com/ajax/statuses/uploadPic")
	form.Set("p", "1")
	form.Set("markpos", "1")
	form.Set("logo", "1")
	if uid := client.uid(); uid != "" {
		form.Set("url", "weibo.com/u/"+uid)
	}
	headers := map[string]string{"Referer": webReferer}
	raw, err := client.PostForm(epPicUpload+"?data=base64", form, headers)
	if err != nil {
		return nil, err
	}
	picID := extractPicID(raw)
	if picID == "" {
		return nil, &appError{Code: -2, Status: 200, Message: "图片上传失败：未能从微博返回中解析出 pic_id，请稍后重试或更换图片"}
	}
	out := map[string]any{}
	out["ok"] = true
	out["pic_id"] = picID
	return out, nil
}
