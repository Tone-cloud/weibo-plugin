#pragma once

// 列表分页拉取的公共骨架。
//
// bili 用 BiliListFetch::fetchParsed 把「发请求 → 校验对象存活 → 解析 →
// 追加到模型 / 报错」这套样板收敛到一处。这里保持同样的形状，但因为是
// 模板 + 头文件实现，各 Module 直接 include 即可。

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>

#include "WeiboController.h"
#include "WeiboNetwork.h"

namespace WeiboListFetch {

enum class Via { Network = 0 };

// ParseFn:  (const QJsonObject &data) -> Parsed
// ApplyFn:  (WeiboController *self, Parsed result) -> void
// FailFn:   (WeiboController *self, int code, const QString &msg) -> void
template <typename ParseFn, typename ApplyFn, typename FailFn>
void fetchParsed(WeiboController *controller, Via via, const QString &path,
                 const QMap<QString, QString> &params, ParseFn parse,
                 ApplyFn apply, FailFn fail, int timeoutMs = 0) {
    Q_UNUSED(via)
    if (!controller)
        return;
    WeiboNetwork *network = controller->network();
    if (!network)
        return;

    network->get(
        path, params,
        [controller, parse = std::move(parse), apply = std::move(apply)](
            const QJsonObject &data) mutable {
            if (!controller)
                return;
            apply(controller, parse(data));
        },
        [controller, fail = std::move(fail)](int code, const QString &msg) mutable {
            if (!controller)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                controller->clearLocalLoginState();
            fail(controller, code, msg);
        },
        timeoutMs);
}

// POST 版本：BodyFn 产出 JSON 请求体。
template <typename BodyFn, typename ParseFn, typename ApplyFn, typename FailFn>
void postParsed(WeiboController *controller, const QString &path, BodyFn body,
                ParseFn parse, ApplyFn apply, FailFn fail) {
    if (!controller)
        return;
    WeiboNetwork *network = controller->network();
    if (!network)
        return;

    network->post(
        path, body(),
        [controller, parse = std::move(parse), apply = std::move(apply)](
            const QJsonObject &data) mutable {
            if (!controller)
                return;
            apply(controller, parse(data));
        },
        [controller, fail = std::move(fail)](int code, const QString &msg) mutable {
            if (!controller)
                return;
            if (code == -100 || code == -101)
                controller->clearLocalLoginState();
            fail(controller, code, msg);
        });
}

}  // namespace WeiboListFetch
