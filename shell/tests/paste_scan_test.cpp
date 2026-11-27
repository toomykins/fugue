// Source scan: every editable widget constructed in shell/ or app/ (QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox,
// QDoubleSpinBox, or a read-only text widget made editable) must be registered with shell/paste.h's wirePaste() in the same
// statement or within the next lines.
//
// The scan is checked before it is trusted: scanSource() must flag an unregistered snippet, and the tree scan must find a minimum
// number of registrations, so a regex that matches nothing cannot pass.
//
// Exempt: a BrowserPane (QWebEngineView), since Chromium owns its keys and pastes natively. No file may install a paste filter on a web view.
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QtTest>
#include "tests/testmain.h"

namespace {
struct Finding {
    QString file;
    int line;
    QString text;
};

// Returns the (1-based) lines that construct an editable widget without registering it.
QList<int> scanSource(const QString& src, int* registered = nullptr) {
    static const QRegularExpression ctor(
        R"((?:\bnew\s+(?:QLineEdit|QTextEdit|QPlainTextEdit|QComboBox|QSpinBox|QDoubleSpinBox)\b)|(?:^\s*(?:QLineEdit|QTextEdit|QPlainTextEdit|QComboBox|QSpinBox|QDoubleSpinBox)\s+\w+\s*(?:;|\(|\{|=)))");
    static const QRegularExpression readOnlyOff(R"(setReadOnly\(\s*false\s*\))");
    const QStringList lines = src.split('\n');
    QList<int> bad;
    for (int i = 0; i < lines.size(); i++) {
        const QString& l = lines[i];
        if (l.trimmed().startsWith("//")) continue;
        if (!ctor.match(l).hasMatch() && !readOnlyOff.match(l).hasMatch()) continue;
        bool ok = l.contains("wirePaste(");
        for (int j = i + 1; !ok && j < qMin(lines.size(), i + 13); j++) ok = lines[j].contains("wirePaste(");
        if (ok) {
            if (registered) (*registered)++;
        } else
            bad << i + 1;
    }
    return bad;
}

QStringList sourcesUnder(const QString& dir) {
    QStringList out;
    QDirIterator it(dir, {"*.cpp", "*.h"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString p = it.next();
        if (p.contains("/tests/") || p.endsWith("/paste.cpp") || p.endsWith("/paste.h"))
            continue;  // the helper itself and the tests
        out << p;
    }
    out.sort();
    return out;
}
QString slurp(const QString& p) {
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}
}  // namespace

class PasteScan : public QObject {
    Q_OBJECT
private slots:
    void theScanFlagsWhatItShould() {  // the scan must be able to fail
        QCOMPARE(scanSource("auto* le = new QLineEdit(this);\nle->setText(\"x\");\n"), QList<int>{1});
        QCOMPARE(scanSource("auto* e = new QPlainTextEdit(parent);\n").size(), 1);
        QCOMPARE(scanSource("QLineEdit field;\n").size(), 1);
        QCOMPARE(scanSource("auto* cb = new QComboBox(d);\ncb->setEditable(true);\n").size(), 1);
        QCOMPARE(scanSource("tb->setReadOnly(false);\n").size(), 1);
        int reg = 0;
        QCOMPARE(scanSource("auto* le = wirePaste(new QLineEdit(this));\n", &reg).size(), 0);
        QCOMPARE(reg, 1);
        QCOMPARE(
            scanSource(
                "auto* le = new QLineEdit(this);\nconnect(le, &QLineEdit::returnPressed, this, f);\nwirePaste(le);\n")
                .size(),
            0);
        QCOMPARE(scanSource("// new QLineEdit(this) in a comment is not a widget\n").size(), 0);
        QCOMPARE(scanSource("QLineEdit* p = nullptr;\nclass QLineEdit;\n").size(),
                 0);  // a pointer / forward declaration constructs nothing
        // registration far away (more than a statement later) does not count: it is a different widget's
        QString far = "auto* le = new QLineEdit(this);\n" + QString("x;\n").repeated(20) + "wirePaste(other);\n";
        QCOMPARE(scanSource(far).size(), 1);
    }
    void everyEditableInShellAndAppIsWired() {
        const QString root = FUGUE_SOURCE_ROOT;
        int registered = 0;
        QStringList failures;
        QStringList files = sourcesUnder(root + "/shell") + sourcesUnder(root + "/app");
        QVERIFY2(files.size() > 10, "the scan found no sources: wrong root?");
        for (const QString& f : files)
            for (int line : scanSource(slurp(f), &registered))
                failures << QString("%1:%2").arg(QDir(root).relativeFilePath(f)).arg(line);
        QVERIFY2(failures.isEmpty(), qPrintable("editable widgets NOT registered with shell/paste.h wirePaste():\n  " +
                                                failures.join("\n  ")));
        QVERIFY2(registered >= 5, qPrintable(QString("only %1 registered constructions found (INPUT bar + prompt "
                                                     "fields expected): the scan may be blind")
                                                 .arg(registered)));
        qInfo("paste scan: %d editable construction(s) across %d files, all registered", registered, int(files.size()));
    }
    void aWebViewIsNeverFilteredByUs() {  // Chromium owns its keys; a paste filter on a page would fight it
        const QString root = FUGUE_SOURCE_ROOT;
        for (const QString& f : sourcesUnder(root + "/shell") + sourcesUnder(root + "/app")) {
            const QString s = slurp(f);
            QVERIFY2(!s.contains(QRegularExpression(
                         R"(wirePaste(?:Widget)?\([^;]*(?:BrowserPane|QWebEngineView|view_|page_))")),
                     qPrintable(f + " wires paste onto a web view"));
        }
    }
    void pagerWindowGapsAreReportedNotFailed() {  // pager/ui is reported, not enforced
        const QString root = FUGUE_SOURCE_ROOT;
        QStringList gaps;
        for (const QString& f : sourcesUnder(root + "/pager/ui"))
            for (int line : scanSource(slurp(f)))
                gaps << QString("%1:%2").arg(QDir(root).relativeFilePath(f)).arg(line);
        if (!gaps.isEmpty())
            qWarning("pager/ui: unregistered editable widgets (not enforced): %s", qPrintable(gaps.join(", ")));
    }
};

FUGUE_TEST_APPLESS_MAIN(PasteScan)
#include "paste_scan_test.moc"
