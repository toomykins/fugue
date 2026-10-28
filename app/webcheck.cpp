// `fugue-qt webcheck`: starts Qt WebEngine, loads one page that runs a script and exits 0 once the script has run, 1 if it has
// not within the timeout. An installed package uses it to prove the WebEngine helper process, its resources and the sandbox work.
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <cstdio>

int webcheckMain(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    int rc = 1;
    QWebEngineView view;
    QObject::connect(view.page(), &QWebEnginePage::titleChanged, &app, [&](const QString& t) {
        if (t != "webcheck-42") return;
        rc = 0;
        app.quit();
    });
    QTimer::singleShot(60000, &app, [&] { app.quit(); });
    view.setHtml("<title>start</title><script>document.title = 'webcheck-' + 6 * 7;</script>");
    app.exec();
    std::printf(rc == 0 ? "webcheck ok\n" : "webcheck failed: the page never ran its script\n");
    return rc;
}
