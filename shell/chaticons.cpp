#include "shell/chaticons.h"

#include <QBuffer>
#include <QImage>
#include <QRegularExpression>
#include <vector>

#include "shell/dmiicon.h"
#include "skin/winicon.h"

namespace fugue::shell {
namespace {

constexpr int kSouth = 2;

// An <img ...> tag, quoted attribute values may hold '>'.
const QRegularExpression& imgTag() {
    static const QRegularExpression re(R"re(<img\b(?:[^>'"]|'[^']*'|"[^"]*")*>)re",
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

struct Tag {
    bool icon = false;  // SRC names a ResourceList entry
    bool none = false;  // a reference to no file ([0x0], a non-resource type): draws nothing
    uint32_t ordinal = 0;
    bool hasState = false;  // ICONSTATE present; absent means the icon's first state
    std::string state;
};

Tag parseTag(const QString& tag) {
    static const QRegularExpression src(R"re(\bsrc\s*=\s*\[(0x[0-9a-fA-F]+)\])re",
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression st(R"re(\biconstate\s*=\s*(?:'((?:[^'\\]|\\.)*)'|"((?:[^"\\]|\\.)*)"))re",
                                       QRegularExpression::CaseInsensitiveOption);
    Tag t;
    auto m = src.match(tag);
    if (!m.hasMatch()) return t;
    skin::IconRef r = skin::parseIconRef("[" + m.captured(1).toStdString() + "]");
    t.icon = true;
    if (r.kind != skin::IconRef::Ordinal) {
        t.none = true;
        return t;
    }
    t.ordinal = r.ordinal;
    auto s = st.match(tag);
    if (s.hasMatch()) {
        t.hasState = true;
        QString v = s.captured(s.captured(1).isNull() ? 2 : 1);
        v.replace(QRegularExpression(R"(\\(.))"), "\\1");
        t.state = v.toStdString();
    }
    return t;
}

// The tag's replacement: the state's south-facing first frame as an inline PNG at the icon's own size. Empty for a
// named state the icon lacks (and it has no unnamed one) or bytes that are not an image. With no ICONSTATE, stock draws
// the unnamed state, or the icon's first state when it has none.
QString cellImg(const ChatIconLookup& l, const Tag& tag) {
    DmiIcon decoded;
    const DmiIcon* icon = l.icon;
    if (!icon) {
        decoded = DmiIcon::decode(QByteArray::fromStdString(l.bytes));
        icon = &decoded;
    }
    if (icon->sheet.isNull() || icon->cell.isEmpty()) return {};
    // DmiIcon::state degrades a state the sheet lacks to the unnamed one, as the map does.
    const DmiIcon::State* s = icon->state(QString::fromStdString(tag.state));
    if (!s && !tag.hasState && icon->metadata)
        for (const assets::IconState& first : *icon->metadata)
            if (!first.movement) {
                s = icon->state(QString::fromStdString(first.name));
                break;
            }
    if (!s) return {};
    const QImage img = icon->frame(icon->index(*s, kSouth));
    if (img.isNull()) return {};
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "PNG")) return {};
    return QString("<img class=\"icon\" src=\"data:image/png;base64,%1\" width=\"%2\" height=\"%3\">")
        .arg(QString::fromLatin1(png.toBase64()))
        .arg(img.width())
        .arg(img.height());
}

}  // namespace

QString expandChatIcons(const QString& html, const std::function<ChatIconLookup(uint32_t)>& get, bool* pending) {
    if (pending) *pending = false;
    if (!html.contains("<img", Qt::CaseInsensitive)) return html;
    QString out;
    out.reserve(html.size());
    qsizetype last = 0;
    auto it = imgTag().globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        Tag t = parseTag(m.captured(0));
        if (!t.icon) continue;
        if (t.none) {
            out += html.mid(last, m.capturedStart() - last);
            last = m.capturedEnd();
            continue;
        }
        ChatIconLookup l = get(t.ordinal);
        if (l.status == ChatIconLookup::Pending) {
            if (pending) *pending = true;
            continue;
        }
        out += html.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        if (l.status == ChatIconLookup::Ready) out += cellImg(l, t);
    }
    out += html.mid(last);
    return out;
}

}  // namespace fugue::shell
