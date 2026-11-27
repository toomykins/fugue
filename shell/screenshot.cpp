#include "shell/screenshot.h"
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>

namespace fugue::shell {

QString screenshotBaseName(const QString& worldName, const QDateTime& when) {
    // The Windows-reserved set. Stock DreamSeeker drops these characters rather than replacing them (see screenshot.h).
    static const QString bad = QStringLiteral("\\/:*?\"<>|");
    QString n;
    for (QChar c : worldName)
        if (!bad.contains(c) && c.unicode() >= 0x20) n += c;
    n = n.trimmed();
    if (n.isEmpty()) n = QStringLiteral("screenshot");
    return n + ' ' + when.toString(QStringLiteral("yyyy-MM-dd HHmmss"));
}

QString screenshotAutoPath(const QString& dir, const QString& worldName, const QDateTime& when) {
    QString base = QDir(dir).filePath(screenshotBaseName(worldName, when));
    QString p = base + ".png";
    for (int i = 2; QFileInfo::exists(p); i++) p = base + ' ' + QString::number(i) + ".png";
    return p;
}

QString withPngSuffix(const QString& path) {
    if (path.isEmpty() || !QFileInfo(path).suffix().isEmpty()) return path;
    return path + ".png";
}

bool saveScreenshot(const QImage& frame, const QString& path, QString* err) {
    if (err) err->clear();
    auto fail = [&](const QString& why) {
        if (err) *err = why;
        return false;
    };
    if (frame.isNull()) return fail(QStringLiteral("no map frame to capture"));
    if (path.isEmpty()) return fail(QStringLiteral("no screenshot destination"));
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(QStringLiteral("cannot create ") + QFileInfo(path).absolutePath());
    QImageWriter w(path, "png");
    if (!w.write(frame.convertToFormat(QImage::Format_RGB888))) return fail(w.errorString());
    return true;
}

}  // namespace fugue::shell
