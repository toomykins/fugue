#include "pager/ui/icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace fugue::pager::icons {
namespace {
QPixmap canvas(int px) {
    QPixmap p(px, px);
    p.fill(Qt::transparent);
    return p;
}
void aa(QPainter& g) { g.setRenderHint(QPainter::Antialiasing, true); }
QIcon iconOf(QPixmap (*draw)(int)) {
    QIcon i;
    for (int s : {16, 20, 24, 32}) i.addPixmap(draw(s));
    return i;
}
QPixmap drawHome(int px) {
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    const qreal s = px / 16.0;
    QPainterPath roof;
    roof.moveTo(1 * s, 8 * s);
    roof.lineTo(8 * s, 1.5 * s);
    roof.lineTo(15 * s, 8 * s);
    g.setPen(QPen(Qt::white, 1.8 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    g.drawPath(roof);
    g.setPen(Qt::NoPen);
    g.setBrush(Qt::white);
    g.drawRect(QRectF(3.5 * s, 8 * s, 9 * s, 6.5 * s));
    g.setBrush(QColor("#222"));
    g.drawRect(QRectF(7 * s, 10.5 * s, 2.5 * s, 4 * s));
    return p;
}
QPixmap drawGear(int px) {
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    const qreal c = px / 2.0, ro = px * 0.46, ri = px * 0.33;
    QPainterPath path;
    for (int i = 0; i < 16; i++) {
        qreal a = i * M_PI / 8, r = (i % 2 == 0) ? ro : ri;
        QPointF pt(c + r * qCos(a), c + r * qSin(a));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    path.closeSubpath();
    path.addEllipse(QPointF(c, c), px * 0.14, px * 0.14);
    path.setFillRule(Qt::OddEvenFill);
    g.setPen(Qt::NoPen);
    g.setBrush(Qt::white);
    g.drawPath(path);
    return p;
}
QPixmap drawPlus(int px) {
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    g.setPen(QPen(Qt::white, px * 0.16, Qt::SolidLine, Qt::RoundCap));
    g.drawLine(QPointF(px * 0.5, px * 0.2), QPointF(px * 0.5, px * 0.8));
    g.drawLine(QPointF(px * 0.2, px * 0.5), QPointF(px * 0.8, px * 0.5));
    return p;
}
QPixmap drawDetails(int px) {
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    g.setPen(QPen(Qt::white, px * 0.12, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 3; i++) {
        qreal y = px * (0.28 + 0.22 * i);
        g.drawLine(QPointF(px * 0.18, y), QPointF(px * 0.26, y));
        g.drawLine(QPointF(px * 0.38, y), QPointF(px * 0.82, y));
    }
    return p;
}
QPixmap drawGames(int px) {  // a joystick
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    g.setPen(Qt::NoPen);
    g.setBrush(Qt::white);
    g.drawRoundedRect(QRectF(px * 0.12, px * 0.62, px * 0.76, px * 0.26), px * 0.08, px * 0.08);
    g.drawRect(QRectF(px * 0.45, px * 0.32, px * 0.1, px * 0.32));
    g.setBrush(QColor("#e05050"));
    g.drawEllipse(QPointF(px * 0.5, px * 0.26), px * 0.15, px * 0.15);
    return p;
}
QPixmap drawFriends(int px) {
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    g.setPen(Qt::NoPen);
    g.setBrush(QColor("#bbbbbb"));
    for (qreal x : {0.3, 0.7}) {
        g.drawEllipse(QPointF(px * x, px * 0.3), px * 0.13, px * 0.13);
        g.drawRoundedRect(QRectF(px * (x - 0.17), px * 0.48, px * 0.34, px * 0.42), px * 0.1, px * 0.1);
    }
    return p;
}
}  // namespace

QPixmap gameIcon(const QString& name, int px) {
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    const auto h = qHash(name.toLower());
    QColor c = QColor::fromHsv(int(h % 360), 150, 190);
    QLinearGradient grad(0, 0, 0, px);
    grad.setColorAt(0, c.lighter(130));
    grad.setColorAt(1, c.darker(140));
    g.setPen(Qt::NoPen);
    g.setBrush(grad);
    g.drawRoundedRect(QRectF(0.5, 0.5, px - 1, px - 1), px * 0.22, px * 0.22);
    QString letter;
    for (QChar ch : name)
        if (ch.isLetterOrNumber()) {
            letter = ch.toUpper();
            break;
        }
    QFont f = g.font();
    f.setBold(true);
    f.setPixelSize(int(px * 0.68));
    g.setFont(f);
    g.setPen(Qt::white);
    g.drawText(QRectF(0, 0, px, px), Qt::AlignCenter, letter.isEmpty() ? QStringLiteral("?") : letter);
    return p;
}

QPixmap people(int px) {
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    g.setPen(Qt::NoPen);
    const QColor cs[2] = {QColor("#e070c0"), QColor("#9070e0")};
    for (int i = 0; i < 2; i++) {
        qreal x = px * (0.3 + 0.4 * i);
        g.setBrush(cs[i]);
        g.drawEllipse(QPointF(x, px * 0.22), px * 0.14, px * 0.14);
        g.drawRoundedRect(QRectF(x - px * 0.17, px * 0.4, px * 0.34, px * 0.58), px * 0.08, px * 0.08);
    }
    return p;
}

QPixmap lock(int px) {
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    g.setPen(QPen(QColor("#bbbbbb"), px * 0.12));
    g.setBrush(Qt::NoBrush);
    g.drawArc(QRectF(px * 0.28, px * 0.1, px * 0.44, px * 0.56), 0, 180 * 16);
    g.drawLine(QPointF(px * 0.28, px * 0.38), QPointF(px * 0.28, px * 0.5));
    g.drawLine(QPointF(px * 0.72, px * 0.38), QPointF(px * 0.72, px * 0.5));
    g.setPen(Qt::NoPen);
    g.setBrush(QColor("#bbbbbb"));
    g.drawRoundedRect(QRectF(px * 0.16, px * 0.46, px * 0.68, px * 0.48), px * 0.08, px * 0.08);
    return p;
}

QPixmap avatar(int px) {
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    g.fillRect(p.rect(), QColor("#d8d8d8"));
    g.setPen(Qt::NoPen);
    g.setBrush(QColor("#2a2a2a"));
    g.drawEllipse(QPointF(px * 0.5, px * 0.36), px * 0.17, px * 0.2);
    QPainterPath body;
    body.moveTo(px * 0.18, px);
    body.cubicTo(px * 0.18, px * 0.62, px * 0.82, px * 0.62, px * 0.82, px);
    body.closeSubpath();
    g.drawPath(body);
    return p;
}

QPixmap find(int px) {  // a magnifier over a star
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    g.setPen(QPen(QColor("#9fc3ff"), px * 0.1));
    g.setBrush(QColor(80, 130, 220, 90));
    g.drawEllipse(QRectF(px * 0.12, px * 0.12, px * 0.5, px * 0.5));
    g.setPen(QPen(QColor("#d0d0d0"), px * 0.14, Qt::SolidLine, Qt::RoundCap));
    g.drawLine(QPointF(px * 0.56, px * 0.56), QPointF(px * 0.86, px * 0.86));
    return p;
}

QPixmap recent(int px) {  // a clock face
    QPixmap p = canvas(px);
    QPainter g(&p);
    aa(g);
    g.setPen(QPen(QColor("#9fc3ff"), px * 0.09));
    g.setBrush(QColor("#1f3f80"));
    g.drawEllipse(QRectF(px * 0.08, px * 0.08, px * 0.84, px * 0.84));
    g.setPen(QPen(Qt::white, px * 0.08, Qt::SolidLine, Qt::RoundCap));
    g.drawLine(QPointF(px * 0.5, px * 0.5), QPointF(px * 0.5, px * 0.24));
    g.drawLine(QPointF(px * 0.5, px * 0.5), QPointF(px * 0.7, px * 0.6));
    return p;
}

QIcon home() { return iconOf(drawHome); }
QIcon gear() { return iconOf(drawGear); }
QIcon plus() { return iconOf(drawPlus); }
QIcon details() { return iconOf(drawDetails); }
QIcon games() { return iconOf(drawGames); }
QIcon friends() { return iconOf(drawFriends); }

}  // namespace fugue::pager::icons
