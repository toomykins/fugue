// hudstack: the right-click menu over stacked client.screen objects (vg's equipped item over its inventory slot, vg's
// full-screen click catcher) and map atoms, including BACKGROUND_LAYER and EFFECTS_LAYER on both. Scene:
// tests/data/hudstack-world (world.dm lists the cells), recorded from stock DreamDaemon. Graded against the menus stock
// DreamSeeker 516 opened at 71 pixels (dreamseeker-menus.tsv): every named atom with a layer whose rectangle holds the pixel,
// in draw order topmost first, and each atom's verbs.
#include <QtTest>
#include <fstream>
#include <sstream>
#include "model/verbmenu.h"
#include "model/world.h"
#include "net/bytes.h"
#include "render/hittest.h"
#include "render/scene.h"

using namespace fugue;

namespace {
struct Row {
    std::string label, atoms, verbs;
    int x, y;
};

constexpr const char* kThingDmi = "6f79d7e8";  // res/: thing.dmi by its resID
std::string dir() { return std::string(FUGUE_TEST_DATA_DIR) + "/hudstack-world"; }

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
    return out;
}
}  // namespace

class HudStack : public QObject {
    Q_OBJECT
    model::Session sess_;
    render::ResStore res_;
    std::shared_ptr<const render::HitScene> sc_;
    render::DrawList dl_;
    std::vector<Row> rows_;

    bool build() {
        std::ifstream f(dir() + "/frames.txt");
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("0x", 0) != 0) continue;
            uint16_t typ = uint16_t(std::stoul(line.substr(2, 4), nullptr, 16));
            size_t sp = line.find(' ', line.find("len="));
            sess_.ingest(typ, sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1)));
        }
        std::ifstream rf(dir() + "/res/" + kThingDmi, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
        Bytes b(bytes.begin(), bytes.end());
        uint32_t claimed = uint32_t(std::stoul(kThingDmi, nullptr, 16));
        std::string err;
        if (!res_.add(b, &claimed, &err)) return false;
        sess_.noteResource(claimed, b, "");
        render::SceneStats st;
        if (!render::assembleMapControl(sess_, res_, 0, dl_, st, &err, true)) {
            qWarning("assemble: %s", err.c_str());
            return false;
        }
        sc_ = dl_.hit;
        return sc_ != nullptr;
    }

    std::vector<render::Hit> stackAt(const Row& r) const { return render::hitStack(*sc_, r.x, sc_->pixH() - 1 - r.y); }

    std::string verbsOf(const render::Hit& h) const {
        const model::World& w = sess_.world();
        model::HitTarget ht = model::targetFromHit(w, h.kind, h.ref, h.worldX, h.worldY, h.screen);
        std::string out;
        for (const model::MenuVerb& mv : model::atomMenu(w, ht.appid, ht.target))
            out += (out.empty() ? "" : ",") + mv.verb.name;
        return out;
    }

private slots:
    void initTestCase() {
        QVERIFY(build());
        QCOMPARE(sc_->pixW(), 480);
        QCOMPARE(sc_->pixH(), 480);
        std::ifstream f(dir() + "/dreamseeker-menus.tsv");
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> c = split(line, '\t');
            if (c.size() != 5) continue;
            rows_.push_back({c[0], c[3], c[4], std::stoi(c[1]), std::stoi(c[2])});
        }
        QCOMPARE(int(rows_.size()), 71);
    }

    // The menu's rows. Screen and map atoms meet by plane and layer band, a screen object above the map layers of its own
    // plane and band; an unnamed atom (the click catcher) is left out; an atom whose own icon draws nothing holds its own
    // cell; overlays and client.images add their own rectangles, not the span between them.
    void atomsAgreeWithDreamSeeker() {
        int agree = 0;
        for (const Row& r : rows_) {
            std::string got;
            for (const render::Hit& h : stackAt(r)) got += (got.empty() ? "" : "|") + h.name;
            if (got == r.atoms)
                agree++;
            else
                qWarning("%s: DS %s | ours %s", r.label.c_str(), r.atoms.c_str(), got.c_str());
        }
        QCOMPARE(agree, int(rows_.size()));
    }

    // Each atom's submenu where DreamSeeker's was opened: the carried item has its own and the argument verbs; the slot
    // (on client.screen, not carried) only the runtime-list Examine.
    void verbsAgreeWithDreamSeeker() {
        int graded = 0, agree = 0;
        for (const Row& r : rows_) {
            std::vector<render::Hit> hits = stackAt(r);
            std::vector<std::string> want = split(r.verbs, '|');
            for (size_t i = 0; i < want.size() && i < hits.size(); i++) {
                if (want[i] == "-") continue;
                graded++;
                std::string got = verbsOf(hits[i]);
                if (got == want[i])
                    agree++;
                else
                    qWarning("%s %s: DS %s | ours %s", r.label.c_str(), hits[i].name.c_str(), want[i].c_str(),
                             got.c_str());
            }
        }
        QCOMPARE(graded, 9);
        QCOMPARE(agree, graded);
    }

    // The left click still names only the topmost atom where it paints: the item over its slot, the slot at the item's
    // transparent margin.
    void clickStaysTopmost() {
        render::Hit h;
        QVERIFY(render::hitAt(*sc_, 80, sc_->pixH() - 1 - 79, h));
        QCOMPARE(h.name, std::string("jumpsuit"));
        QVERIFY(render::hitAt(*sc_, 67, sc_->pixH() - 1 - 79, h));
        QCOMPARE(h.name, std::string("i_clothing"));
    }

    // Layer bands in the draw and the left click, as stock DreamSeeker's Click() log named them: a BACKGROUND_LAYER atom,
    // screen or map, under the floor; an EFFECTS_LAYER map atom over a regular screen object.
    void bandsDrawAndClick() {
        const struct {
            int x, y;
            const char* click;
            bool ink;
        } cases[] = {
            {240, 272, "floor", false},  // s_bg
            {304, 272, "floor", false},  // m_bg
            {432, 272, "floor", false},  // s_bg1 over m_bg2
            {304, 464, "floor", false},  // m_20000h
            {176, 272, "m_eff", true},   // m_eff over s_eff
            {368, 272, "s_e1", true},    // s_e1 over m_e2
            {48, 464, "s_l1", true},     // s_l1 over s_bgl
            {432, 464, "m_huge", true},  // m_huge over s_huge
            {368, 474, "s_neg", true},   // s_neg over the floor
        };
        render::RgbaImage img = render::execute(dl_);
        QCOMPARE(img.w, sc_->pixW());
        for (const auto& c : cases) {
            int py = sc_->pixH() - 1 - c.y;
            render::Hit h;
            QVERIFY(render::hitAt(*sc_, c.x, py, h));
            QCOMPARE(h.name, std::string(c.click));
            const uint8_t* p = &img.px[(size_t(py) * size_t(img.w) + size_t(c.x)) * 4];
            QCOMPARE(p[2] == 224, c.ink);  // the item's blue, else the floor's grey
        }
    }
};

QTEST_GUILESS_MAIN(HudStack)
#include "hudstack_test.moc"
