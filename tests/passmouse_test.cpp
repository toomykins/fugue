// passmouse: the mouse over render_source relays, vg's AI camera static (an /image with render_source "*aistatic" and
// PASS_MOUSE on each obscured turf; the source a client.screen obj whose MouseEntered turns show_popup_menus off).
// Scene: tests/data/passmouse-world (world.dm lists the variants), recorded from stock DreamDaemon. Graded against what
// stock DreamSeeker 516 sent at 24 pixels (dreamseeker-mouse.tsv): the atom a click names, its location and params, and
// the right-click menu. Through PASS_MOUSE the relayed pixels name the source, so the hover reaches the source and vg's
// rule can take the menu away; without it they name the image's turf.
#include <QtTest>
#include <fstream>
#include <sstream>
#include "model/world.h"
#include "net/bytes.h"
#include "proto/enc_c2s_0002_input.h"
#include "render/hittest.h"
#include "render/mousechain.h"
#include "render/scene.h"

using namespace fugue;

namespace {
struct Row {
    std::string label, atom, loc, params, menu;
    int x, y;
};

std::string dir() { return std::string(FUGUE_TEST_DATA_DIR) + "/passmouse-world"; }

Bytes unhex(const std::string& h) {
    Bytes b;
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(uint8_t(v(h[i]) << 4 | v(h[i + 1])));
    return b;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string cell;
    while (std::getline(ss, cell, sep)) out.push_back(cell);
    if (!s.empty() && s.back() == sep) out.push_back("");
    return out;
}
}  // namespace

class PassMouse : public QObject {
    Q_OBJECT
    model::Session sess_;
    render::ResStore res_;
    std::shared_ptr<const render::HitScene> sc_;
    std::vector<Row> rows_;

    // What a click at the row's pixel names: the atom, its location ("" = null, else the turf's name) and the params.
    void named(const Row& r, std::string& atom, std::string& loc, std::string& params) const {
        render::Naming n;
        if (!render::nameMouse(*sc_, r.x, sc_->pixH() - 1 - r.y, n)) return;
        atom = n.hit.name;
        if (!n.locNull)
            for (const render::Hit& h : render::hitStack(*sc_, r.x, sc_->pixH() - 1 - r.y))
                if (h.kind == render::kAtomTurf) loc = h.name;
        params = proto::enc::clickParams(render::clickCmdFrom(n, 1, {}, "mapwindow.map"));
        const std::string lb = "left=1;button=left;";
        if (size_t at = params.find(lb); at != std::string::npos) params.erase(at, lb.size());
    }

private slots:
    void initTestCase() {
        std::ifstream f(dir() + "/frames.txt");
        QVERIFY(f);
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("0x", 0) != 0) continue;
            size_t sp = line.find(' ', line.find("len="));
            sess_.ingest(uint16_t(std::stoul(line.substr(2, 4), nullptr, 16)),
                         sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1)));
        }
        for (const char* id : {"4946a4f8", "08bcd326"}) {
            std::ifstream rf(dir() + "/res/" + id, std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
            Bytes b(bytes.begin(), bytes.end());
            uint32_t claimed = uint32_t(std::stoul(id, nullptr, 16));
            std::string err;
            QVERIFY2(res_.add(b, &claimed, &err), err.c_str());
            sess_.noteResource(claimed, b, "");
        }
        render::DrawList dl;
        render::SceneStats st;
        std::string err;
        QVERIFY2(render::assembleMapControl(sess_, res_, 0, dl, st, &err, true), err.c_str());
        sc_ = dl.hit;
        QVERIFY(sc_);
        QCOMPARE(sc_->pixW(), 224);
        QCOMPARE(sc_->pixH(), 224);

        std::ifstream tf(dir() + "/dreamseeker-mouse.tsv");
        QVERIFY(tf);
        while (std::getline(tf, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto c = split(line, '\t');
            QVERIFY2(c.size() == 7, line.c_str());
            rows_.push_back({c[0], c[3], c[4], c[5], c[6], std::stoi(c[1]), std::stoi(c[2])});
        }
        QCOMPARE(int(rows_.size()), 24);
    }

    // The atom a click names, where it is, and icon-x/icon-y/vis-x/vis-y/screen-loc, exactly as stock sent them.
    void clicksNameWhatStockNames() {
        int bad = 0;
        for (const Row& r : rows_) {
            std::string atom, loc, params;
            named(r, atom, loc, params);
            if (atom == r.atom && loc == r.loc && params == r.params) continue;
            bad++;
            qWarning("%s: ours %s loc=%s %s | stock %s loc=%s %s", r.label.c_str(), atom.c_str(), loc.c_str(),
                     params.c_str(), r.atom.c_str(), r.loc.c_str(), r.params.c_str());
        }
        QCOMPARE(bad, 0);
    }

    // The right-click stack: a relay holds its whole rectangle, for the source with PASS_MOUSE and for its turf without.
    void menusListWhatStockLists() {
        int bad = 0, graded = 0;
        for (const Row& r : rows_) {
            if (r.menu == "-") continue;
            graded++;
            std::string ours;
            for (const render::Hit& h : render::hitStack(*sc_, r.x, sc_->pixH() - 1 - r.y))
                ours += (ours.empty() ? "" : "|") + h.name;
            if (ours == r.menu) continue;
            bad++;
            qWarning("%s: ours %s | stock %s", r.label.c_str(), ours.c_str(), r.menu.c_str());
        }
        QCOMPARE(graded, 10);
        QCOMPARE(bad, 0);
    }

    // vg's static: the source is what the hover reaches (its MouseEntered is what turns the menu off), a screen object.
    void hoverReachesTheSource() {
        render::Hit h;
        QVERIFY(render::hitAt(*sc_, 8, sc_->pixH() - 1 - 87, h));
        QCOMPARE(QString::fromStdString(h.name), QString("obscured"));
        QVERIFY(h.screen && h.hover && h.kind == render::kAtomObj);
    }
};

QTEST_GUILESS_MAIN(PassMouse)
#include "passmouse_test.moc"
