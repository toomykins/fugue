// macOS delivers registered URL schemes as QFileOpenEvent, not command-line arguments.
#pragma once
#include <QCoreApplication>
#include <QFileOpenEvent>
#include <QTimer>
#include <functional>

namespace fugue::shell {
class UrlOpener : public QObject {
public:
    explicit UrlOpener(QObject* owner, std::function<void(const QUrl&)> open) : QObject(owner), open_(std::move(open)) {
        QCoreApplication::instance()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() != QEvent::FileOpen) return false;
        const QUrl url = static_cast<QFileOpenEvent*>(event)->url();
        if (url.scheme().compare("byond", Qt::CaseInsensitive) != 0) return false;
        // Run after startup/any native event dispatch, with owner lifetime protection.
        QTimer::singleShot(0, this, [this, url] { open_(url); });
        event->accept();
        return true;
    }

private:
    std::function<void(const QUrl&)> open_;
};
}  // namespace fugue::shell
