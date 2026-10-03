#include "WeiboJsonUtils.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QTime>

// 本文件集中处理 m.weibo.cn / weibo.com 返回文本里的脏数据：
//   * 正文是 HTML 片段（<br />、<a>、表情 <img>、转义实体混杂）
//   * 时间字段格式五花八门，甚至带 HTML（"<a ...>12:30</a>"）
//
// 注意：所有 QRegularExpression 都用函数内局部实例。Qt 只保证
// QRegularExpression 是 reentrant（每个线程一个实例才安全），
// 用 static 实例跨线程 match() 并不安全。

namespace {

constexpr qint64 kSecondsPerHour = 3600;
constexpr qint64 kSecondsPerDay = 86400;

// ---- HTML 实体 -----------------------------------------------------------

// 解析一个实体名（不含 & 和 ;）；成功返回 true。
bool decodeEntity(const QString &entity, QString *out)
{
    const QString lower = entity.toLower();
    if (lower == QLatin1String("amp")) { *out = QStringLiteral("&"); return true; }
    if (lower == QLatin1String("lt")) { *out = QStringLiteral("<"); return true; }
    if (lower == QLatin1String("gt")) { *out = QStringLiteral(">"); return true; }
    if (lower == QLatin1String("quot")) { *out = QStringLiteral("\""); return true; }
    if (lower == QLatin1String("apos")) { *out = QStringLiteral("'"); return true; }
    if (lower == QLatin1String("nbsp")) { *out = QStringLiteral(" "); return true; }
    // 数字实体：&#39; / &#x1F600;
    if (lower.startsWith(QLatin1String("#x"))) {
        bool ok = false;
        const uint code = lower.mid(2).toUInt(&ok, 16);
        if (!ok || code == 0)
            return false;
        *out = QString::fromUcs4(&code, 1);
        return !out->isEmpty();
    }
    if (lower.startsWith(QLatin1Char('#'))) {
        bool ok = false;
        const uint code = lower.mid(1).toUInt(&ok, 10);
        if (!ok || code == 0)
            return false;
        *out = QString::fromUcs4(&code, 1);
        return !out->isEmpty();
    }
    return false;
}

// 把所有实体解码成真实字符。
QString decodeEntities(const QString &text)
{
    if (!text.contains(QLatin1Char('&')))
        return text;

    QString out;
    out.reserve(text.size());
    const int len = text.size();
    int i = 0;
    while (i < len) {
        const QChar ch = text.at(i);
        if (ch == QChar(0x00A0)) {  // 非断行空格直接当普通空格，省得 QML 里对不齐
            out += QLatin1Char(' ');
            ++i;
            continue;
        }
        if (ch != QLatin1Char('&')) {
            out += ch;
            ++i;
            continue;
        }
        const int semi = text.indexOf(QLatin1Char(';'), i + 1);
        if (semi > i && semi - i <= 12) {  // 实体最长就这么长，避免误吃普通 &
            QString decoded;
            if (decodeEntity(text.mid(i + 1, semi - i - 1), &decoded)) {
                out += decoded;
                i = semi + 1;
                continue;
            }
        }
        out += ch;
        ++i;
    }
    return out;
}

// 转义成 HTML 文本 / 属性都安全的形态。
QString escapeHtml(const QString &text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        switch (ch.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        case '\'': out += QLatin1String("&#39;"); break;
        default: out += ch; break;
        }
    }
    return out;
}

// "<" 后面这一段是否像一个标签名（而不是数学里的小于号）。
bool looksLikeTagName(const QString &inner)
{
    if (inner.isEmpty())
        return false;
    const QChar first = inner.at(0);
    return first.isLetter() || first == QLatin1Char('/') || first == QLatin1Char('!')
            || first == QLatin1Char('?');
}

// 从标签内部文本（不含 <>）里取属性值，支持 双引号/单引号/无引号。
QString extractAttr(const QString &tag, const QString &attr)
{
    const QString wanted = attr.toLower();
    const int len = tag.size();
    int pos = 0;
    while (pos < len) {
        while (pos < len && tag.at(pos).isSpace())
            ++pos;
        const int nameStart = pos;
        while (pos < len && (tag.at(pos).isLetterOrNumber() || tag.at(pos) == QLatin1Char('-')
                             || tag.at(pos) == QLatin1Char('_') || tag.at(pos) == QLatin1Char(':')))
            ++pos;
        if (pos == nameStart) {
            ++pos;  // 不是属性名，跳过这个字符继续找
            continue;
        }
        const QString name = tag.mid(nameStart, pos - nameStart).toLower();
        while (pos < len && tag.at(pos).isSpace())
            ++pos;
        if (pos >= len || tag.at(pos) != QLatin1Char('='))
            continue;  // 布尔属性（如 checked）
        ++pos;
        while (pos < len && tag.at(pos).isSpace())
            ++pos;
        QString value;
        if (pos < len && (tag.at(pos) == QLatin1Char('"') || tag.at(pos) == QLatin1Char('\''))) {
            const QChar quote = tag.at(pos);
            ++pos;
            const int end = tag.indexOf(quote, pos);
            if (end < 0) {
                value = tag.mid(pos);
                pos = len;
            } else {
                value = tag.mid(pos, end - pos);
                pos = end + 1;
            }
        } else {
            const int start = pos;
            while (pos < len && !tag.at(pos).isSpace())
                ++pos;
            value = tag.mid(start, pos - start);
        }
        if (name == wanted)
            return value;
    }
    return QString();
}

// 去掉空白/控制字符再判断协议，避免 "java\tscript:" 这种绕过写法。
QString compactUrl(const QString &url)
{
    QString out;
    out.reserve(url.size());
    for (int i = 0; i < url.size(); ++i) {
        const QChar ch = url.at(i);
        if (ch.unicode() > 0x20)
            out += ch.toLower();
    }
    return out;
}

// javascript: / data: 这类 href 必须丢掉（QML 富文本里点了会出事）。
bool isSafeUrl(const QString &url)
{
    const QString compact = compactUrl(url);
    if (compact.isEmpty())
        return false;
    return !(compact.startsWith(QLatin1String("javascript:"))
             || compact.startsWith(QLatin1String("data:"))
             || compact.startsWith(QLatin1String("vbscript:"))
             || compact.startsWith(QLatin1String("file:")));
}

// 上游常给站内相对链接（/status/xxx）或协议相对链接（//wx1.sinaimg.cn/...）。
QString normalizeUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed.startsWith(QLatin1String("//")))
        return QStringLiteral("https:") + trimmed;
    if (trimmed.startsWith(QLatin1Char('/')))
        return QStringLiteral("https://m.weibo.cn") + trimmed;
    return trimmed;
}

bool isPositiveInt(const QString &text)
{
    if (text.isEmpty() || text.size() > 6)
        return false;
    for (int i = 0; i < text.size(); ++i) {
        if (!text.at(i).isDigit())
            return false;
    }
    return text.toInt() > 0;
}

// "Mon" → 1；失败返回 0。
int monthFromName(const QString &name)
{
    static const char *const kNames[12] = { "jan", "feb", "mar", "apr", "may", "jun",
                                            "jul", "aug", "sep", "oct", "nov", "dec" };
    const QString lower = name.toLower();
    for (int i = 0; i < 12; ++i) {
        if (lower == QLatin1String(kNames[i]))
            return i + 1;
    }
    return 0;
}

// 用指定日期 + 匹配到的 时:分(:秒) 组一个时间戳；非法返回 0。
qint64 epochFromDateAndMatch(const QDate &date, const QRegularExpressionMatch &match,
                             int hourGroup, int minuteGroup, int secondGroup)
{
    if (!date.isValid())
        return 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (hourGroup > 0 && !match.captured(hourGroup).isEmpty()) {
        hour = match.captured(hourGroup).toInt();
        minute = match.captured(minuteGroup).toInt();
        if (secondGroup > 0)
            second = match.captured(secondGroup).toInt();
    }
    const QTime time(hour, minute, second);
    if (!time.isValid())
        return 0;
    return QDateTime(date, time).toSecsSinceEpoch();
}

}  // namespace

// ===========================================================================
// 文本处理
// ===========================================================================

QString WeiboJson::stripHtml(const QString &html)
{
    if (html.isEmpty())
        return QString();

    QString text;
    text.reserve(html.size());

    const int len = html.size();
    int i = 0;
    while (i < len) {
        const QChar ch = html.at(i);

        if (ch == QLatin1Char('<')) {
            const int gt = html.indexOf(QLatin1Char('>'), i + 1);
            if (gt < 0) {
                text += html.mid(i);  // 没有闭合的 '<'，剩下的都当正文
                break;
            }
            const QString inner = html.mid(i + 1, gt - i - 1).trimmed();
            if (looksLikeTagName(inner)) {
                const QString tag = inner.toLower();
                // 换行标签必须在"去标签"之前处理，否则换行全丢了
                if (tag.startsWith(QLatin1String("br")))
                    text += QLatin1Char('\n');
                else if (tag == QLatin1String("/p") || tag == QLatin1String("p")
                         || tag == QLatin1String("/div") || tag == QLatin1String("/li")
                         || tag == QLatin1String("/tr") || tag == QLatin1String("/blockquote")
                         || tag == QLatin1String("/h1") || tag == QLatin1String("/h2")
                         || tag == QLatin1String("/h3"))
                    text += QLatin1Char('\n');
                i = gt + 1;
                continue;
            }
            text += ch;  // "<" 不是标签，例如 "5 < 10"
            ++i;
            continue;
        }

        if (ch == QLatin1Char('&')) {
            const int semi = html.indexOf(QLatin1Char(';'), i + 1);
            if (semi > i && semi - i <= 12) {
                QString decoded;
                if (decodeEntity(html.mid(i + 1, semi - i - 1), &decoded)) {
                    text += decoded;
                    i = semi + 1;
                    continue;
                }
            }
            text += ch;
            ++i;
            continue;
        }

        if (ch == QLatin1Char('\r')) {
            text += QLatin1Char('\n');
            ++i;
            if (i < len && html.at(i) == QLatin1Char('\n'))
                ++i;  // CRLF 只算一个换行
            continue;
        }

        if (ch == QChar(0x00A0)) {
            text += QLatin1Char(' ');
            ++i;
            continue;
        }

        text += ch;
        ++i;
    }

    // 3 个以上连续换行折叠成 2 个（正文里常见 <br><br><br>）
    QString collapsed;
    collapsed.reserve(text.size());
    int newlines = 0;
    for (int k = 0; k < text.size(); ++k) {
        const QChar c = text.at(k);
        if (c == QLatin1Char('\n')) {
            ++newlines;
            if (newlines <= 2)
                collapsed += c;
        } else {
            newlines = 0;
            collapsed += c;
        }
    }
    return collapsed.trimmed();
}

QString WeiboJson::htmlToPlainText(const QString &html)
{
    if (html.isEmpty())
        return QString();
    if (!html.contains(QLatin1Char('<')))
        return decodeEntities(html).trimmed();

    // 先把表情 <img alt="微笑"> 变成 [微笑]，其余标签留给 stripHtml。
    QString replaced;
    replaced.reserve(html.size());
    const int len = html.size();
    int i = 0;
    while (i < len) {
        const QChar ch = html.at(i);
        if (ch == QLatin1Char('<')) {
            const int gt = html.indexOf(QLatin1Char('>'), i + 1);
            if (gt < 0) {
                replaced += html.mid(i);
                break;
            }
            const QString tag = html.mid(i + 1, gt - i - 1);
            const QString lower = tag.trimmed().toLower();
            if (lower.startsWith(QLatin1String("img"))) {
                const QString alt = extractAttr(tag, QStringLiteral("alt")).trimmed();
                if (!alt.isEmpty())
                    replaced += QLatin1Char('[') + alt + QLatin1Char(']');
            } else {
                replaced += html.mid(i, gt - i + 1);
            }
            i = gt + 1;
            continue;
        }
        replaced += ch;
        ++i;
    }
    return stripHtml(replaced);
}

QString WeiboJson::sanitizeHtmlForQml(const QString &html, const QString &linkColor)
{
    if (html.isEmpty())
        return QString();

    const QString color = linkColor.trimmed().isEmpty() ? QStringLiteral("#576B95")
                                                        : linkColor.trimmed();

    QString out;
    out.reserve(html.size() + 64);

    int openAnchors = 0;
    const int len = html.size();
    int i = 0;
    while (i < len) {
        const QChar ch = html.at(i);

        if (ch != QLatin1Char('<')) {
            // 文本节点：先解实体再统一转义，这样 "&amp;" 不会变成 "&amp;amp;"
            const int lt = html.indexOf(QLatin1Char('<'), i);
            const QString chunk = (lt < 0) ? html.mid(i) : html.mid(i, lt - i);
            out += escapeHtml(decodeEntities(chunk));
            i = (lt < 0) ? len : lt;
            continue;
        }

        const int gt = html.indexOf(QLatin1Char('>'), i + 1);
        if (gt < 0) {
            out += escapeHtml(decodeEntities(html.mid(i)));
            break;
        }
        const QString tag = html.mid(i + 1, gt - i - 1);
        const QString lower = tag.trimmed().toLower();
        i = gt + 1;

        // <br> / <br/> / <br /> 统一成 XHTML 写法，QML 富文本都认
        if (lower.startsWith(QLatin1String("br"))) {
            out += QLatin1String("<br/>");
            continue;
        }

        if (lower == QLatin1String("/a")) {
            if (openAnchors > 0) {
                out += QLatin1String("</a>");
                --openAnchors;
            }
            continue;
        }

        // 只放行 <a href="...">，并且强制去掉 target，强制链接色 + 无下划线
        if (lower.startsWith(QLatin1String("a"))
            && (lower.size() == 1 || lower.at(1).isSpace())) {
            const QString href = extractAttr(tag, QStringLiteral("href")).trimmed();
            if (!href.isEmpty() && isSafeUrl(href)) {
                out += QStringLiteral("<a href=\"%1\" style=\"color:%2;text-decoration:none;\">")
                           .arg(escapeHtml(normalizeUrl(href)), escapeHtml(color));
                ++openAnchors;
            }
            // href 不合法（javascript:/data:/空）时把 <a> 本身丢掉，保留里面的文字
            continue;
        }

        // 只放行 <img src alt width height>
        if (lower.startsWith(QLatin1String("img"))) {
            const QString src = extractAttr(tag, QStringLiteral("src")).trimmed();
            if (src.isEmpty() || !isSafeUrl(src))
                continue;
            out += QStringLiteral("<img src=\"%1\"").arg(escapeHtml(normalizeUrl(src)));
            const QString alt = extractAttr(tag, QStringLiteral("alt"));
            if (!alt.isEmpty())
                out += QStringLiteral(" alt=\"%1\"").arg(escapeHtml(alt));
            const QString width = extractAttr(tag, QStringLiteral("width")).trimmed();
            if (isPositiveInt(width))
                out += QStringLiteral(" width=\"%1\"").arg(width);
            const QString height = extractAttr(tag, QStringLiteral("height")).trimmed();
            if (isPositiveInt(height))
                out += QStringLiteral(" height=\"%1\"").arg(height);
            out += QLatin1String("/>");
            continue;
        }

        // 其余标签一律转义：QML 侧永远不会把用户/上游内容当 HTML 解析。
        out += QLatin1String("&lt;");
        out += escapeHtml(decodeEntities(tag));
        out += QLatin1String("&gt;");
    }

    // 补齐没闭合的 <a>，否则 QML 富文本后面所有内容都会变成链接
    for (; openAnchors > 0; --openAnchors)
        out += QLatin1String("</a>");

    return out;
}

// ===========================================================================
// 时间
// ===========================================================================

qint64 WeiboJson::parseWeiboTime(const QString &raw)
{
    if (raw.isEmpty())
        return 0;

    // 有些字段是 <a href="...">12:30</a>，先剥掉 HTML
    const QString s = stripHtml(raw).trimmed();
    if (s.isEmpty())
        return 0;

    const QDate today = QDate::currentDate();

    if (s == QStringLiteral("刚刚"))
        return QDateTime::currentSecsSinceEpoch();

    // N秒前 / N分钟前 / N小时前 / N天前
    {
        const QRegularExpression re(QStringLiteral("^(\\d+)\\s*(秒|分钟|小时|天)前$"));
        const QRegularExpressionMatch m = re.match(s);
        if (m.hasMatch()) {
            const qint64 n = m.captured(1).toLongLong();
            const QString unit = m.captured(2);
            qint64 multiplier = 1;
            if (unit == QStringLiteral("分钟"))
                multiplier = 60;
            else if (unit == QStringLiteral("小时"))
                multiplier = kSecondsPerHour;
            else if (unit == QStringLiteral("天"))
                multiplier = kSecondsPerDay;
            return QDateTime::currentSecsSinceEpoch() - n * multiplier;
        }
    }

    // 纯 10 位（秒）/ 13 位（毫秒）时间戳
    {
        bool ok = false;
        const qint64 digits = s.toLongLong(&ok);
        if (ok && digits > 0) {
            if (s.size() == 13)
                return digits / 1000;
            if (s.size() == 10)
                return digits;
        }
    }

    // 今天 / 昨天 HH:mm(:ss)
    {
        const QRegularExpression todayRe(
                    QStringLiteral("^今天\\s*(\\d{1,2}):(\\d{2})(?::(\\d{2}))?$"));
        const QRegularExpressionMatch m = todayRe.match(s);
        if (m.hasMatch())
            return epochFromDateAndMatch(today, m, 1, 2, 3);

        const QRegularExpression yesterdayRe(
                    QStringLiteral("^昨天\\s*(\\d{1,2}):(\\d{2})(?::(\\d{2}))?$"));
        const QRegularExpressionMatch ym = yesterdayRe.match(s);
        if (ym.hasMatch())
            return epochFromDateAndMatch(today.addDays(-1), ym, 1, 2, 3);
    }

    // yyyy-MM-dd [HH:mm[:ss]]
    {
        const QRegularExpression re(QStringLiteral(
            "^(\\d{4})-(\\d{1,2})-(\\d{1,2})(?:[ T](\\d{1,2}):(\\d{2})(?::(\\d{2}))?)?(?:Z)?$"));
        const QRegularExpressionMatch m = re.match(s);
        if (m.hasMatch()) {
            const QDate date(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
            if (!date.isValid())
                return 0;
            return epochFromDateAndMatch(date, m, 4, 5, 6);
        }
    }

    // MM-dd HH:mm[:ss]（当年，上游省掉了年份）
    {
        const QRegularExpression re(
                    QStringLiteral("^(\\d{1,2})-(\\d{1,2})\\s+(\\d{1,2}):(\\d{2})(?::(\\d{2}))?$"));
        const QRegularExpressionMatch m = re.match(s);
        if (m.hasMatch()) {
            const QDate date(today.year(), m.captured(1).toInt(), m.captured(2).toInt());
            if (!date.isValid())
                return 0;
            return epochFromDateAndMatch(date, m, 3, 4, 5);
        }
    }

    // MM-dd（当年）
    {
        const QRegularExpression re(QStringLiteral("^(\\d{1,2})-(\\d{1,2})$"));
        const QRegularExpressionMatch m = re.match(s);
        if (m.hasMatch()) {
            const QDate date(today.year(), m.captured(1).toInt(), m.captured(2).toInt());
            if (!date.isValid())
                return 0;
            return QDateTime(date, QTime(0, 0)).toSecsSinceEpoch();
        }
    }

    // HH:mm[:ss]（今天）
    {
        const QRegularExpression re(QStringLiteral("^(\\d{1,2}):(\\d{2})(?::(\\d{2}))?$"));
        const QRegularExpressionMatch m = re.match(s);
        if (m.hasMatch())
            return epochFromDateAndMatch(today, m, 1, 2, 3);
    }

    // yyyy年M月d日 [HH:mm]
    {
        const QRegularExpression re(QStringLiteral(
            "^(\\d{4})年(\\d{1,2})月(\\d{1,2})日(?:\\s*(\\d{1,2}):(\\d{2}))?$"));
        const QRegularExpressionMatch m = re.match(s);
        if (m.hasMatch()) {
            const QDate date(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
            if (!date.isValid())
                return 0;
            return epochFromDateAndMatch(date, m, 4, 5, 0);
        }
    }

    // C 风格：Mon Jan 02 15:04:05 +0800 2024
    // 上游给的是**带时区偏移**的本地时间，必须把它换算成 UTC 才能得到正确时间戳。
    {
        const QRegularExpression re(QStringLiteral(
            "^([A-Za-z]{3})\\s+([A-Za-z]{3})\\s+(\\d{1,2})\\s+(\\d{1,2}):(\\d{2}):(\\d{2})"
            "\\s*(?:([+-]\\d{4}))?\\s+(\\d{4})$"));
        const QRegularExpressionMatch m = re.match(s);
        if (m.hasMatch()) {
            const int month = monthFromName(m.captured(2));
            const QDate date(m.captured(8).toInt(), month, m.captured(3).toInt());
            const QTime time(m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt());
            if (date.isValid() && time.isValid()) {
                qint64 epoch = QDateTime(date, time, Qt::UTC).toSecsSinceEpoch();
                const QString offset = m.captured(7);
                if (!offset.isEmpty()) {
                    const int sign = (offset.at(0) == QLatin1Char('-')) ? -1 : 1;
                    const int hours = offset.mid(1, 2).toInt();
                    const int minutes = offset.mid(3, 2).toInt();
                    epoch -= sign * (hours * kSecondsPerHour + minutes * 60);
                }
                return epoch;
            }
        }
    }

    // 最后兜底：交给 Qt 的 ISO 解析（"2024-01-01T12:00:00+08:00" 之类）
    {
        const QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
        if (dt.isValid())
            return dt.toSecsSinceEpoch();
    }

    return 0;
}

QString WeiboJson::formatRelativeTime(qint64 ts)
{
    if (ts <= 0)
        return QString();

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 diff = now.toSecsSinceEpoch() - ts;
    if (diff < 60)
        return QStringLiteral("刚刚");  // 设备时钟偏差导致的"未来时间"也按刚刚处理
    if (diff < kSecondsPerHour)
        return QStringLiteral("%1分钟前").arg(diff / 60);
    if (diff < kSecondsPerDay)
        return QStringLiteral("%1小时前").arg(diff / kSecondsPerHour);

    const QDateTime dt = QDateTime::fromSecsSinceEpoch(ts);
    if (dt.date() == now.date().addDays(-1))
        return QStringLiteral("昨天 %1").arg(dt.toString(QStringLiteral("HH:mm")));
    if (dt.date().year() == now.date().year())
        return dt.toString(QStringLiteral("MM-dd"));
    return dt.toString(QStringLiteral("yyyy-MM-dd"));
}

QString WeiboJson::formatCount(qint64 n)
{
    if (n < 0)
        n = 0;
    if (n < 10000)
        return QString::number(n);

    // 微博的计数是**截断**：12345 → "1.2万"（不是四舍五入的 1.2/1.3）
    const bool useYi = (n >= 100000000);
    const qint64 unit = useYi ? 100000000 : 10000;
    const qint64 tenths = n / (unit / 10);
    const qint64 whole = tenths / 10;
    const qint64 fraction = tenths % 10;

    QString text = QString::number(whole);
    if (fraction > 0) {
        text += QLatin1Char('.');
        text += QString::number(fraction);
    }
    text += useYi ? QStringLiteral("亿") : QStringLiteral("万");
    return text;
}

// ===========================================================================
// 计数与状态
// ===========================================================================

qint64 WeiboJson::parseLooseCount(const QJsonValue &v)
{
    if (v.isString()) {
        // 极少数接口会把计数塞成 "<a>1.2万</a>" 或 "1,234"
        QString plain = stripHtml(v.toString()).trimmed();
        plain.remove(QLatin1Char(','));
        plain.remove(QLatin1Char(' '));
        if (plain.isEmpty())
            return 0;
        return num(QJsonValue(plain), 0);
    }
    return num(v, 0);
}

int WeiboJson::parseAttitudeStatus(const QJsonObject &mblog)
{
    static const char *const kKeys[] = { "attitudes_status", "attitude_status", "liked" };
    for (const char *key : kKeys) {
        const QJsonValue v = mblog.value(QLatin1String(key));
        if (v.isUndefined() || v.isNull())
            continue;
        if (v.isBool())
            return v.toBool() ? 1 : 0;
        if (v.isDouble())
            return (v.toDouble() != 0.0) ? 1 : 0;
        if (v.isString()) {
            const QString s = v.toString().trimmed();
            if (s.isEmpty())
                continue;
            if (s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
                return 1;
            if (s.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
                return 0;
            bool ok = false;
            const int n = s.toInt(&ok);
            if (ok)
                return (n != 0) ? 1 : 0;
        }
    }
    return 0;
}
