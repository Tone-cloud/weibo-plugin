#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// 上游 m.weibo.cn / weibo.com 返回的字段类型极不稳定（同名字段可能是
// 字符串、数字、null，或是 {"url": "..."} 这种二级结构）。
// 这里集中所有容错取值，供 Models 与各 Module 复用。

namespace WeiboJson {

// ---- 基础取值 ----------------------------------------------------------

// 取字符串；数字会转成字符串（微博 id 常以数字下发但需要按字符串使用）。
inline QString str(const QJsonValue &v, const QString &def = QString()) {
    if (v.isString())
        return v.toString();
    if (v.isDouble()) {
        const double d = v.toDouble();
        // 整数值不要输出 ".0"
        if (d == static_cast<double>(static_cast<qint64>(d)))
            return QString::number(static_cast<qint64>(d));
        return QString::number(d);
    }
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return def;
}

inline QString str(const QJsonObject &o, const char *key,
                   const QString &def = QString()) {
    return str(o.value(QLatin1String(key)), def);
}

// 兼容 "count": 12 / "count": "12" / "count": "1.2万"
inline qint64 num(const QJsonValue &v, qint64 def = 0) {
    if (v.isDouble())
        return static_cast<qint64>(v.toDouble());
    if (v.isString()) {
        const QString s = v.toString().trimmed();
        if (s.isEmpty())
            return def;
        bool ok = false;
        const qint64 direct = s.toLongLong(&ok);
        if (ok)
            return direct;
        // "1.2万" / "3.4亿"
        static const QChar wan(0x4E07);  // 万
        static const QChar yi(0x4EBF);   // 亿
        if (s.endsWith(wan) || s.endsWith(yi)) {
            const double base = s.left(s.size() - 1).toDouble(&ok);
            if (ok)
                return static_cast<qint64>(base * (s.endsWith(yi) ? 100000000.0 : 10000.0));
        }
        const double d = s.toDouble(&ok);
        if (ok)
            return static_cast<qint64>(d);
    }
    return def;
}

inline qint64 num(const QJsonObject &o, const char *key, qint64 def = 0) {
    return num(o.value(QLatin1String(key)), def);
}

inline bool boolean(const QJsonValue &v, bool def = false) {
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toDouble() != 0.0;
    if (v.isString()) {
        const QString s = v.toString().trimmed().toLower();
        if (s == QLatin1String("true") || s == QLatin1String("1"))
            return true;
        if (s == QLatin1String("false") || s == QLatin1String("0") || s.isEmpty())
            return false;
    }
    return def;
}

inline bool boolean(const QJsonObject &o, const char *key, bool def = false) {
    return boolean(o.value(QLatin1String(key)), def);
}

inline QJsonObject obj(const QJsonObject &o, const char *key) {
    return o.value(QLatin1String(key)).toObject();
}

inline QJsonArray arr(const QJsonObject &o, const char *key) {
    return o.value(QLatin1String(key)).toArray();
}

// 有些接口把数组包在 {"list":[...]} 里。
inline QJsonArray arrOrList(const QJsonObject &o, const char *key) {
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isArray())
        return v.toArray();
    if (v.isObject())
        return v.toObject().value(QStringLiteral("list")).toArray();
    return QJsonArray();
}

// ---- 文本处理 ----------------------------------------------------------

// 剥离 HTML 标签，解转义常见实体，把 <br> 换成换行。
// m.weibo.cn 的正文是 HTML 片段，QML 的 Text 不能直接渲染。
QString stripHtml(const QString &html);

// 取出 HTML 里的表情 <img>，替换成 [表情名] 文本（alt 属性）。
QString htmlToPlainText(const QString &html);

// 把 HTML 里 <a href="..."> 转成保留链接的简化 HTML（QML Text 富文本可用）。
QString sanitizeHtmlForQml(const QString &html, const QString &linkColor);

// ---- 时间 --------------------------------------------------------------

// 微博时间格式五花八门：
//   "刚刚" / "3分钟前" / "今天 12:30" / "12-25" / "2024-01-01 12:00:00"
//   / "Mon Jan 01 12:00:00 +0800 2024" / "<a ...>12:30</a>"
// 统一解析成秒级时间戳（解析不出来的返回 0）。
qint64 parseWeiboTime(const QString &raw);

// 把秒级时间戳格式化成 "刚刚 / 5分钟前 / 3小时前 / 昨天 12:30 / 01-01 / 2024-01-01"。
QString formatRelativeTime(qint64 ts);

// 大数字转 "1.2万" / "3.4亿"。
QString formatCount(qint64 n);

// ---- 计数与状态 --------------------------------------------------------

// 微博正文里的 attitudes_count / comments_count / reposts_count 有时是
// 字符串形式的 "1.2万"，stripHtml 后再 num() 一次。
qint64 parseLooseCount(const QJsonValue &v);

// 从 mblog 里判断 attitudes_status：1 表示已赞。
int parseAttitudeStatus(const QJsonObject &mblog);

}  // namespace WeiboJson
