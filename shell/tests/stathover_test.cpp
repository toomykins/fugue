// Pointer hover over the stat panel (INFO control), against what stock DreamSeeker 516.1689 sent and its world logged
// (tests/data/hover-world panel.json, README "Stat panel hover"), and the panel widget's picks.
#include "tests/appearancejson.h"
#include <QtTest>
#include <QSet>
#include "tests/testmain.h"
#include "tests/recordedinput.h"
#include "model/statpanel.h"
#include "net/input.h"
#include "shell/mapinput.h"
#include "shell/mapvisibility.h"
#include "shell/namedmap.h"
#include "shell/statpanelwidget.h"
#include "shell/statview.h"
#include "shell/tests/replayfixture.h"
#include <QTableWidget>
using namespace fugue;
using namespace fugue::shell;

class StatHoverTest : public QObject {
    Q_OBJECT
private slots:
    // tests/data/hover-world panel.json: the real pointer moved over stock's map, second map and stat panel, with
    // clicks on stat rows; per action the hover procs the world logged and the hover reports stock sent. Each run is
    // replayed in order: the map picks through the real hit test, the panel picks through statHoverPointer at the
    // rows stock drew under the pointer, all into one MapHover under the MouseSubs and appearance flags DreamDaemon
    // sent stock (replay/panel.txt, replay/pboth.txt), and the server's rule (as in mapinput_test hoverLikeStock)
    // turns the reports into procs. The reports must be stock's byte for byte, and the procs those stock's world
    // logged. Cases over the grid, which Fugue does not have, and those whose keys or tab click change the panel are
    // left out (the widget test below holds when the panel picks again).
    void statHoverLikeStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/hover-world/panel.json";
        QJsonDocument document; QString error;
        QVERIFY2(testinput::readRecordedJson(path, document, error), qPrintable(error));
        // The atoms with all three hover procs; plain has none.
        const QSet<QString> procs{"both", "statrow", "bare", "swapa", "swapb", "other", "hud2", "island", "gridrow"};
        const QSet<QString> hoverProcs{"ENTER", "EXIT", "MOVE", "C-ENTER", "C-EXIT", "C-MOVE"};
        QSet<QString> runs; int actions = 0, reports = 0, lines = 0, panelReports = 0;
        for (const auto& runValue : document.object()["runs"].toArray()) {
            const auto run = runValue.toObject(); const auto name = run["run"].toString();
            runs.insert(name);
            const auto replay = run["replay"].toString();
            test::ReplayFixture f; net::InputModel im;
            QVERIFY2(f.load(replay, "hover-world/replay"), qPrintable(name + ": " + f.error));
            f.frame.geometry.visibility = std::make_shared<const VisibilityFrame>();
            QVERIFY(f.readFrames(replay, "hover-world/replay", [&](uint16_t type, const Bytes& body) { im.onFrame(type, body); }));
            QStringList switches; for (const auto& v : run["switches"].toArray()) switches.push_back(v.toString());
            const bool clientEnter = switches.contains("CLIENT_ENTER"), clientMove = switches.contains("CLIENT_MOVE");
            QCOMPARE(im.clientHover(), clientEnter || clientMove); QCOMPARE(im.clientMove(), clientMove);
            const auto& world = f.session.world();
            // The published panel, as the session builds it.
            StatThumbnails thumbnails;
            thumbnails.begin(world, [&](uint32_t id, QByteArray& out) { return f.read(id, out); }, 0);
            StatView view = buildStatView(world, thumbnails);
            view.generation = 1;
            QCOMPARE(view.wireTab, QByteArray("Probe"));
            QStringList labels; for (const auto& row : view.rows) labels << row.name;
            QCOMPARE(labels, (QStringList{"row", "", "plain", "bare", "text", "swap"}));
            NamedMap second("map2");
            const auto map2 = second.prepare(world, [&](uint32_t id, QByteArray& out) { return f.read(id, out); }, 0, 0);
            auto appearanceName = [&](uint32_t appid) {
                const auto ap = world.appearances.find(appid);
                return ap == world.appearances.end() ? QString() : QString::fromStdString(model::plainAtomName(
                    model::parseJson(fieldsJson(ap->second))["name"].bytesOrStr()));
            };
            auto nameOf = [&](const MapPointer& p) {
                return p.info ? appearanceName(p.infoAppid) : QString::fromStdString(f.targetName(p));
            };
            auto flagsOf = [&](const MapPointer* p) -> uint32_t {
                std::optional<uint32_t> appid;
                if (p && p->info) appid = p->infoAppid;
                else if (const auto target = p ? mapPointerTarget(*p, world) : std::nullopt) appid = target->appid;
                const auto a = appid ? world.appearances.find(*appid) : world.appearances.end();
                return a == world.appearances.end() ? 0 : a->second.fields.appearanceFlags;
            };
            // 1: the map, 2: map2, 3: the stat panel's rows (below its tab strip), 4: the grid, 0: anywhere else.
            auto region = [](QPoint q) {
                return q.x() >= 0 && q.y() >= 0 && q.x() < 288 && q.y() < 224 ? 1 :
                       q.x() >= 300 && q.y() >= 0 && q.x() < 396 && q.y() < 32 ? 2 :
                       q.x() >= 300 && q.y() >= 63 && q.x() < 600 && q.y() < 330 ? 3 :
                       q.x() < 288 && q.y() >= 232 && q.y() < 328 ? 4 : 0;
            };
            // Stock's panel for this compile, in window pixels (its screenshot, and the icon-x/icon-y it sent): rows by
            // y, the value column from x 346, an atom row taking the pointer to x 429 (statrow's text ends between 400
            // and 500, bare's between 420 and 440), icons 32 pixels from x 346 with these bottom rows.
            const int rowTop[] = {63, 97, 130, 163, 182, 200, 234};
            const int iconBottom[] = {95, 128, 161, 0, 0, 232};
            auto statPick = [&](QPoint q) {
                StatHover h; h.control = "main.info"; h.generation = view.generation;
                for (int r = 0; r < 6; ++r)
                    if (q.y() >= rowTop[r] && q.y() < rowTop[r + 1] && q.x() >= 346 && q.x() < 430 && view.rows[r].clickable) {
                        h.row = r;
                        h.icon = !view.rows[r].thumbnail.isNull();
                        if (h.icon) { h.iconX = q.x() - 346 + 1; h.iconY = iconBottom[r] - q.y() + 1; }
                    }
                return statHoverPointer(world, view, h, 0);
            };
            auto pick = [&](QPoint q) {
                std::optional<MapPointer> p;
                if (region(q) == 1)
                    p = mapPointerAt(QPointF(q), {288, 224}, {7, 7}, f.frame.source, f.frame.geometry, "main.map", true, f.sprites);
                else if (region(q) == 2)
                    p = mapPointerAt(QPointF(q - QPoint(300, 0)), {96, 32}, map2.terrain.geometry.region.size(),
                                     map2.terrain.source, map2.terrain.geometry, "main.map2", false, map2.sprites);
                else if (region(q) == 3)
                    p = statPick(q);
                auto picked = p ? *p : MapPointer{};
                picked.click.button.clear();
                return picked;
            };
            auto paramsOf = [](const MapPointer& p) {
                return QString::fromStdString(p.info ? proto::enc::infoHoverParams(p.click.mods, p.infoIcon, p.click.iconX, p.click.iconY)
                                                     : proto::enc::hoverParams(p.click));
            };
            // The server: what it was last told the pointer is over, and the params it entered with.
            struct Stored { QString name, params; proto::enc::AtomRef over; };
            std::optional<Stored> stored; QStringList logged, sent;
            auto exit = [&] {
                if (!stored) return;
                if (clientEnter) logged << "C-EXIT " + stored->name + " " + stored->params;
                if (procs.contains(stored->name)) logged << "EXIT " + stored->name + " " + stored->params;
                stored.reset();
            };
            auto same = [&](const MapPointer& p) {
                return stored && stored->over.kind == p.click.over.kind && stored->over.ref == p.click.over.ref &&
                       stored->over.x == p.click.over.x && stored->over.y == p.click.over.y;
            };
            MapHover hover; int64_t now = 0;
            auto flush = [&](int64_t ns) {
                now += ns;
                for (auto body = hover.due(now); !body.empty(); body = hover.due(now)) {
                    ++reports;
                    sent << QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(body.data()), int(body.size())).toHex());
                    const auto* last = hover.sent();
                    if (last && last->info) ++panelReports;
                    if (!last) { exit(); continue; }
                    const auto atom = nameOf(*last), params = paramsOf(*last);
                    if (same(*last)) {
                        if (clientMove) logged << "C-MOVE " + atom + " " + params;
                        if (procs.contains(atom)) logged << "MOVE " + atom + " " + params;
                    } else {
                        exit();
                        if (clientEnter) logged << "C-ENTER " + atom + " " + params;
                        if (procs.contains(atom)) logged << "ENTER " + atom + " " + params;
                        stored = Stored{atom, params, last->click.over};
                    }
                }
            };
            auto report = [&](MapPointer p) {
                p.kind = MapPointer::Hover;
                const MapHover::Gate gate{im.clientHover(), im.clientMove(), flagsOf(&p), flagsOf(hover.last())};
                hover.update(p, p.info || mapPointerCurrent(p, world, 0), gate, now);
            };
            auto leave = [&] {
                MapPointer p; p.kind = MapPointer::Leave;
                hover.update(p, true, {im.clientHover(), im.clientMove(), 0, flagsOf(hover.last())}, now);
            };
            QPoint at(-80, 112);
            auto moveTo = [&](QPoint q) {
                if (q == at) return;
                const int from = region(at), to = region(q);
                at = q;
                if (to) report(pick(q));
                if (from && from != to) leave();   // after the control reached has reported
            };
            auto point = [](const QString& action) -> std::optional<QPoint> {
                const auto xy = action.split(',');
                if (xy.size() != 2 || action.contains(':')) return {};
                return QPoint(xy[0].toInt(), xy[1].toInt());
            };
            for (const auto& caseValue : run["cases"].toArray()) {
                const auto row = caseValue.toObject(); const auto label = name + "/" + row["case"].toString();
                bool skip = false; QPoint q(-80, 112);
                for (const auto& entry : row["actions"].toArray()) {
                    const auto action = entry.toObject()["action"].toString();
                    if (const auto p = point(action)) q = *p;
                    skip |= action == "key:f2" || action == "key:f3" || (action == "tap" && region(q) != 3) ||
                            region(q) == 4 || action.startsWith("post:") || action.startsWith("sweep:");
                }
                if (skip) { hover = MapHover{}; stored.reset(); at = QPoint(-80, 112); continue; }
                moveTo(QPoint(-80, 112)); flush(1'000'000'000); logged.clear(); sent.clear();
                for (const auto& entry : row["actions"].toArray()) {
                    const auto action = entry.toObject()["action"].toString();
                    if (const auto p = point(action)) {
                        moveTo(*p);
                    } else if (action == "tap") {
                        hover.released();   // a click on a stat row (the session's StatRowJob)
                    } else if (action.startsWith("wait:")) {
                        now += int64_t(action.mid(5).toDouble() * 1e9);
                    } else {
                        // F4 adds a text row below the others: under a still pointer it is empty space, as before.
                        QVERIFY2(action.startsWith("shot:") || action == "key:f4", qPrintable(label + ": not modelled: " + action));
                    }
                    flush(action.startsWith("shot:") ? 50'000'000 : 350'000'000);
                    QStringList stock, stockSent;
                    for (const auto& line : entry.toObject()["logged"].toArray()) {
                        const auto words = line.toString().split(' ');
                        if (words.size() >= 4 && hoverProcs.contains(words[1])) stock << words[1] + " " + words[2] + " " + words.last();
                    }
                    for (const auto& body : entry.toObject()["sent"].toArray())
                        if (body.toString().startsWith("08")) stockSent << body.toString();
                    QVERIFY2(logged == stock, qPrintable(QString("%1 %2: stock [%3], got [%4]")
                                                          .arg(label, action, stock.join("; "), logged.join("; "))));
                    QVERIFY2(sent == stockSent, qPrintable(QString("%1 %2: stock sent [%3], got [%4]")
                                                            .arg(label, action, stockSent.join(" "), sent.join(" "))));
                    lines += int(stock.size()); logged.clear(); sent.clear(); ++actions;
                }
            }
        }
        QCOMPARE(runs, (QSet<QString>{"panel1", "panel", "panel2", "pboth"}));
        QVERIFY2(actions == 101 && reports == 42 && lines == 48 && panelReports == 23,
                 qPrintable(QString("%1 actions, %2 reports (%3 over the panel), %4 procs")
                            .arg(actions).arg(reports).arg(panelReports).arg(lines)));
    }

    // The panel widget's picks: an atom row is its icon and name text; its label, past its text and a text row are the
    // panel's empty space; a row without an icon takes the pointer about an icon's width past its text. The leave comes
    // after the event in hand. Another tab shown under a still pointer is picked again once its rows are there; rows
    // changing under it are not (stock 516.1689, tests/data/hover-world panel.json).
    void statPanelHoverPicks() {
        StatPanelWidget w;
        w.resize(500, 240);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.setControlId("main.info");
        StatView v;
        v.tabs = {"Probe", "Other"};
        v.wireTab = "Probe";
        v.generation = 3;
        v.rows = {{"row", "statrow", true}, {"text", "just text", false}, {"bare", "bare", true}};
        v.rows[0].thumbnail = QImage(32, 32, QImage::Format_ARGB32);
        v.rows[0].thumbnail.fill(Qt::red);
        v.rows[0].identity = 1; v.rows[2].identity = 2;
        w.setView(v);
        QSignalSpy hovered(&w, &StatPanelWidget::rowHovered), left(&w, &StatPanelWidget::hoverLeft);
        auto* viewport = w.table()->viewport();
        auto cell = [&](int r, int c) { return w.table()->visualRect(w.table()->model()->index(r, c)); };
        auto move = [&](QPoint at) {
            QMouseEvent e(QEvent::MouseMove, QPointF(at), viewport->mapToGlobal(QPointF(at)), Qt::NoButton, Qt::NoButton,
                          Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &e);
            return hovered.isEmpty() ? QList<QVariant>{-99} : hovered.takeLast();
        };
        const QRect value = cell(0, 1);
        const QPoint icon(value.left() + 2, value.top() + (value.height() - 32) / 2);
        auto onIcon = move(icon + QPoint(4, 15));
        QCOMPARE(onIcon[0].toInt(), 0);
        QCOMPARE(onIcon[1].toString(), QString("main.info"));
        QCOMPARE(onIcon[3].toULongLong(), quint64(3));
        QVERIFY(onIcon[4].toBool());
        QCOMPARE(onIcon[5].toInt(), 5);
        QCOMPARE(onIcon[6].toInt(), 17);
        const int textLeft = icon.x() + 32 + 4;
        QCOMPARE(move(QPoint(textLeft + 3, value.center().y()))[0].toInt(), 0);            // the name text
        QCOMPARE(move(QPoint(cell(0, 0).center()))[0].toInt(), -1);                        // the label
        QCOMPARE(move(QPoint(value.right() - 2, value.center().y()))[0].toInt(), -1);      // past the text
        QCOMPARE(move(cell(1, 1).center() - QPoint(cell(1, 1).width() / 2 - 4, 0))[0].toInt(), -1);  // a text row
        const QRect bare = cell(2, 1);
        const int bareText = QFontMetrics(w.table()->font()).horizontalAdvance("bare");
        auto onBare = move(QPoint(bare.left() + 3, bare.center().y()));
        QCOMPARE(onBare[0].toInt(), 2);
        QVERIFY(!onBare[4].toBool());
        QCOMPARE(move(QPoint(bare.left() + bareText + 30, bare.center().y()))[0].toInt(), 2);
        QCOMPARE(move(QPoint(bare.left() + bareText + 60, bare.center().y()))[0].toInt(), -1);
        // Leaving: said once the events in hand are handled, and not when the pointer is back by then.
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(viewport, &leave);
        QCOMPARE(left.count(), 0);
        QCoreApplication::processEvents();
        QCOMPARE(left.count(), 1);
        QCoreApplication::sendEvent(viewport, &leave);
        move(icon + QPoint(4, 15));
        QCoreApplication::processEvents();
        QCOMPARE(left.count(), 1);
        // Picked again for another tab, not for changed rows.
        QCursor::setPos(viewport->mapToGlobal(icon + QPoint(4, 15)));
        if (viewport->mapFromGlobal(QCursor::pos()) != icon + QPoint(4, 15)) QSKIP("the pointer cannot be placed here");
        hovered.clear();
        StatView swapped = v;
        swapped.generation = 4;
        swapped.rows[0].text = "swapped";
        swapped.rows[0].identity = 7;
        w.setView(swapped);
        QCOMPARE(hovered.count(), 0);
        StatView other = swapped;
        other.selected = 1; other.wireTab = "Other"; other.generation = 5; other.rows.clear();
        w.setView(other);
        QCOMPARE(hovered.count(), 0);   // no rows yet: the server has not sent the tab
        other.generation = 6;
        other.rows = {{"row", "other", true}};   // the same label: the icon stays where it was
        other.rows[0].thumbnail = v.rows[0].thumbnail;
        other.rows[0].identity = 9;
        w.setView(other);
        QCOMPARE(hovered.count(), 1);
        QCOMPARE(hovered[0][0].toInt(), 0);
        QCOMPARE(hovered[0][3].toULongLong(), quint64(6));
        other.generation = 7;
        other.rows[0].text = "changed";
        w.setView(other);
        QCOMPARE(hovered.count(), 1);
    }
};
FUGUE_TEST_MAIN(StatHoverTest)
#include "stathover_test.moc"
