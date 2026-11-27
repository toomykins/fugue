// menulist: which atoms of a right-click stack get a menu row, against the part B rows MEASURED on stock DreamSeeker
// (data/popupmenu-world/dreamseeker-rightclick.tsv): only atoms with an eligible verb are listed, and a stack with none
// opens no menu.
#include <QFile>
#include <QtTest>
#include "tests/testmain.h"

#include <map>
#include <string>
#include <vector>

#include "model/verbmenu.h"

using fugue::model::listedMenuAtoms;

namespace {
struct Row {
    QString label, target, menu;
};

std::vector<Row> partB() {
    QFile f(QString(FUGUE_TEST_DATA_DIR) + "/popupmenu-world/dreamseeker-rightclick.tsv");
    if (!f.open(QIODevice::ReadOnly)) qFatal("cannot open %s", qPrintable(f.fileName()));
    std::vector<Row> rows;
    bool inB = false;
    for (const QByteArray& raw : f.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith("# part ")) inB = line.startsWith("# part B");
        if (!inB || line.isEmpty() || line.startsWith('#')) continue;
        const QStringList c = line.split('\t');
        if (c.size() == 3) rows.push_back({c[0], c[1], c[2]});
    }
    return rows;
}

struct Entry {
    const char* name;
    size_t verbs;
};
// The atoms under each target, topmost first, with how many eligible verbs each has (the rocks have one; the turfs and
// the static have none).
const std::map<std::string, std::vector<Entry>> kStack = {
    {"NB", {{"t_B", 0}}},
    {"SB", {{"obscured", 0}, {"t_B", 0}}},
    {"NR", {{"rock_B", 1}, {"t_B", 0}}},
    {"SR", {{"obscured", 0}, {"rock_A", 1}, {"t_A", 0}}},
};
}  // namespace

class MenuList : public QObject {
    Q_OBJECT
private slots:
    void stockRows() {
        const std::vector<Row> rows = partB();
        QCOMPARE(int(rows.size()), 4);
        for (const Row& r : rows) {
            const auto& stack = kStack.at(r.target.toStdString());
            std::vector<size_t> counts;
            for (const Entry& e : stack) counts.push_back(e.verbs);
            const std::vector<size_t> idx = listedMenuAtoms(counts);
            QStringList listed;
            for (size_t i : idx) listed << stack[i].name;
            const QString want = r.menu == "-" ? QString() : r.menu;
            QCOMPARE(listed.join(","), want);
            QCOMPARE(idx.empty(), want.isEmpty());
        }
    }

    void keepsOrderAndSkipsEmpty() {
        QCOMPARE(listedMenuAtoms({0, 2, 0, 1}), (std::vector<size_t>{1, 3}));
        QVERIFY(listedMenuAtoms({}).empty());
    }
};

FUGUE_TEST_APPLESS_MAIN(MenuList)
#include "menulist_test.moc"
