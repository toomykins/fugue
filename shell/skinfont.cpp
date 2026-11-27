#include "shell/skinfont.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QRegularExpression>
#include <QStringList>
#include <QStyleOptionButton>
#include <QStylePainter>

namespace fugue::shell {
namespace {

// Metric-compatible stand-ins, in order of preference.
QStringList twinsOf(const QString& lowerFamily) {
    if (lowerFamily == "arial") return {"Arial", "Liberation Sans", "Arimo", "Helvetica", "Nimbus Sans", "FreeSans"};
    if (lowerFamily == "times new roman")
        return {"Times New Roman", "Liberation Serif", "Tinos", "Times", "Nimbus Roman"};
    if (lowerFamily == "courier new") return {"Courier New", "Liberation Mono", "Cousine", "Courier", "Nimbus Mono PS"};
    return {};
}

bool isStockFace(const QString& l) {
    return l.isEmpty() || l == "sans-serif" || l == "monospace" || l == "ms sans serif" || l == "courier" ||
           l == "small fonts";
}

bool available(const QString& family) {
    if (QFontDatabase::hasFamily(family)) return true;
    for (const QString& t : twinsOf(family.toLower()))
        if (QFontDatabase::hasFamily(t)) return true;
    return false;
}

struct Run {
    std::vector<int> adv, at;
    int width = 0;
};
Run runOf(const SkinFont& f, const QString& text) {
    Run r;
    const QFontMetrics m(f.font);
    for (int i = 0; i < text.size();) {
        const char32_t cp = text.at(i).isHighSurrogate() && i + 1 < text.size()
                                ? QChar::surrogateToUcs4(text[i], text[i + 1])
                                : char32_t(text[i].unicode());
        const int n = cp > 0xFFFF ? 2 : 1;
        const int a = m.horizontalAdvance(text.mid(i, n));
        r.adv.push_back(a);
        r.at.push_back(i);
        r.width += a;
        i += n;
    }
    return r;
}

}  // namespace

SkinFont skinControlFont(const std::string& family, const std::string& size, const std::string& style) {
    double pt = std::atof(size.c_str());
    if (!(pt > 0)) pt = 9;
    const int em = std::max(1, int(std::floor(pt * 4 / 3 + 0.5)));

    QStringList listed;
    for (QString f : QString::fromStdString(family).split(',')) {
        f = f.trimmed();
        if (f.size() >= 2 && (f.front() == '"' || f.front() == '\'') && f.back() == f.front())
            f = f.mid(1, f.size() - 2).trimmed();
        listed << f;
    }
    QString pick = listed.front();
    for (const QString& f : listed)
        if (isStockFace(f.toLower()) || available(f)) {
            pick = f;
            break;
        }
    QString l = pick.toLower();
    if (l.isEmpty() || l == "sans-serif") l = "ms sans serif";
    if (l == "monospace") l = "courier";

    bool bold = false;
    SkinFont out;
    QFont& f = out.font;
    for (const QString& t :
         QString::fromStdString(style).toLower().split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts)) {
        if (t == "bold")
            bold = true;
        else if (t == "italic")
            f.setItalic(true);
        else if (t == "underline")
            f.setUnderline(true);
        else if (t == "strike")
            f.setStrikeOut(true);
    }
    f.setBold(bold);

    const QStringList sans = twinsOf("arial");
    QStringList fams;
    int px = em;
    QFont::StyleHint hint = QFont::SansSerif;
    if (isStockFace(l)) {
        if (l == "courier") {
            fams = twinsOf("courier new");
            hint = QFont::Monospace;
        } else
            fams = sans;
    } else {
        for (const QStringList& part : {QStringList{pick}, twinsOf(l), sans})
            for (const QString& s : part)
                if (!fams.contains(s, Qt::CaseInsensitive)) fams << s;
    }
    f.setFamilies(fams);
    f.setStyleHint(hint);
    f.setPixelSize(px);
    f.setKerning(false);  // GDI sums advances without kerning
    return out;
}

int skinTextWidth(const SkinFont& f, const QString& text) { return runOf(f, text).width; }

QString skinEllipsized(const SkinFont& f, const QString& text, int width) {
    const Run run = runOf(f, text);
    if (run.width <= width) return text;
    const int dots = skinTextWidth(f, "...");
    int n = 0, w = 0;
    while (n < int(run.adv.size()) && w + run.adv[n] + dots <= width) w += run.adv[n++];
    return text.left(run.at[n]) + "...";
}

void drawSkinText(QPainter& p, const QRect& r, const SkinFont& f, const QString& text, bool ellipsis,
                  Qt::Alignment align) {
    const QString s = ellipsis ? skinEllipsized(f, text, r.width()) : text;
    const Run run = runOf(f, s);
    const QFontMetrics m(f.font);
    const int height = m.height();
    const int ascent = m.ascent();
    const int x0 = align & Qt::AlignLeft    ? r.left()
                   : align & Qt::AlignRight ? r.right() + 1 - run.width
                                            : r.left() + (r.width() - run.width) / 2;
    const int top = align & Qt::AlignTop      ? r.top()
                    : align & Qt::AlignBottom ? r.bottom() + 1 - height
                                              : r.top() + (r.height() - height) / 2;
    const int base = top + ascent;
    p.save();
    p.setClipRect(r, Qt::IntersectClip);
    p.setFont(f.font);
    p.drawText(QPoint(x0, base), s);
    p.restore();
}

Qt::Alignment skinLabelAlign(const std::string& v) {
    const QString a = QString::fromStdString(v).trimmed().toLower();
    Qt::Alignment h = Qt::AlignHCenter, vert = Qt::AlignVCenter;
    if (a.contains("left"))
        h = Qt::AlignLeft;
    else if (a.contains("right"))
        h = Qt::AlignRight;
    if (a.contains("top"))
        vert = Qt::AlignTop;
    else if (a.contains("bottom"))
        vert = Qt::AlignBottom;
    return h | vert;
}

QFont skinMenuFont() {
    QFont f;
    f.setFamilies(QStringList{"Tahoma"} + twinsOf("arial"));
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(11);
    return f;
}

void SkinButton::paintEvent(QPaintEvent*) {
    QStylePainter p(this);
    QStyleOptionButton o;
    initStyleOption(&o);
    const QString t = o.text;
    o.text.clear();
    p.drawControl(QStyle::CE_PushButton, o);
    QRect r = style()->subElementRect(QStyle::SE_PushButtonContents, &o, this);
    if (o.state & (QStyle::State_On | QStyle::State_Sunken))
        r.translate(style()->pixelMetric(QStyle::PM_ButtonShiftHorizontal, &o, this),
                    style()->pixelMetric(QStyle::PM_ButtonShiftVertical, &o, this));
    p.setPen(palette().color(QPalette::ButtonText));
    drawSkinText(p, r, sf_, t, false);
}

void SkinLabel::paintEvent(QPaintEvent* e) {
    QFrame::paintEvent(e);
    QPainter p(this);
    p.setPen(palette().color(foregroundRole()));
    drawSkinText(p, contentsRect(), sf_, text(), true, alignment());
}

}  // namespace fugue::shell
