#include "tests/appearancejson.h"
#include <QtTest>
#include "tests/testmain.h"
#include "tests/recordedinput.h"
#include <QBuffer>
#include <QPainter>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include "shell/entitymap.h"
#include "shell/animation.h"
#include "shell/namedmap.h"
#include "shell/compositionplan.h"
#include "shell/mappresentation.h"
#include "shell/mapmailbox.h"
#include "shell/clientoffset.h"
#include "shell/tests/iconfixture.h"
#include "shell/tests/affinefixture.h"
#include "shell/tests/opacityfixture.h"
#include "shell/tests/filtertypesfixture.h"
#include "shell/tests/roottreefixture.h"
#include "shell/localtargets.h"
#include "shell/tests/planesourcefixture.h"
#include "shell/tests/turflocalfixture.h"
using namespace fugue;

namespace {
QByteArray iconBytes() {
    QImage image(96, 32, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    for (int y = 0; y < 32; ++y) for (int x = 32; x < 64; ++x) image.setPixelColor(x, y, Qt::green);
    // "hollow": transparent but for a 2x2 dot in the middle.
    for (int y = 0; y < 32; ++y) for (int x = 64; x < 96; ++x)
        image.setPixelColor(x, y, x >= 79 && x <= 80 && y >= 15 && y <= 16 ? QColor(Qt::blue) : QColor(Qt::transparent));
    image.setPixelColor(0, 0, Qt::transparent);
    image.setText("Description", "width = 32\nheight = 32\nstate = \"red\"\ndirs = 1\nframes = 1\nstate = \"green\"\ndirs = 1\nframes = 1\n"
                                 "state = \"hollow\"\ndirs = 1\nframes = 1\n");
    QByteArray bytes;
    QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
    return bytes;
}
model::World world() {
    model::World w;
    w.bounds = {true, 20, 20, 0, 3, 3, 3, 3};
    w.resync.haveOrigin = true; w.resync.originX = w.resync.originY = 4;
    w.resources = {{123, 0}};
    auto& ap = w.appearances[1];
    setFields(ap, "{}"); ap.fields.iconResID = 0; ap.fields.iconState = "red"; ap.fields.layerBits = std::bit_cast<uint32_t>(3.0f);
    auto& atom = w.atoms[42];
    atom.hasPos = true; atom.x = atom.y = 6; atom.appid = 1; atom.haveKey = true; atom.key = 99;
    return w;
}
shell::TurfMap::Read reader() {
    return [bytes = iconBytes()](uint32_t, QByteArray& out) { out = bytes; return true; };
}
// What the placement tests below read from master-screen-loc-world: a phase's record, and a marker colour's
// bounds (left, top, right, bottom) in its 224x224 capture.
bool masterLocPhases(std::map<QString,QJsonObject>& phases, QString& error) {
    const QString path = QString(FUGUE_TEST_DATA_DIR) + "/master-screen-loc-world/stock-observations.json";
    QJsonDocument doc;
    if (!testinput::readRecordedJson(path, doc, error)) return false;
    for (const auto& value : doc.object()["phases"].toArray()) phases[value.toObject()["phase"].toString()] = value.toObject();
    return true;
}
bool masterLocBounds(const std::map<QString,QJsonObject>& phases, const QString& phase, const char* colour, QRect& out) {
    const auto found = phases.find(phase);
    if (found == phases.end() || found->second["crop"].toArray() != QJsonArray{0,0,224,224}) return false;
    const QJsonValue value = found->second["bounds"].toObject()[colour];
    if (!testinput::isIntegerTuple(value, 4, 0, 223)) return false;
    const auto b = value.toArray();
    out = QRect(QPoint(b[0].toInt(), b[1].toInt()), QPoint(b[2].toInt(), b[3].toInt()));
    return true;
}
bool samePixelsAndMetadata(const shell::MapSprites& a, const shell::MapSprites& b) {
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (a[i].image != b[i].image) return false;
        auto sprite = a[i]; sprite.image = b[i].image; // independent caches have different QImage identities
        if (!(sprite == b[i])) return false;
    }
    return true;
}
struct VisFixture {
    enum Icon { Kid, Holder, Floor, Remote, Thing, Npc, Wall };
    model::World w = world();
    std::map<uint32_t, QByteArray> bytes;
    static QImage box(QColor c, QRect r, char notch) {
        QImage im(32, 32, QImage::Format_RGBA8888); im.fill(Qt::transparent);
        QPainter p(&im); p.fillRect(r, c);
        const int mx = (r.left() + r.right()) / 2, my = (r.top() + r.bottom()) / 2;
        const QRect n = notch == 'S' ? QRect(mx - 1, r.bottom() - 2, 3, 3) : notch == 'N' ? QRect(mx - 1, r.top(), 3, 3) :
            notch == 'E' ? QRect(r.right() - 2, my - 1, 3, 3) : QRect(r.left(), my - 1, 3, 3);
        p.fillRect(n, Qt::white);
        return im;
    }
    static QByteArray dmi(const QList<QImage>& frames, const QString& description) {
        QImage sheet(32 * int(frames.size()), 32, QImage::Format_RGBA8888); sheet.fill(Qt::transparent);
        QPainter p(&sheet);
        for (int i = 0; i < frames.size(); ++i) p.drawImage(32 * i, 0, frames[i]);
        p.end(); sheet.setText("Description", description);
        QByteArray out; QBuffer buffer(&out); buffer.open(QIODevice::WriteOnly); sheet.save(&buffer, "PNG");
        return out;
    }
    static QByteArray boxes(QColor a, QColor b, QRect r) {
        QList<QImage> frames;
        for (QColor c : {a, b}) for (char n : {'S', 'N', 'E', 'W'}) frames << box(c, r, n);
        return dmi(frames, "width = 32\nheight = 32\nstate = \"a\"\ndirs = 4\nframes = 1\nstate = \"b\"\ndirs = 4\nframes = 1\n");
    }
    static QByteArray single(const std::function<void(QPainter&)>& draw) {
        QImage im(32, 32, QImage::Format_RGBA8888); im.fill(Qt::transparent);
        { QPainter p(&im); draw(p); }
        return dmi({im}, "width = 32\nheight = 32\nstate = \"\"\ndirs = 1\nframes = 1\n");
    }
    VisFixture() {
        w.atoms.clear(); w.appearances.clear(); w.resources.clear();
        bytes[100] = boxes({240, 120, 0}, {230, 40, 40}, {8, 8, 16, 16});
        bytes[101] = boxes({40, 80, 230}, {40, 200, 230}, {4, 4, 24, 24});
        bytes[102] = single([](QPainter& p) { p.fillRect(0, 0, 32, 32, QColor(16, 16, 16)); });
        bytes[103] = single([](QPainter& p) {
            p.fillRect(0, 0, 32, 32, QColor(200, 0, 200)); p.fillRect(1, 1, 30, 30, QColor(60, 20, 60));
            p.fillRect(1, 1, 6, 6, Qt::white);
        });
        bytes[104] = single([](QPainter& p) { p.fillRect(17, 17, 12, 12, QColor(40, 200, 60)); });
        bytes[105] = single([](QPainter& p) { p.fillRect(2, 12, 10, 18, QColor(240, 220, 40)); });
        bytes[106] = single([](QPainter& p) { p.fillRect(26, 0, 6, 32, QColor(120, 120, 120)); });
        for (uint32_t id = 100; id <= 106; ++id) w.resources.push_back({id, 0});
    }
    uint32_t look(Icon icon, const std::string& state, float layer, const std::string& fields = "{}") {
        const uint32_t id = uint32_t(w.appearances.size()) + 1;
        auto& ap = w.appearances[id];
        ap.fields.iconResID = icon; ap.fields.iconState = state; ap.fields.layerBits = std::bit_cast<uint32_t>(layer); setFields(ap, fields);
        return id;
    }
    uint32_t atom(uint32_t key, uint32_t appid, int x = 0, int y = 0) {
        auto& a = w.atoms[key]; a.appid = appid; a.renderIdentity = key + 1000; a.haveKey = true; a.key = uint16_t(key);
        if (x) { a.hasPos = true; a.x = x; a.y = y; }
        return key;
    }
    void vis(uint64_t holder, uint64_t identity, const std::vector<model::VisReference>& members) {
        QVERIFY(w.vis.replace(holder, identity, members, [&](uint32_t child) {
            const auto found = w.atoms.find(child); return found == w.atoms.end() ? uint64_t(0) : found->second.renderIdentity;
        }));
    }
    void floor(int x, int y, uint32_t appid) { w.tiles[{uint16_t(x - 1), uint16_t(y - 1), 0}] = appid; }
    uint32_t flat(int x, int y) const { return uint32_t((x - 1) + (y - 1) * w.bounds.maxX); }
    // The case tile's pixels, composited in draw order (tints and transforms do not occur here).
    // margin extends the composite past the tile's top-left by that many pixels on every side.
    QImage tile(int x, int y, shell::MapSprites* drawn = nullptr, int margin = 0) {
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w, {}, 0, false);
        const auto read = [this](uint32_t id, QByteArray& out) { out = bytes.at(id); return true; };
        const auto sprites = scene.prepare(w, map, read, 1000, nullptr, true);
        if (drawn) *drawn = sprites;
        QImage out(64 + 2 * margin, 64 + 2 * margin, QImage::Format_RGBA8888); out.fill(Qt::black);
        QPainter p(&out);
        // Positions are padded-terrain pixels; any drawn turf fixes the case tile's top-left (y grows south).
        const auto turf = std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.kind == shell::MapSprite::Turf; });
        if (turf == sprites.end()) return out;
        const QPoint origin = turf->position + QPoint((x - turf->cell.x()) * 32, (turf->cell.y() - y) * 32) - QPoint(margin, margin);
        for (const auto& s : sprites) {
            // Nearest sampling: the facts compared are positions, sizes and notch directions, not edge blending.
            if (s.affine) p.setTransform(s.affine->forward * QTransform::fromTranslate(s.position.x() - origin.x(), s.position.y() - origin.y()));
            p.drawImage(s.affine ? QPoint() : s.position - origin, s.image);
            p.resetTransform();
        }
        return out;
    }
    static QString name(QColor c) {
        static const std::vector<std::pair<QString, QColor>> palette{
            {"floor", {16, 16, 16}}, {"holder.a", {40, 80, 230}}, {"holder.b", {40, 200, 230}}, {"kid.a", {240, 120, 0}},
            {"kid.b", {230, 40, 40}}, {"wall", {120, 120, 120}}, {"white", Qt::white}, {"remote", {60, 20, 60}},
            {"thing", {40, 200, 60}}, {"npc", {240, 220, 40}}, {"black", Qt::black}};
        for (const auto& [n, p] : palette)
            if (std::abs(p.red() - c.red()) + std::abs(p.green() - c.green()) + std::abs(p.blue() - c.blue()) < 40) return n;
        return "other";
    }
};
bool readStockCase(const QString& path, const QString& set, const QString& name, QJsonObject& out,
                   QString& error, qsizetype expectedRows = 0) {
    out = {}; error.clear();
    QJsonDocument doc;
    if (!testinput::readRecordedJson(path, doc, error)) return false;
    const auto fail = [&](const QString& detail) { error = path + ": sets." + set + ": " + detail; return false; };
    const QJsonValue sets = doc.object()["sets"];
    if (!doc.isObject() || !sets.isObject() || !sets.toObject()[set].isArray())
        return fail("expected an array of named cases");
    const auto rows = sets.toObject()[set].toArray();
    if (expectedRows && rows.size() != expectedRows)
        return fail(QString("expected %1 retained cases").arg(expectedRows));
    QSet<QString> names;
    QJsonObject found;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const auto row = rows[i].toObject();
        const auto rowName = row["case"].toString();
        if (!rows[i].isObject() || !row["case"].isString() || rowName.isEmpty())
            return fail(QString("row %1 requires a nonempty case name").arg(i + 1));
        if (names.contains(rowName)) return fail("duplicate case " + rowName);
        names.insert(rowName);
        if (rowName == name) found = row;
    }
    if (found.isEmpty()) return fail("missing case " + name);
    out = found;
    return true;
}
bool visStockCase(const QString& set, const QString& name, QJsonObject& out, QString& error) {
    return readStockCase(QString(FUGUE_TEST_DATA_DIR) + "/vis-world/stock.json", set, name, out, error, set == "c" ? 12 : 0);
}
bool masterMatrixIs(const shell::MapSprite& s, std::array<double,6> m) {
    if (!s.planeEffects || !s.planeEffects->matrix) return false;
    for (int i = 0; i < 6; ++i) if (std::abs((*s.planeEffects->matrix)[i] - m[i]) > 1e-6) return false;
    return true;
}
std::optional<shell::MapSprite> masterSprite(const shell::MapSprites& sprites) {
    const auto it = std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.kind == shell::MapSprite::Master; });
    if (it == sprites.end()) return std::nullopt;
    return *it;
}
}
class EntityMapTest : public QObject {
    Q_OBJECT
    bool screenRangeDataReady_ = false;
private slots:
    void appearanceDecoderValuesAndWireFlags() {
        const auto decoded = shell::decodeEntityAppearance(model::appearanceOf(R"({"dir":4,"hasPlane":true,"plane":-32767,
            "pixelX":3,"pixelW":4,"pixelY":5,"pixelZ":6,"hasColor":true,
            "colorR":0,"colorG":128,"colorB":255,"colorA":64,"underlays":[8,7],"overlays":[9],
            "screenLoc":"2,2","maptext":"hello","maptextW":48,"maptextH":16,"maptextY":3,
            "appearanceFlags":2147484164,"vars":[{"bit":8192,"data":"2000"}]})"), 32);
        QVERIFY(decoded.valid); const auto& ap = decoded.fields;
        QCOMPARE(ap.dir, 4); QVERIFY(ap.hasPlane); QCOMPARE(ap.plane, -32767);
        QCOMPARE(ap.offset, QPoint(7,-11));
        QCOMPARE(ap.tint, (std::array<float,4>{0,128.f/255,1,64.f/255}));
        QCOMPARE(ap.underlays, (std::vector<uint32_t>{8,7})); QCOMPARE(ap.overlays, (std::vector<uint32_t>{9}));
        QVERIFY(ap.screenPlacement); QCOMPARE(ap.maptext, QString("hello"));
        QCOMPARE(ap.maptextSize, QSize(48,16)); QCOMPARE(ap.maptextOffset, QPoint(0,-3));
        QVERIFY(ap.imageOwnDir); QVERIFY(ap.passMouse); QVERIFY(!ap.fixedDir);
        QCOMPARE(ap.visFlags, uint16_t(0x20)); QCOMPARE(ap.mouseOpacity, 1);
        const auto fixed = shell::decodeEntityAppearance(model::appearanceOf(R"({"appearanceFlags":135168})"), 32);
        QVERIFY(fixed.valid); QVERIFY(fixed.fields.fixedDir); QVERIFY(!fixed.fields.imageOwnDir);
        QCOMPARE(fixed.fields.mouseOpacity, 2);
        const auto reset = shell::decodeEntityAppearance(model::appearanceOf("{}"), 32, ap);
        QVERIFY(reset.valid); QVERIFY(reset.fields.overlays.empty()); QVERIFY(!reset.fields.screenPlacement);
        QCOMPARE(reset.fields.offset, QPoint()); QCOMPARE(reset.fields.tint, (std::array<float,4>{1,1,1,1}));
        const auto transform = shell::decodeEntityAppearance(model::appearanceOf(R"({"hasTransform":true,"transform":[]})"), 32);
        QVERIFY(transform.valid); QVERIFY(!transform.fields.transformValid); QVERIFY(transform.fields.affine);
        const auto effects = shell::decodeEntityAppearance(model::appearanceOf(R"({"appearanceFlags":268435456,"filters":[{"type":99,"flags":1}]})"), 32);
        QVERIFY(effects.valid); QVERIFY(effects.fields.master); QVERIFY(effects.fields.planeEffects);
        QCOMPARE(effects.fields.planeEffects->ignoredFilters, size_t(1));
    }
    void selfVisibilityRequests_data() {
        QTest::addColumn<bool>("sampled");
        QTest::newRow("cached") << false; QTest::newRow("sampled") << true;
    }
    void selfVisibilityRequests() {
        QFETCH(bool, sampled);
        auto w = world();
        setFields(w.appearances[1], R"({"vars":[{"bit":4,"data":"65"}]})");
        w.appearances[2] = w.appearances[1];
        setFields(w.appearances[2], R"({"pixelX":16,"vars":[{"bit":4,"data":"65"}]})");
        auto atom = w.atoms.at(42); w.atoms.clear(); atom.appid = sampled ? 2 : 1;
        if (sampled) atom.chain = {1000,100,{{1,2,std::bit_cast<uint32_t>(10.f),0,0,1,1000}}};
        const uint32_t self = 42 | model::kRegistryA, other = 43 | model::kRegistryA;
        w.atoms[self] = atom; w.atoms[other] = atom; // same appearance/direction, different visibility policy
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        auto frame = terrain.prepare(w,{},0,false);
        auto visibility = std::make_shared<shell::VisibilityFrame>();
        visibility->cells = {0,0,20,20}; visibility->status = shell::VisibilityOutcome::Visible;
        visibility->sight = 0x24; // category admission isolates appearance invisibility from LOS
        frame.geometry.visibility = visibility;
        auto draw = [&] { return scene.prepare(w,frame,reader(),1500,&stats); };
        QVERIFY(draw().empty()); // ordinary cache must not acquire SEE_SELF expansion
        w.haveSelf = true; w.selfRef = 42;
        const auto first = draw(); QCOMPARE(first.size(),1); QCOMPARE(first[0].atom,self);
        const auto warm = draw(); QCOMPARE(warm,first);
        QCOMPARE(stats.templateBuilds, sampled ? uint64_t(2) : uint64_t(0));
        QCOMPARE(stats.frameDecodes,uint64_t(0)); QCOMPARE(stats.resourceReads,uint64_t(0));
        w.selfRef = 43;
        const auto switched = draw(); QCOMPARE(switched.size(),1); QCOMPARE(switched[0].atom,other);
        QCOMPARE(switched[0].position,first[0].position);
        QCOMPARE(stats.templateBuilds, sampled ? uint64_t(2) : uint64_t(0));
        w.haveSelf = false; QVERIFY(draw().empty());
        QCOMPARE(first[0].atom,self); // changing policy cannot mutate a published frame
    }
    void sampledImageInheritanceAndCacheIsolation() {
        auto w = world(); w.appearances[1].fields.iconState = "";
        setFields(w.appearances[1], R"({"pixelX":3,"hasPlane":true,"plane":5,"hasColor":true,"colorR":128,"colorG":255,"colorB":255,"colorA":128})");
        w.atoms[42].renderIdentity = 10; w.atoms[42].dir = 4;
        w.appearances[2] = w.appearances[1]; setFields(w.appearances[2], "{}");
        w.appearances[2].fields.layerBits = std::bit_cast<uint32_t>(-1.f);
        w.appearances[3] = w.appearances[2];
        setFields(w.appearances[3], R"({"pixelX":16,"appearanceFlags":4})");
        w.appearances[4] = w.appearances[1];
        setFields(w.appearances[4], R"({"pixelX":6,"hasPlane":true,"plane":7,"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":64})");
        w.atoms[43] = w.atoms[42]; w.atoms[43].appid = 3; // same root/direction, ordinary cached template
        w.atoms[44] = w.atoms[42]; w.atoms[44].appid = 4; w.atoms[44].dir = 8; w.atoms[44].renderIdentity = 11;
        w.imageInstances[7] = {3,42,1,false,20,10}; w.imageInstances[8] = {3,44,1,false,21,11};
        w.imageChains[7] = {1000,100,{{2,3,std::bit_cast<uint32_t>(10.f),0,0,1,1000}}};
        w.imageChains[8] = w.imageChains[7];
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto frame = terrain.prepare(w,{},0,false);
        auto read = [](uint32_t, QByteArray& out) { out = iconfixture::animation(); return true; };
        auto draw = [&](int64_t now) { return scene.prepare(w,frame,read,now,&stats,true); };
        auto find = [](const auto& sprites, uint32_t atom, bool image) {
            return std::find_if(sprites.begin(),sprites.end(),[&](const auto& s) {
                return s.atom == atom && bool(s.imageIdentity) == image;
            });
        };
        const auto first = draw(1500); QCOMPARE(first.size(),5);
        for (uint32_t target : {42u,44u}) {
            const auto parent = find(first,target,false), image = find(first,target,true);
            QVERIFY(parent != first.end()); QVERIFY(image != first.end());
            QCOMPARE(image->position-parent->position,QPoint(8,0));
            QCOMPARE(image->plane,parent->plane); QCOMPARE(image->tint,parent->tint);
            QCOMPARE(image->image.pixelColor(16,0),target == 42 ? QColor(50,70,20) : QColor(50,90,20));
        }
        const auto ordinary = find(first,43,false); QVERIFY(ordinary != first.end());
        QCOMPARE(ordinary->position-find(first,42,false)->position,QPoint(13,0));
        QCOMPARE(ordinary->plane,0); QCOMPARE(ordinary->tint,(std::array<float,4>{1,1,1,1}));
        QCOMPARE(draw(1500),first); QCOMPARE(stats.templateBuilds,uint64_t(2));
        QCOMPARE(stats.frameDecodes,uint64_t(0)); QCOMPARE(stats.resourceReads,uint64_t(0));
        setFields(w.appearances[3], R"({"pixelX":16,"appearanceFlags":516})"); // explicitly owned direction
        const auto assigned = draw(1500); QCOMPARE(assigned.size(),5);
        for (uint32_t target : {42u,44u}) {
            const auto image = find(assigned,target,true); QVERIFY(image != assigned.end());
            QCOMPARE(image->image.pixelColor(16,0),QColor(50,30,20));
        }
        QCOMPARE(find(first,42,true)->image.pixelColor(16,0),QColor(50,70,20));
        QCOMPARE(draw(2100).size(),5);
        const auto settled = draw(2200); QCOMPARE(settled.size(),5);
        QCOMPARE(stats.templateBuilds,uint64_t(0)); // sampled roots never replace shared static assets
    }
    void sampledOccurrenceCompatibility_data() {
        QTest::addColumn<QString>("attachment"); QTest::addColumn<int>("kind");
        QTest::addColumn<int>("during");
        using S = shell::MapSprite;
        QTest::newRow("ordinary") << QString("ordinary") << int(S::Entity) << 1;
        QTest::newRow("flick") << QString("flick") << int(S::Entity) << 1; // flick frames on the sampled root
        QTest::newRow("hud-root") << QString("hud") << int(S::Screen) << 1; // stock keeps it at its screen_loc (hud-anim-world)
        QTest::newRow("vis-holder") << QString("vis-holder") << int(S::Vis) << 1; // copies use the sampled holder offset
        QTest::newRow("vis-child") << QString("vis-child") << int(S::Vis) << 1;
        QTest::newRow("image-parent") << QString("image") << int(S::Image) << 1; // follows the sampled target
    }
    void sampledOccurrenceCompatibility() {
        QFETCH(QString, attachment); QFETCH(int, kind); QFETCH(int, during);
        auto w = world(); w.atoms[42].renderIdentity = 10;
        w.appearances[2] = w.appearances[1]; setFields(w.appearances[2], R"({"pixelX":16})");
        w.atoms[42].appid = 2;
        w.atoms[42].chain = {1000,100,{{1,2,std::bit_cast<uint32_t>(10.f),0,0,1,1000}}};
        if (attachment == "flick") w.flicks[42] = {"red",0,true,1000,99,true};
        if (attachment == "hud") {
            setFields(w.appearances[2], R"({"pixelX":16,"screenLoc":"1,1"})");
            w.hudOrder = {42}; w.hudAppid[42] = 2; w.hudIdentity[42] = {2,99,10};
        }
        if (attachment.startsWith("vis")) {
            auto& child = w.atoms[43]; child.appid = 1; child.renderIdentity = 11;
            if (attachment == "vis-child") {
                child.appid = 2; child.chain = w.atoms[42].chain;
                w.atoms[42].appid = 1; w.atoms[42].chain = {};
            }
            QVERIFY(w.vis.replace(42,10,{{2,43}},[](uint32_t) { return uint64_t(11); }));
        }
        if (attachment == "image") w.imageInstances[7] = {1,42,1,false,20,10};
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto frame = terrain.prepare(w,{},0,false);
        auto draw = [&](int64_t now) { return scene.prepare(w,frame,reader(),now,&stats,true); };
        auto count = [&](const auto& sprites) {
            return std::count_if(sprites.begin(),sprites.end(),[&](const auto& s) { return int(s.kind) == kind; });
        };
        const auto active = draw(1500); QCOMPARE(count(active),during);
        if (attachment == "image") {
            // Mid-animation the image sits on the target's interpolated offset.
            auto at = [&](shell::MapSprite::Kind k) {
                return std::find_if(active.begin(),active.end(),[&](const auto& s) { return s.kind == k; })->position;
            };
            QCOMPARE(at(shell::MapSprite::Image),at(shell::MapSprite::Entity));
        }
        QCOMPARE(count(draw(2100)),1); // settled chains regain the existing static combination
        const auto settled = draw(2200); QCOMPARE(count(settled),1);
        if (attachment == "hud") {
            // A screen object's animated pixel offset does not move it (hud-anim-world): mid-animation it sits where
            // the same object without an offset does.
            auto at = [](const auto& sprites) {
                return std::find_if(sprites.begin(),sprites.end(),[](const auto& s) { return s.kind == shell::MapSprite::Screen; })->position;
            };
            auto still = w; still.atoms[42].chain = {}; setFields(still.appearances[2], R"({"screenLoc":"1,1"})");
            shell::EntityMap plain;
            QCOMPARE(at(active),at(plain.prepare(still,frame,reader(),1500,nullptr,true)));
        }
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.frameDecodes,uint64_t(0));
        QCOMPARE(count(active),during);
    }
    void templateRequestDependencies_data() {
        QTest::addColumn<int>("request");
        QTest::newRow("ordinary") << 0; QTest::newRow("image") << 1;
        QTest::newRow("vis") << 2; QTest::newRow("flick") << 3;
    }
    void templateRequestDependencies() {
        QFETCH(int, request);
        auto w = world(); w.resources.push_back({456,0}); w.atoms[42].renderIdentity = 10;
        w.appearances[2] = w.appearances[1]; setFields(w.appearances[2], R"({"overlays":[3]})");
        w.appearances[3] = w.appearances[1]; setFields(w.appearances[3], R"({"pixelX":8})");
        w.appearances[3].fields.iconResID = 1; w.appearances[3].fields.iconState = "green";
        w.appearances[3].fields.layerBits = std::bit_cast<uint32_t>(-1.f);
        w.appearances[1].fields.iconResID = UINT32_MAX;
        if (request == 1) w.imageInstances[7] = {2,42,1,false,20,10};
        else if (request == 2) {
            auto& child = w.atoms[43]; child.appid = 2; child.renderIdentity = 11;
            QVERIFY(w.vis.replace(42,10,{{2,43}},[](uint32_t) { return uint64_t(11); }));
        } else {
            w.atoms[42].appid = 2;
            if (request == 3) w.flicks[42] = {"red",0,true,0,99,true};
        }
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto frame = terrain.prepare(w,{},0,false);
        const auto bytes = iconBytes(); bool ready = true; int reads = 0;
        auto read = [&](uint32_t id, QByteArray& out) {
            ++reads; out = bytes; return id != 789 || ready;
        };
        auto draw = [&] { return scene.prepare(w,frame,read,1,&stats,true); };
        const auto first = draw(); QCOMPARE(first.size(),2);
        QCOMPARE(first[0].image.pixelColor(16,16), QColor(Qt::red));
        QCOMPARE(first[1].image.pixelColor(16,16), QColor(Qt::green));
        QCOMPARE(first[1].position-first[0].position, QPoint(8,0));
        const auto warm = draw(); QCOMPARE(warm.constData(), first.constData());
        QCOMPARE(stats.appearanceDecodes,uint64_t(0)); QCOMPARE(stats.templateBuilds,uint64_t(0));
        QCOMPARE(stats.frameDecodes,uint64_t(0)); QCOMPARE(stats.resourceReads,uint64_t(0));
        setFields(w.appearances[3], R"({"pixelX":16})");
        const auto moved = draw(); QCOMPARE(moved.size(),2);
        QCOMPARE(moved[1].position-moved[0].position, QPoint(16,0));
        QCOMPARE(stats.templateBuilds,uint64_t(1)); QCOMPARE(stats.frameDecodes,uint64_t(0));
        w.appearances[3].fields.iconState = "red";
        const auto scalar = draw(); QCOMPARE(scalar.size(),2);
        QCOMPARE(scalar[1].image.pixelColor(16,16), QColor(Qt::red));
        QCOMPARE(stats.appearanceDecodes,uint64_t(0)); QCOMPARE(stats.templateBuilds,uint64_t(1));
        const auto child = w.appearances[3]; w.appearances.erase(3);
        QCOMPARE(draw().size(), request == 2 ? 0 : 1);
        w.appearances[3] = child; QCOMPARE(draw().size(),2);
        w.resources[1] = {789,0}; ready = false;
        QCOMPARE(draw().size(), request == 2 ? 0 : 1);
        const int beforeRetry = reads;
        QCOMPARE(draw().size(), request == 2 ? 0 : 1);
        QCOMPARE(reads,beforeRetry+1); QCOMPARE(stats.templateBuilds,uint64_t(0));
        ready = true; const auto arrived = draw(); QCOMPARE(arrived.size(),2);
        QCOMPARE(stats.templateBuilds,uint64_t(1));
        setFields(w.appearances[3], R"({"overlays":[2]})"); // path-local cycle, not global deduplication
        QCOMPARE(draw().size(), request == 2 ? 0 : 2);
        QCOMPARE(first[1].image.pixelColor(16,16), QColor(Qt::green));
        QCOMPARE(first[1].position-first[0].position, QPoint(8,0)); // immutable publication
    }
    void turfImageBindingAndLocalCaptureLifecycle() {
        model::Session session; auto& w=session.worldMut();w=world();
        w.atoms[42].renderIdentity=10;
        setFields(w.appearances[1], R"({"renderTarget":"*local","overlays":[3],"pixelX":5,"appearanceFlags":67108864})");
        setFields(w.appearances[2], R"({"renderSource":"*local","appearanceFlags":2147483648,"hasPlane":true,"plane":5})");
        w.appearances[3]=w.appearances[1];w.appearances[3].fields.iconState="green";
        setFields(w.appearances[3], R"({"pixelX":-12,"pixelY":8})");
        setFields(w.appearances[4], "{}");w.appearances[4].fields.iconResID=UINT32_MAX;
        auto image=[&](uint16_t type,uint32_t cell) {
            Bytes b;putLe32(b,7);putLe32(b,2);b.push_back(7);putLe32(b,cell);putLe16(b,0);session.ingest(type,b);
        };
        image(0x33,105);const auto firstIdentity=w.imageInstances.at(7).identity;
        const std::array<uint16_t,3> cell{5,5,0};
        QVERIFY(!w.imageTurfCurrent(7,firstIdentity,cell)); // image-before-tile is inert
        w.tiles[cell]=4;QVERIFY(w.imageTurfCurrent(7,firstIdentity,cell));
        QVERIFY(w.pendingImageTargets.empty()); // turf cells never alias atom slots
        shell::TurfMap terrain;shell::EntityMap entities;
        auto draw=[&] {return entities.prepare(w,terrain.prepare(w,{},0,false),reader(),0,nullptr,true);};
        const auto sprites=draw();QCOMPARE(sprites.size(),3);
        const shell::LocalTargets plan(sprites);QCOMPARE(plan.groups.size(),size_t(1));
        QVERIFY(plan.groups[0].valid);QCOMPARE(plan.groups[0].bounds.size(),QSize(44,40));
        const auto relay=std::find_if(sprites.begin(),sprites.end(),[](const auto& s){return s.imageIdentity!=0;});
        QVERIFY(relay!=sprites.end());QCOMPARE(relay->kind,shell::MapSprite::Turf);QVERIFY(relay->passMouse);
        const auto pixel=shell::LocalTargets::placed(*relay,plan.groups[0]).topLeft()+QPoint(1,1);
        const auto frame=terrain.prepare(w,{},0,false);
        const auto pointer=shell::mapPointerAt(pixel-frame.source.topLeft(),frame.source.size(),{3,3},frame.source,frame.geometry,"map",true,sprites);
        QVERIFY(pointer && pointer->relayHit);QCOMPARE(pointer->click.over.ref,42u);
        QVERIFY(shell::mapPointerCurrent(*pointer,w,0));
        image(0x35,105);QCOMPARE(w.imageInstances.at(7).identity,firstIdentity);
        image(0x35,106);QVERIFY(!shell::mapPointerCurrent(*pointer,w,0));
        image(0x35,105);QVERIFY(!shell::mapPointerCurrent(*pointer,w,0)); // no back-and-forth revival
        image(0x33,105);
        auto missingReader=[](uint32_t,QByteArray&){return false;};
        shell::EntityMap late;
        const auto missing=late.prepare(w,frame,missingReader,0,nullptr,true);
        const shell::LocalTargets missingPlan(missing);QVERIFY(!missingPlan.groups.empty());QVERIFY(!missingPlan.groups[0].valid);
        const auto arrived=late.prepare(w,frame,reader(),0,nullptr,true);
        QVERIFY(shell::LocalTargets(arrived).groups[0].valid);
        QCOMPARE(sprites.size(),3); // old publication retained across all transitions
        // Without KEEP_TOGETHER, stock leaves the overlay at the original
        // source occurrence. Only the root's own part belongs to its target.
        setFields(w.appearances[1], R"({"renderTarget":"*local","overlays":[3],"pixelX":5})");
        const auto separate=draw();const shell::LocalTargets separatePlan(separate);
        QVERIFY(separatePlan.groups[0].valid);QCOMPARE(separatePlan.groups[0].bounds.size(),QSize(32,32));
        QCOMPARE(std::count_if(separate.begin(),separate.end(),[](const auto& s){return !s.imageIdentity && s.renderTarget.isEmpty();}),1);
        for(const auto fields:{R"({"renderTarget":"*local","filters":[{"type":99,"flags":1}]})",
                               R"({"renderTarget":"*local","hasTransform":true,"transform":[]})"}) {
            setFields(w.appearances[1], fields);const auto refused=draw();
            const shell::LocalTargets rejected(refused);QVERIFY(!rejected.groups.empty());QVERIFY(!rejected.groups[0].valid);
        }
        Bytes deleted;putLe32(deleted,7);session.ingest(0x34,deleted);QVERIFY(w.imageInstances.empty());
        QCOMPARE(session.ingestErrors(),0);
    }
    void stockTurfLocalMembershipAndInput() {
        shell::test::TurfLocalFixture f; QVERIFY(f.load());
        auto& w=f.session.worldMut();
        const auto original=f.sprites;
        const shell::LocalTargets targets(original);
        QVERIFY(std::any_of(original.begin(),original.end(),[](const auto& s){return s.kind==shell::MapSprite::Master && s.plane==-5;}));
        QCOMPARE(targets.groups.size(),size_t(6));
        for (const auto& group:targets.groups) { QVERIFY(group.valid); QVERIFY(!group.cyclic); }
        QCOMPARE(targets.groups[targets.find("*capture1")].parts.size(),size_t(1));
        QCOMPARE(targets.groups[targets.find("*capture2")].parts.size(),size_t(3));
        QCOMPARE(targets.groups[targets.find("*self")].parts.size(),size_t(1));
        QCOMPARE(targets.groups[targets.find("full")].parts.size(),size_t(1));
        const auto producer=original[targets.groups[targets.find("*capture1")].parts[0]];
        auto duplicate=producer; ++duplicate.localGroup;
        QCOMPARE(shell::LocalTargets({producer,duplicate}).find("*capture1"),0); // one name twice: stock relays one of them (shared-render-target)
        auto cycle=producer; cycle.renderSource=cycle.renderTarget;
        const shell::LocalTargets cyclic({cycle});
        QVERIFY(!cyclic.groups[0].valid); QVERIFY(cyclic.groups[0].cyclic);
        const auto arrival=w.tiles.at({7,2,0});
        QCOMPARE(std::count_if(original.begin(),original.end(),[&](const auto& s){return s.kind==shell::MapSprite::Turf && s.appearanceId==arrival;}),6);
        const auto blocker=std::find_if(original.begin(),original.end(),[&](const auto& s){
            return s.appearanceId==arrival && s.plane==13 && s.renderSource=="full";
        });
        QVERIFY(blocker!=original.end()); QVERIFY(!blocker->localGroup); QVERIFY(blocker->sourcePath);
        QCOMPARE(blocker->tint,(std::array<float,4>{0,0,0,1}));
        QVERIFY(!blocker->appearanceGroup); // leaf source flags do not invent a capture cycle
        struct Receipt { QPoint point; const char* name; int x,y; bool vis; int vx,vy; };
        // Independent stock callbacks, physical MAP scale 2, not renderer goldens.
        for (const auto& row:std::array<Receipt,6>{{
            {{40,136},"producer1",9,24,true,9,-40},
            {{104,152},"producer2",9,8,true,9,-56},
            {{168,72},"producer3",9,24,false,0,0},
            {{72,200},"selfproducer",41,24,false,0,0},
            {{152,232},"holder1",25,24,false,0,0},
            {{216,232},"child2",25,24,false,0,0}}}) {
            const auto hit=f.at(row.point); QVERIFY(hit);
            QVERIFY2(shell::mapPointerCurrent(*hit,w,0),row.name);
            uint32_t appid=0;
            if (hit->click.over.kind==proto::enc::RefTurf)
                appid=w.tiles.at({uint16_t(hit->click.over.x),uint16_t(hit->click.over.y),uint16_t(hit->z)});
            else appid=w.atoms.at(hit->click.over.ref).appid;
            QCOMPARE(QString::fromStdString(model::parseJson(fieldsJson(w.appearances.at(appid)))["name"].s),QString(row.name));
            QCOMPARE(hit->click.iconX,row.x); QCOMPARE(hit->click.iconY,row.y);
            QCOMPARE(hit->click.hasVis,row.vis);
            if (row.vis) { QCOMPARE(hit->click.visX,row.vx); QCOMPARE(hit->click.visY,row.vy); }
        }
        const auto hit=f.at({40,136}); QVERIFY(hit && hit->relayHit);
        const std::array<uint16_t,3> cell{1,6,0};
        const auto appid=w.tiles.at(cell);
        auto replace=[&](uint32_t appearance) {
            Bytes bytes; putLe16(bytes,1); bytes.push_back(1);
            for (auto coordinate:cell) putLe16(bytes,coordinate);
            putLe32(bytes,appearance); putLe16(bytes,0);
            f.session.setIngestClockNs(2'000'000'000); f.session.ingest(0xd8,bytes);
        };
        w.tileIconStart[cell].ns=1'000'000'000;
        const auto revision=w.tileRevision.at(cell);
        replace(appid); QCOMPARE(w.tileRevision.at(cell),revision);
        QVERIFY(shell::mapPointerCurrent(*hit,w,0));
        replace(w.tiles.at({3,6,0})); replace(appid);
        QVERIFY(w.tileRevision.at(cell)>revision);
        QCOMPARE(w.tileIconStart.at(cell).ns,int64_t(1'000'000'000)); // appearance-only changes preserve playback
        QCOMPARE(f.session.ingestErrors(),0);
        QVERIFY(!shell::mapPointerCurrent(*hit,w,0));
        // Same cell/name after replacement cannot revive an old rendered endpoint.
        f.refresh(0); const auto replaced=f.at({40,136}); QVERIFY(replaced && shell::mapPointerCurrent(*replaced,w,0));
        w.tiles.erase(cell); f.refresh(0); QVERIFY(!shell::mapPointerCurrent(*replaced,w,0));
        QVERIFY(shell::LocalTargets(f.sprites).find("*capture1")<0);
        w.tiles[cell]=appid; ++w.tileRevision[cell]; f.refresh(1'000'000'000);
        QVERIFY(!shell::mapPointerCurrent(*replaced,w,0));
        QVERIFY(shell::LocalTargets(f.sprites).groups[0].valid);
        QCOMPARE(original.size(),f.sprites.size());
        const auto child=f.at({72,200}); QVERIFY(child && child->relayHit);
        const auto root=w.tiles.at({1,2,0}); const auto saved=fieldsJson(w.appearances[root]);
        setFields(w.appearances[root], R"({"renderTarget":"*self"})");
        QVERIFY(!shell::mapPointerCurrent(*child,w,0));
        setFields(w.appearances[root], saved);
        const auto sourceId=child->relayHit->consumerPath->back();
        const auto sourceSaved=fieldsJson(w.appearances[sourceId]);
        // Unverified child opacity/PASS_MOUSE combinations stay visually intact,
        // but do not acquire root-owned relay input by accident.
        for (const uint32_t flags:{0x3000u,0x1000u,0x80000000u}) {
            setFields(w.appearances[sourceId], "{\"renderSource\":\"*self\",\"pixelX\":32,\"appearanceFlags\":"+std::to_string(flags)+"}");
            f.refresh(0);
            const auto inert=f.at({72,200}); QVERIFY(inert); QVERIFY(!inert->relayHit);
            QVERIFY(std::any_of(f.sprites.begin(),f.sprites.end(),[](const auto& s){return s.renderSource=="*self" && !s.inkInteractive;}));
        }
        setFields(w.appearances[sourceId], sourceSaved);
        auto drag=*hit; drag.kind=shell::MapPointer::Drag;
        QVERIFY(!shell::mapPointerCurrent(drag,w,0));
    }
    void refusedAnimatedForwardCaptureMarker() {
        auto w=world(); w.resources={{123,0},{456,0}};
        w.appearances[10]=w.appearances[1];
        setFields(w.appearances[10], R"({"renderTarget":"*refused","renderSource":"*missing","underlays":[11]})");
        w.appearances[11]=w.appearances[1]; w.appearances[11].fields.iconResID=1; w.appearances[11].fields.iconState="";
        setFields(w.appearances[11], "{}");
        w.tiles[{5,5,0}]=10;
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto read=[&](uint32_t id,QByteArray& out) { out=id==456 ? iconfixture::animation() : iconBytes(); return true; };
        // The forwarding path still uses construct(), traverses the animated
        // underlay, then replaces all parts with a refusal marker. Its old
        // animated=[0] must not survive even as ordinary captures gain support.
        for (int64_t ns:{0LL,100'000'000LL,200'000'000LL}) {
            const auto sprites=scene.prepare(w,terrain.prepare(w,{},ns,false),read,ns,nullptr,true);
            const shell::LocalTargets plan(sprites); QCOMPARE(plan.groups.size(),size_t(1));
            QVERIFY(!plan.groups[0].valid);
            for (auto i:plan.groups[0].parts) { QVERIFY(sprites[i].footprint); QVERIFY(sprites[i].image.isNull()); }
        }
    }
    void refusedAnimatedLocalCapture_data() {
        QTest::addColumn<int>("transition");
        QTest::newRow("animated-root-alpha-is-captured") << 0;
        QTest::newRow("animated-underlay-vis-flags-are-captured") << 1;
        QTest::newRow("animated-underlay-resource-deferred") << 2;
    }
    void refusedAnimatedLocalCapture() {
        QFETCH(int, transition);
        auto w = world();
        w.resources = {{123, 0}, {456, 0}, {789, 0}};
        w.appearances[10] = w.appearances[1];
        w.appearances[11] = w.appearances[1];
        w.appearances[11].fields.iconResID = 1; w.appearances[11].fields.iconState = "";
        setFields(w.appearances[11], R"({"hasPlane":true,"plane":-21})");
        w.appearances[12] = w.appearances[1]; w.appearances[12].fields.iconResID = 2;
        if (transition == 0) { w.appearances[10].fields.iconResID = 1; w.appearances[10].fields.iconState = ""; }
        const std::string base = transition == 0
            ? R"({"renderTarget":"*local","hasPlane":true,"plane":-5)"
            : R"({"renderTarget":"*local","hasPlane":true,"plane":-5,"underlays":[11])";
        setFields(w.appearances[10], base + "}");
        bool ready = false;
        const auto still = iconBytes(), animation = iconfixture::animation();
        const auto read = [&](uint32_t id, QByteArray& out) {
            out = id == 456 ? animation : still;
            return id != 789 || ready;
        };
        shell::TurfMap terrain;
        shell::EntityMap scene;
        const auto frame = terrain.prepare(w, {}, 0, false);
        auto draw = [&](int64_t ns) { return scene.prepare(w, frame, read, ns, nullptr, true); };
        QVERIFY(shell::LocalTargets(draw(0)).groups.empty());
        // Joining reveals a turf capture whose animated underlay is visited
        // before the static root. No actor/HUD or job-specific input is needed.
        const bool turf = transition != 0;
        if (turf) w.tiles[{5, 5, 0}] = 10;
        else { w.atoms[43] = w.atoms[42]; w.atoms[43].appid = 10; }
        const auto published = draw(0);
        const shell::LocalTargets initial(published);
        QCOMPARE(initial.groups.size(), size_t(1));
        QVERIFY(initial.groups[0].valid); // ordinary turf producers have independent stock coverage
        auto animatedPixel = [](const shell::MapSprites& sprites, QColor color) {
            return std::any_of(sprites.begin(), sprites.end(), [&](const auto& s) {
                return !s.image.isNull() && s.image.pixelColor(16, 0) == color;
            });
        };
        QVERIFY(animatedPixel(published, QColor(50, 30, 20)));
        setFields(w.appearances[10], base + (transition == 0 ? R"(,"hasColor":true,"colorA":128})"
            : transition == 1 ? R"(,"vars":[{"bit":8192,"data":"4000"}]})" : R"(,"overlays":[12]})"));
        for (const int64_t ns : {100'000'000LL, 150'000'000LL}) {
            const auto refused = draw(ns);
            const shell::LocalTargets targets(refused);
            QCOMPARE(targets.groups.size(), size_t(1));
            // A producer's own alpha is part of what is captured (stock, stock-parity-world overlay-relays ov-relay-src-alpha).
            // vis_flags on a producer that is in no vis_contents change nothing (stock 516.1689).
            if (transition != 2) { QVERIFY(targets.groups[0].valid); continue; }
            QVERIFY(!targets.groups[0].valid);
            for (const auto& sprite : refused) if (sprite.localGroup) {
                QVERIFY(sprite.image.isNull()); QVERIFY(sprite.footprint);
            }
        }
        // A marker has no DMI playback. Restoring the dependency must resume
        // the original ingest clock, not make the marker's zero FrameKey live.
        ready = true;
        if (transition != 2) setFields(w.appearances[10], base + "}");
        const auto restored = draw(200'000'000);
        QVERIFY(shell::LocalTargets(restored).groups[0].valid);
        QVERIFY(animatedPixel(restored, QColor(210, 30, 20)));
        QVERIFY(animatedPixel(published, QColor(50, 30, 20))); // immutable publication
        w.tiles.clear(); w.atoms.erase(43);
        QVERIFY(shell::LocalTargets(draw(210'000'000)).groups.empty());
    }
    // Every master here has a screen_loc: one without is no master at all (masterWithoutScreenLocIsInert).
    void masterSurfaceMetadataAndRefusal() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.hudOrder={100}; w.hudAppid[100]=10; w.hudIdentity[100]={2,7,11};
        setFields(w.appearances[10], affinefixture::fields({0,-1,3,1,0,2},
            R"(,"appearanceFlags":268435456,"screenLoc":"1,1","hasPlane":true,"plane":0)"));
        auto draw=[&] { return scene.prepare(w,terrain.prepare(w,{},0,false),reader(),0,nullptr,true); };
        const auto original=draw(); QCOMPARE(original.size(),2);
        const auto& master=original.back(); QCOMPARE(master.kind,shell::MapSprite::Master);
        QVERIFY(master.planeEffects); QVERIFY(master.planeEffects->supported); QVERIFY(master.planeEffects->matrix);
        QVERIFY(!master.affine); // geometry is viewport-sized, not icon-sized
        QCOMPARE((*master.planeEffects->matrix)[2],3.0);
        setFields(w.appearances[10], R"({"appearanceFlags":268435456,"screenLoc":"1,1","filters":[{"type":1,"flags":1,"paramMask":1,"params":{"bstr":"0000803f"}}]})");
        const auto filtered=draw(); QCOMPARE(filtered.size(),2);
        QVERIFY(filtered.back().planeEffects->supported);
        QCOMPARE(filtered.back().planeEffects->filters.size(),size_t(1));
        QCOMPARE(filtered.back().planeEffects->filters[0].size,1.0);
        const auto matrix=QByteArray(reinterpret_cast<const char*>(shell::identityColor.data()),80).toHex();
        setFields(w.appearances[10], std::string("{\"appearanceFlags\":268435456,\"screenLoc\":\"1,1\",\"hasColor\":true,\"colorA\":0,\"vars\":[{\"bit\":1024,\"data\":\"")+
            matrix.toStdString()+"\"}]}");
        const auto matrixMaster=draw(); QCOMPARE(matrixMaster.size(),2);
        QCOMPARE(matrixMaster.back().tint,(std::array<float,4>{1,1,1,1}));
        QVERIFY(matrixMaster.back().planeEffects->supported);
        setFields(w.appearances[10], affinefixture::fields({0,-1,3,1,0,2},
            R"(,"appearanceFlags":268435456,"screenLoc":"1,1","filters":[{"type":99,"flags":1}])"));
        const auto fallback=draw();QCOMPARE(fallback.size(),2);
        QVERIFY(fallback.back().planeEffects->supported);QVERIFY(fallback.back().planeEffects->filters.empty());
        QCOMPARE(fallback.back().planeEffects->ignoredFilters,size_t(1));
        QCOMPARE(fallback.back().planeEffects->matrix,original.back().planeEffects->matrix);
        QCOMPARE(filtered.back().planeEffects->filters.size(),size_t(1)); // immutable older filter command
        // Invalid matrices and malformed colour matrices still retain a refused marker;
        // ignoring an unsupported filter does not bypass those gates.
        for(const auto& fields:std::vector<std::string>{
            affinefixture::fields({0,0,0,0,0,0},R"(,"appearanceFlags":268435456,"screenLoc":"1,1")"),
            R"({"appearanceFlags":268435456,"screenLoc":"1,1","vars":[{"bit":1024,"data":"00"}]})"}) {
            setFields(w.appearances[10], fields);
            const auto refused=draw(); QCOMPARE(refused.size(),2);
            QVERIFY(refused.back().planeEffects); QVERIFY(!refused.back().planeEffects->supported);
        }
        QVERIFY(original.back().planeEffects->supported); // earlier frame is immutable
        w.hudOrder.clear(); w.hudAppid.clear(); QCOMPARE(draw().size(),1);
    }
    // A plane master whose screen_loc is null or "" does nothing in stock 516.1689: its plane draws as if it were
    // absent, whatever filter, alpha, KEEP_TOGETHER, overlay or render target it has. At 1,1 the same master acts
    // (tests/data/master-screen-loc-world).
    void masterWithoutScreenLocIsInert() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        auto draw=[&] { return scene.prepare(w,terrain.prepare(w,{},0,false),reader(),0,nullptr,true); };
        const auto absent=draw(); QCOMPARE(absent.size(),1);
        w.hudOrder={100}; w.hudAppid[100]=10; w.hudIdentity[100]={2,7,11};
        const std::vector<std::string> effects{
            R"("appearanceFlags":268435456,"filters":[{"type":1,"flags":1,"paramMask":1,"params":{"bstr":"0000803f"}}])",
            R"("appearanceFlags":268435456,"hasColor":true,"colorA":0)",
            R"("appearanceFlags":335544320,"hasColor":true,"colorA":0)", // KEEP_TOGETHER too
            R"("appearanceFlags":268435456,"overlays":[1])",
            R"("appearanceFlags":268435456,"renderTarget":"*cap")"};
        for (const auto& effect : effects) {
            // "bogus" is not coordinates: stock ignores that master too.
            for (const std::string loc : {"", R"(,"screenLoc":"")", R"(,"screenLoc":"  ")", R"(,"screenLoc":"bogus")"}) {
                setFields(w.appearances[10], "{" + effect + loc + "}");
                QVERIFY2(samePixelsAndMetadata(draw(),absent), (effect + loc).c_str());
            }
            setFields(w.appearances[10], "{" + effect + R"(,"screenLoc":"1,1"})");
            const auto placed=draw();
            QVERIFY2(std::any_of(placed.begin(),placed.end(),[](const auto& s) { return s.kind==shell::MapSprite::Master; }),
                     effect.c_str());
        }
    }
    // A master with a range acts once and draws its overlay at every position (stock 516.1689,
    // master-screen-loc-world "1,1 to 3,3|ov" and "|ov-outline": nine overlays, the plane outlined once).
    void masterRangeDrawsOverlaysAtEveryPosition() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/master-screen-loc-world/stock-observations.json";
        QJsonDocument doc; QString error;
        QVERIFY2(testinput::readRecordedJson(path, doc, error), qPrintable(error));
        std::map<QString,int> overlayPixels;
        for (const auto& value : doc.object()["phases"].toArray())
            overlayPixels[value.toObject()["phase"].toString()] = value.toObject()["counts"].toObject()["overlay"].toInt(-1);
        // The counted crop cuts the bottom row's overlays in half: six whole and three halves against one half.
        QVERIFY(overlayPixels["1,1|ov"] > 0);
        QCOMPARE(overlayPixels["1,1 to 3,3|ov"], 15 * overlayPixels["1,1|ov"]);
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        auto draw=[&] { return scene.prepare(w,terrain.prepare(w,{},0,false),reader(),0,nullptr,true); };
        w.hudOrder={100}; w.hudAppid[100]=10; w.hudIdentity[100]={2,7,11};
        const std::string outline = R"(,"filters":[{"type":2,"paramMask":35,"flags":1,"params":"0000803fffffffff0000"}])";
        for (const std::string& effect : {std::string(), outline}) {
            auto overlays=[&](const std::string& loc) {
                setFields(w.appearances[10], std::string("{\"appearanceFlags\":268435456,\"hasPlane\":true,\"plane\":1,\"overlays\":[1],\"screenLoc\":\"") +
                    loc + "\"" + effect + "}"); // (no raw string: moc misreads one that ends in a quote)
                std::vector<QPoint> at; int masters=0;
                for (const auto& s : draw()) {
                    if (s.kind==shell::MapSprite::Master) {
                        ++masters;
                        if (!s.planeEffects || !s.planeEffects->supported || s.planeEffects->filters.size()!=(effect.empty() ? 0u : 1u)) masters=-100;
                    } else if (s.kind==shell::MapSprite::Screen) at.push_back(s.position);
                }
                return std::pair{masters,at};
            };
            const auto [oneMaster,one]=overlays("1,1");
            QCOMPARE(oneMaster,1); QCOMPARE(one.size(),size_t(1));
            auto [rangeMasters,nine]=overlays("1,1 to 3,3");
            QCOMPARE(rangeMasters,1); QCOMPARE(nine.size(),size_t(9));
            std::vector<QPoint> expected;
            for (int x=0;x<3;++x) for (int y=0;y<3;++y) expected.push_back(one[0]+QPoint(32*x,-32*y));
            auto order=[](const QPoint& a,const QPoint& b) { return a.x()!=b.x() ? a.x()<b.x() : a.y()<b.y(); };
            std::sort(nine.begin(),nine.end(),order); std::sort(expected.begin(),expected.end(),order);
            QCOMPARE(nine,expected);
        }
    }
    // A master off the view acts on its plane, enlarges the map like an ordinary screen object and draws its overlay
    // there, alone or as part of a range; a pixel offset past the edge enlarges nothing (stock 516.1689,
    // master-screen-loc-world "8,1", "0,4", "1,9", "6,6 to 9,9", "9,9 to 10,10", "7:40,1"; "|ov" against "|obj").
    void masterOffViewActsInTheLargerMap() {
        std::map<QString,QJsonObject> phases; QString error;
        QVERIFY2(masterLocPhases(phases, error), qPrintable(error));
        QRect basePart, baseOverlay;
        QVERIFY(masterLocBounds(phases, "7,1|ov", "part", basePart) && masterLocBounds(phases, "7,1|ov", "overlay", baseOverlay));
        for (const QString loc : {"8,1", "0,4", "1,9", "6,6 to 9,9"}) { // stock: a master's overlay is where an object's icon is
            QVERIFY2(phases.contains(loc + "|ov") && phases.contains(loc + "|obj"), qPrintable(loc));
            QCOMPARE(phases[loc + "|ov"]["counts"], phases[loc + "|obj"]["counts"]);
            QCOMPARE(phases[loc + "|ov"]["bounds"], phases[loc + "|obj"]["bounds"]);
        }
        for (const QString hidden : {"8,1|alpha0", "8,1|kt-alpha0", "0,4|alpha0", "1,9|alpha0", "7:40,1|alpha0",
                                     "6,6 to 9,9|alpha0", "9,9 to 10,10|alpha0"}) { // stock: the master acts
            QVERIFY2(phases.contains(hidden), qPrintable(hidden));
            QCOMPARE(phases[hidden]["counts"].toObject()["part"].toInt(-1), 0);
        }
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.bounds.viewW=w.bounds.viewH=7; // the view stock was measured in
        w.hudOrder={100}; w.hudAppid[100]=10; w.hudIdentity[100]={2,7,11};
        const std::string outline = R"(,"filters":[{"type":2,"paramMask":35,"flags":1,"params":"0000803fffffffff0000"}])";
        struct Seen { int masters = 0; std::vector<QPoint> at; QPoint entity; QSize source; };
        auto collect=[&](const shell::MapSprites& sprites, const QSize& source, bool filtered) {
            Seen seen; seen.source = source;
            for (const auto& s : sprites) {
                if (s.kind==shell::MapSprite::Master) {
                    ++seen.masters;
                    if (!s.planeEffects || !s.planeEffects->supported || s.planeEffects->filters.size()!=(filtered ? 1u : 0u)) seen.masters=-100;
                } else if (s.kind==shell::MapSprite::Screen) seen.at.push_back(s.position);
                else if (s.kind==shell::MapSprite::Entity) seen.entity = s.position;
            }
            return seen;
        };
        // Where the range's lowest, leftmost overlay stands against the world sprite.
        auto corner=[](const Seen& seen) {
            QPoint c = seen.at.front();
            for (const auto& p : seen.at) c = QPoint(std::min(c.x(), p.x()), std::max(c.y(), p.y()));
            return c - seen.entity;
        };
        struct Case { const char* loc; QMargins margins; size_t count; };
        for (const std::string& effect : {std::string(), outline}) {
            auto place=[&](const std::string& loc) {
                setFields(w.appearances[10], std::string("{\"appearanceFlags\":268435456,\"hasPlane\":true,\"plane\":1,\"overlays\":[1],\"screenLoc\":\"") +
                    loc + "\"" + effect + "}");
            };
            place("7,1");
            auto frame = terrain.prepare(w,{},0,false);
            const auto base = collect(scene.prepare(w,frame,reader(),0,nullptr,true), frame.source.size(), !effect.empty());
            QCOMPARE(base.masters,1); QCOMPARE(base.at.size(),size_t(1)); QCOMPARE(scene.hudMargins(),QMargins());
            for (const Case& c : {Case{"8,1",{0,0,1,0},1}, Case{"0,4",{1,0,0,0},1}, Case{"1,9",{0,2,0,0},1},
                                  Case{"6,6 to 9,9",{0,2,2,0},16}, Case{"9,9 to 10,10",{0,3,3,0},4}, Case{"7:24,1",{},1}}) {
                place(c.loc);
                // The frame that learns of the placement still has the old scene: the master acts, and its overlay,
                // which would lie outside that scene, is left out.
                frame = terrain.prepare(w,{},0,false);
                const auto before = collect(scene.prepare(w,frame,reader(),0,nullptr,true), frame.source.size(), !effect.empty());
                QVERIFY2(before.masters==1, c.loc);
                QCOMPARE(before.at.size(), c.margins.isNull() ? c.count : size_t(0));
                QCOMPARE(scene.hudMargins(), c.margins);
                frame = terrain.prepare(w,{},0,false,scene.hudMargins());
                const auto seen = collect(scene.prepare(w,frame,reader(),0,nullptr,true), frame.source.size(), !effect.empty());
                QVERIFY2(seen.masters==1, c.loc);
                QCOMPARE(seen.at.size(), c.count);
                QCOMPARE(seen.source, base.source + QSize(32*(c.margins.left()+c.margins.right()), 32*(c.margins.top()+c.margins.bottom())));
                // Stock centres the larger map, so its world moves by half of what was added on each side.
                QRect part;
                const QString phase = QString(c.loc) + (QByteArray(c.loc)=="9,9 to 10,10" ? "|ov-outline" : "|ov");
                QVERIFY2(masterLocBounds(phases, phase, "part", part), qPrintable(phase));
                QCOMPARE(part.topLeft()-basePart.topLeft(),
                         QPoint(16*(c.margins.left()-c.margins.right()), 16*(c.margins.top()-c.margins.bottom())));
                // Where stock shows the overlay it stands as far from the world as it does here. (0,4 and 1,9 are
                // outside the 224 pixel window; a range's top rows may be.)
                QRect overlay;
                if (masterLocBounds(phases, phase, "overlay", overlay))
                    QCOMPARE(QPoint(overlay.left(), overlay.bottom()) - part.topLeft() - (baseOverlay.bottomLeft() - basePart.topLeft()),
                             corner(seen) - corner(base));
                else QVERIFY2(QByteArray(c.loc)!="8,1" && QByteArray(c.loc)!="6,6 to 9,9" && QByteArray(c.loc)!="7:24,1", c.loc);
            }
            // A pixel offset that carries the overlay past the edge: the master acts, nothing is drawn or added.
            place("7:40,1");
            frame = terrain.prepare(w,{},0,false);
            const auto past = collect(scene.prepare(w,frame,reader(),0,nullptr,true), frame.source.size(), !effect.empty());
            QCOMPARE(past.masters,1); QCOMPARE(past.at.size(),size_t(0)); QCOMPARE(scene.hudMargins(),QMargins());
            QRect none; QVERIFY(!masterLocBounds(phases, "7:40,1|ov-outline", "overlay", none));
        }
        QCOMPARE(scene.hudMargins(), QMargins());
    }
    // In the larger map a screen object outside the view asks for, the world is placed in the added tiles too, and
    // stock shows it where mapWorldClip says (stock 516.1689, master-screen-loc-world "|wide" phases, README "What the
    // larger map shows"; the columns and rows of world each capture shows, in controls of 192, 224, 240 and 288 pixels).
    void worldInTheLargerMap() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.bounds.viewW=w.bounds.viewH=7;
        for (uint32_t x=1; x<=20; ++x) { auto& a=w.atoms[100+x]; a=w.atoms[42]; a.x=x; a.key=uint16_t(200+x); }
        w.hudOrder={100}; w.hudAppid[100]=10; w.hudIdentity[100]={2,7,11};
        w.appearances[10].fields.iconResID = 0; w.appearances[10].fields.iconState = "red";
        setFields(w.appearances[10], R"({"screenLoc":"8,1"})");
        auto frame = terrain.prepare(w,{},0,false);
        scene.prepare(w,frame,reader(),0,nullptr,true);
        QCOMPARE(scene.hudMargins(), QMargins(0,0,1,0));
        frame = terrain.prepare(w,{},0,false,scene.hudMargins());
        const auto sprites = scene.prepare(w,frame,reader(),0,nullptr,true);
        int left = INT_MAX, right = INT_MIN;
        for (const auto& s : sprites) if (s.kind==shell::MapSprite::Entity && s.cell.y()==6) {
            left = std::min(left, s.rect().left()); right = std::max(right, s.rect().right());
        }
        QVERIFY2(left <= frame.source.left() && right >= frame.source.right(), "the row of objects reaches the added column");

        struct Case { int control; QMargins hud; int stripX, stripY, endX, endY; };
        // Stock's map at the control's top left: world from stripX, stripY to endX, endY (exclusive).
        for (const Case& c : {Case{224,{0,0,1,0},16,0,224,224}, Case{224,{1,0,0,0},32,0,224,224},
                              Case{224,{0,0,2,0},32,0,224,224}, Case{224,{1,0,0,1},32,16,224,224},
                              Case{224,{0,2,0,0},0,64,224,224},
                              Case{240,{0,0,1,0},8,8,224,232}, Case{240,{1,0,0,0},32,8,240,232},
                              Case{240,{0,0,2,0},24,8,224,232},
                              Case{288,{0,0,1,0},16,32,240,256}, Case{288,{1,0,0,0},48,32,272,256},
                              Case{288,{0,0,2,0},0,32,224,256},
                              Case{192,{0,0,1,0},32,16,192,192}}) { // a strip on top too, where nothing was added
            const QSize sceneSize(224 + 32*(c.hud.left()+c.hud.right()), 224 + 32*(c.hud.top()+c.hud.bottom()));
            const QSize control(c.control, c.control);
            shell::MapPresentationSettings normal; normal.iconSize = 32;
            const auto placement = shell::mapPresentation(control, sceneSize, 32, normal);
            QCOMPARE(placement.scale, 1.0);
            const auto clip = shell::mapWorldClip(placement, control, sceneSize, c.hud, 32);
            QVERIFY(clip);
            // The clip in the control, cut by it.
            const QRect shown = clip->translated(placement.destination.topLeft().toPoint()).intersected(QRect({}, control));
            QCOMPARE(shown, QRect(QPoint(c.stripX, c.stripY), QPoint(c.endX - 1, c.endY - 1)));
        }
        // A scaled map: icon-size 48 in a control of 336 pixels is the 224 pixel case magnified 1.5 times. Stock draws
        // the world from 24 pixels across and the colour-matrix atoms stretched by 13/14 there too (8,1|obj|wide-fx|
        // 336x336|48: the strip 0..23, the swap at 69..90, the magenta turf at 248..290): the rule holds in scene pixels.
        {
            shell::MapPresentationSettings scaled; scaled.iconSize = 48;
            const QSize control(336, 336), sceneSize(256, 224);
            const auto placement = shell::mapPresentation(control, sceneSize, 32, scaled);
            QCOMPARE(placement.scale, 1.5);
            const auto clip = shell::mapWorldClip(placement, control, sceneSize, {0,0,1,0}, 32);
            QVERIFY(clip);
            QCOMPARE(*clip, QRect(32, 0, 208, 224));
            const QRectF shown = QRectF(*clip).translated(placement.destination.topLeft().x() / placement.scale, 0);
            QCOMPARE(QRectF(shown.x() * placement.scale, 0, shown.width() * placement.scale, 336), QRectF(24, 0, 312, 336));
            QCOMPARE(shell::mapWorldView(sceneSize, {0,0,1,0}, 32), QRect(0, 0, 224, 224));
        }
        // A map that is not enlarged: no clip, even in a control smaller than the view (stock: no strip at 192).
        shell::MapPresentationSettings normal; normal.iconSize = 32;
        for (int size : {192, 224, 288})
            QVERIFY(!shell::mapWorldClip(shell::mapPresentation(QSizeF(size, size), {224, 224}, 32, normal),
                                         QSizeF(size, size), {224, 224}, {}, 32));
    }
    // A range's grid is moved by its first end's pixel offsets and ignores the second end's (stock 516.1689,
    // master-screen-loc-world "1,1 to 3:8,3", "1,1 to 3:40,3", "1:8,1 to 3,3", "1:4,1:8 to 3:12,3", the same for a
    // master's overlays and an object), when it is written from its lower end.
    void masterRangePixelOffsetsFollowTheFirstEnd() {
        std::map<QString,QJsonObject> phases; QString error;
        QVERIFY2(masterLocPhases(phases, error), qPrintable(error));
        QRect plain;
        QVERIFY(masterLocBounds(phases, "1,1 to 3:8,3|ov", "overlay", plain));
        QCOMPARE(plain, QRect(QPoint(0,128), QPoint(79,207))); // three columns and rows of the 16 pixel overlay from 1,1
        for (const QString same : {"1,1 to 3:8,3|obj", "1,1 to 3:40,3|ov"}) {
            QRect r; QVERIFY2(masterLocBounds(phases, same, "overlay", r), qPrintable(same)); QCOMPARE(r, plain);
        }
        QVERIFY(phases.contains("1,1 to 3:8,3|alpha0"));
        QCOMPARE(phases["1,1 to 3:8,3|alpha0"]["counts"].toObject()["part"].toInt(-1), 0);
        QCOMPARE(phases["1:4,1:8 to 3:12,3|ov"]["bounds"], phases["1:4,1:8 to 3:12,3|obj"]["bounds"]);
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.bounds.viewW=w.bounds.viewH=7; // the view stock was measured in
        w.hudOrder={100}; w.hudAppid[100]=10; w.hudIdentity[100]={2,7,11};
        auto draw=[&](const std::string& loc) {
            setFields(w.appearances[10], std::string("{\"appearanceFlags\":268435456,\"hasPlane\":true,\"plane\":1,\"overlays\":[1],\"screenLoc\":\"") +
                loc + "\"}");
            std::vector<QPoint> at; int masters=0;
            for (const auto& s : scene.prepare(w,terrain.prepare(w,{},0,false),reader(),0,nullptr,true)) {
                if (s.kind==shell::MapSprite::Master) masters += s.planeEffects && s.planeEffects->supported ? 1 : -100;
                else if (s.kind==shell::MapSprite::Screen) at.push_back(s.position);
            }
            auto order=[](const QPoint& a,const QPoint& b) { return a.x()!=b.x() ? a.x()<b.x() : a.y()<b.y(); };
            std::sort(at.begin(),at.end(),order);
            return std::pair{masters,at};
        };
        const auto [masters,grid]=draw("1,1 to 3,3");
        QCOMPARE(masters,1); QCOMPARE(grid.size(),size_t(9));
        for (const char* loc : {"1,1 to 3:8,3", "1,1 to 3:40,3", "1:8,1 to 3,3", "1:4,1:8 to 3:12,3"}) {
            QRect stock;
            QVERIFY2(masterLocBounds(phases, QString(loc) + "|ov", "overlay", stock), loc);
            QCOMPARE(stock.size(), plain.size());
            const auto [n,moved]=draw(loc);
            QCOMPARE(n,1); QCOMPARE(moved.size(),size_t(9));
            for (size_t i=0;i<9;++i) QCOMPARE(moved[i]-grid[i], stock.topLeft()-plain.topLeft());
        }
        // A range with a percent end, or one written backwards with a pixel offset, is placed too and the master acts
        // (where each copy goes: masterLocStockPositions).
        for (const char* placed : {"1,1 to 50%,50%", "0%,0% to 50%,50%", "50%,50% to 6,6", "2,2 to 100%,3", "3:8,3 to 1,1"}) {
            QVERIFY2(phases.contains(QString(placed) + "|ov"), placed);
            const auto [n,copies]=draw(placed);
            QVERIFY2(n==1 && !copies.empty(), placed);
        }
    }
    // Every measured "|obj" and "|ov" phase of master-screen-loc-world that lists its "copies" (each cyan square's left,
    // top and visible pixels in the 224x224 window, stock 516.1689): the squares an object or a master's overlay at
    // that screen_loc shows here, and how much the map grows, as stock. Percent ranges, ranges written backwards with
    // pixel offsets, CENTER, LEFT and RIGHT ends, named maps and ends off the view are among them.
    void masterLocStockPositions() {
        std::map<QString,QJsonObject> phases; QString error;
        QVERIFY2(masterLocPhases(phases, error), qPrintable(error));
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.bounds.viewW=w.bounds.viewH=7; // the view stock was measured in
        w.hudOrder={100}; w.hudAppid[100]=10; w.hudIdentity[100]={2,7,11};
        auto overlays=[&](const QString& loc, const QMargins& margins) {
            setFields(w.appearances[10], std::string("{\"appearanceFlags\":268435456,\"hasPlane\":true,\"plane\":1,\"overlays\":[1],\"screenLoc\":\"") +
                loc.toStdString() + "\"}");
            // Overlay positions are taken from the world sprite's, which the larger scene moves too.
            std::vector<QPoint> at; int masters=0; QPoint entity;
            const auto frame=terrain.prepare(w,{},0,false,margins);
            for (const auto& s : scene.prepare(w,frame,reader(),0,nullptr,true)) {
                if (s.kind==shell::MapSprite::Master) masters += s.planeEffects && s.planeEffects->supported ? 1 : -100;
                else if (s.kind==shell::MapSprite::Screen) at.push_back(s.position);
                else if (s.kind==shell::MapSprite::Entity) entity=s.position;
            }
            for (auto& p : at) p -= entity;
            return std::tuple{masters,at,frame.source.translated(-entity)};
        };
        const auto [baseMasters,base,baseSource]=overlays("1,1",{});
        QCOMPARE(baseMasters,1); QCOMPARE(base.size(),size_t(1));
        int checked=0, masters=0;
        for (const auto& [phase,row] : phases) {
            if (!row.contains("copies") || !(phase.endsWith("|obj") || phase.endsWith("|ov"))) continue;
            const QString text=phase.section('|',0,0);
            if (text=="null" || text=="empty" || text=="bogus") continue; // no screen_loc: masterWithoutScreenLocIsInert
            std::vector<std::array<int,3>> stock;
            for (const auto& c : row["copies"].toArray()) {
                QVERIFY2(testinput::isIntegerTuple(c,3,0,50176),qPrintable(phase));
                stock.push_back({c[0].toInt(),c[1].toInt(),c[2].toInt()});
            }
            // Here: the 16x16 square at the top left of each copy's tile, in the window stock centres the map in.
            std::vector<std::array<int,3>> here;
            QMargins grown;
            const auto target=shell::ScreenLoc::target(text);
            QVERIFY2(target,qPrintable(phase));
            const auto loc=target->map.isEmpty() ? shell::ScreenLoc::parse(target->location) : std::nullopt;
            QVERIFY2(loc || !target->map.isEmpty(),qPrintable(phase));
            if (loc) {
                grown=loc->outside({7,7});
                for (const auto& p : loc->positions({7,7},32,grown)) {
                    const QRect square(p.x()+16*(grown.left()-grown.right()),192-p.y()+16*(grown.top()-grown.bottom()),16,16);
                    const QRect seen=square & QRect(0,0,224,224);
                    if (!seen.isEmpty()) here.push_back({seen.left(),seen.top(),seen.width()*seen.height()});
                }
            }
            std::sort(here.begin(),here.end(),[](const auto& a,const auto& b) { return std::pair(a[1],a[0])<std::pair(b[1],b[0]); });
            QVERIFY2(here==stock,qPrintable(phase));
            // How far the map grew: stock's red square, when it shows, moved by half of what was added on each side.
            QRect part;
            if (masterLocBounds(phases,phase,"part",part) && row["counts"].toObject()["part"].toInt()==256)
                QVERIFY2(part.topLeft()==QPoint(96+16*(grown.left()-grown.right()),96+16*(grown.top()-grown.bottom())),qPrintable(phase));
            ++checked;
            // A master's overlays stand where the object's squares do, and the master acts once. (Whether a master
            // on another map acts is "map:1,1|alpha0".)
            if (!phase.endsWith("|ov") || !loc) continue;
            overlays(text,{}); // the frame that learns of the placement asks for the larger map
            QCOMPARE(scene.hudMargins(),grown);
            const auto [n,at,source]=overlays(text,grown);
            QVERIFY2(n==1,qPrintable(phase));
            QCOMPARE(source.size(),baseSource.size()+QSize(32*(grown.left()+grown.right()),32*(grown.top()+grown.bottom())));
            std::vector<QPoint> expected;
            for (const auto& p : loc->positions({7,7},32,grown)) {
                const QPoint position=base[0]+QPoint(p.x(),-p.y());
                if (QRect(position,QSize(32,32)).intersects(source)) expected.push_back(position);
            }
            auto order=[](const QPoint& a,const QPoint& b) { return a.x()!=b.x() ? a.x()<b.x() : a.y()<b.y(); };
            auto got=at; std::sort(got.begin(),got.end(),order); std::sort(expected.begin(),expected.end(),order);
            QVERIFY2(got==expected,qPrintable(phase));
            ++masters;
        }
        QVERIFY(checked>=90); QVERIFY(masters>=40);
        // A master whose screen_loc names a map, even the default map's own id ("map" in this world's skin), does
        // nothing on the default map: the square shows (no alpha 0) and has no outline. A percent past the view
        // ("5+50%,1"), a percent range and a backwards range with offsets act.
        for (const QString loc : {"map:1,1|alpha0", "map:1,1 to 3,3|alpha0", "map:1,1|outline"}) {
            QVERIFY2(phases.contains(loc),qPrintable(loc));
            QCOMPARE(phases[loc]["counts"].toObject()["part"].toInt(-1),256);
            QCOMPARE(phases[loc]["counts"].toObject()["outline"].toInt(-1),0);
            const auto [n,at,source]=overlays(loc.section('|',0,0),{});
            QVERIFY2(n==0 && at.empty(),qPrintable(loc));
        }
        for (const QString loc : {"5+50%,1|alpha0", "1,1 to 100%,100%|alpha0", "3:8,3:8 to 1,1|alpha0", "50%,50% to 6,6|alpha0"}) {
            QVERIFY2(phases.contains(loc),qPrintable(loc));
            QCOMPARE(phases[loc]["counts"].toObject()["part"].toInt(-1),0);
            QVERIFY2(std::get<0>(overlays(loc.section('|',0,0),{}))==1,qPrintable(loc));
        }
    }
    // A master tree keeps its filters under a matrix: they run before it (stock 516.1689, master-transform-world
    // master-turn-outline1, master-scale-outline1, master-scale-blur2, master-turn-shadow6 match in the client).
    void masterTreeKeepsFiltersUnderMatrix() {
        auto w = world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.hudOrder = {100}; w.hudAppid[100] = 10; w.hudIdentity[100] = {2,7,11};
        w.appearances[11] = w.appearances[1];
        setFields(w.appearances[11], R"({"hasPlane":true,"plane":1,"pixelX":64,"pixelY":32})");
        const std::string outline = R"(,"filters":[{"type":2,"paramMask":35,"flags":1,"params":"0000803f00ff00ff0000"}])";
        for (const auto& matrix : {std::array<float,6>{1,0,0,0,1,0}, std::array<float,6>{0,1,0,-1,0,0}, std::array<float,6>{2,0,0,0,1,0}}) {
            setFields(w.appearances[10], affinefixture::fields(matrix,
                R"(,"appearanceFlags":268435456,"hasPlane":true,"plane":1,"screenLoc":"1,1","overlays":[11])" + outline));
            const auto sprites = scene.prepare(w, terrain.prepare(w, {}, 0, false), reader(), 0, nullptr, true);
            const auto master = std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.kind == shell::MapSprite::Master; });
            QVERIFY(master != sprites.end());
            QVERIFY(master->planeEffects && master->planeEffects->supported);
            QCOMPARE(master->planeEffects->filters.size(), size_t(1));
            QCOMPARE(master->planeEffects->ignoredFilters, size_t(0));
            const bool turned = matrix != std::array<float,6>{1,0,0,0,1,0};
            QCOMPARE(bool(master->planeEffects->matrix), turned);
        }
    }
    // HUD plane-master trees with matrices (tests/data/master-transform-world, stock 516.1688): the root master's matrix
    // turns its plane (the plane relay) and also each overlay about its own pivot; an overlay's own matrix applies first.
    // With KEEP_TOGETHER a master matrix draws nothing, and the overlay is hidden with or without a matrix.
    void masterTreeTransformsLikeStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/master-transform-world/stock.json";
        QJsonDocument doc; QString error;
        QVERIFY2(testinput::readRecordedJson(path, doc, error), qPrintable(error));
        const auto cases = doc.object()["cases"].toArray();
        QSet<QString> remaining{"plain", "master-turn", "master-scale", "master-shift", "child-turn",
                                "master-turn-child-scale", "master-scale-child-turn", "solo-master-turn",
                                "group-plain", "group-master-turn", "group-child-turn"};
        QVERIFY2(cases.size() == remaining.size(), qPrintable(path + ": expected 11 retained master-tree cases"));
        for (const auto& value : cases) {
            const auto row = value.toObject();
            const QString name = row["case"].toString(), context = path + ": case " + name + ": ";
            QVERIFY2(value.isObject() && remaining.remove(name), qPrintable(context + "missing, unknown or duplicate case name"));
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{row["fugueDiffOver8"]}, 1, 0, std::numeric_limits<int>::max()),
                     qPrintable(context + "fugueDiffOver8 must be an explicit nonnegative integer"));
            if (name.startsWith("group") || name.startsWith("solo")) {
                QVERIFY2(row["blue"].isNull(), qPrintable(context + "blue must be explicitly null for the hidden overlay"));
            } else {
                QVERIFY2(testinput::isIntegerTuple(row["blue"], 4, 0, 223),
                         qPrintable(context + "blue must retain four integer bounds in the 224-pixel map"));
                const auto blue = row["blue"].toArray();
                QVERIFY2(blue[0].toInt() <= blue[2].toInt() && blue[1].toInt() <= blue[3].toInt(),
                         qPrintable(context + "blue bounds must be ordered"));
            }
        }
        const float c = 0.70710677f;
        int overlayClicks = 0;
        for (const auto& value : cases) {
            const auto row = value.toObject(); const auto name = row["case"].toString();
            QCOMPARE(row["fugueDiffOver8"].toInt(), 0);
            std::array<float,6> master{1,0,0,0,1,0}, child{1,0,0,0,1,0};
            if (name.contains("master-turn")) master = {0,1,0,-1,0,0};
            if (name.contains("master-scale")) master = {2,0,0,0,1,0};
            if (name.contains("master-shift")) master = {1,0,16,0,1,8};
            if (name.contains("child-turn")) child = {c,c,0,-c,c,0};
            if (name.contains("child-scale")) child = {2,0,0,0,1,0};
            const bool group = name.startsWith("group"), solo = name.startsWith("solo");
            auto w = world(); shell::TurfMap terrain; shell::EntityMap scene;
            w.hudOrder = {100}; w.hudAppid[100] = 10; w.hudIdentity[100] = {2,7,11};
            const std::string flags = group ? "335544320" : "268435456"; // PLANE_MASTER (| KEEP_TOGETHER)
            setFields(w.appearances[10], affinefixture::fields(master, ",\"appearanceFlags\":" + flags +
                R"(,"hasPlane":true,"plane":1,"screenLoc":"1,1")" + (solo ? "" : R"(,"overlays":[11])")));
            w.appearances[11] = w.appearances[1]; w.appearances[11].fields.iconState = "green";
            setFields(w.appearances[11], affinefixture::fields(child, R"(,"hasPlane":true,"plane":1,"pixelX":64,"pixelY":32)"));
            const auto frame = terrain.prepare(w, {}, 0, false);
            const auto sprites = scene.prepare(w, frame, reader(), 0, nullptr, true);
            const auto masterSprite = std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.kind == shell::MapSprite::Master; });
            const auto overlay = std::find_if(sprites.begin(), sprites.end(), [](const auto& s) {
                return s.kind == shell::MapSprite::Screen && s.image.pixelColor(16, 16) == QColor(Qt::green); });
            // The overlay is clicked where it is drawn, as the master, with its own icon coordinates (the same
            // fixture's clicks: master-turn, master-scale, master-turn-child-scale).
            if (!group && !solo && overlay != sprites.end() && masterSprite != sprites.end() && masterSprite->planeEffects &&
                masterSprite->planeEffects->matrix && !name.contains("child-turn")) {
                const auto plane = shell::StaticAffine::make(*masterSprite->planeEffects->matrix, frame.source.size());
                QVERIFY(plane);
                const QPointF drawn = plane->forward.map(QPointF(overlay->rect().center() - frame.source.topLeft()) + QPointF(.5, .5));
                if (QRectF(QPointF(), frame.source.size()).contains(drawn)) {
                    const auto hit = shell::mapPointerAt(drawn, frame.source.size(), frame.source.size() / 32, frame.source,
                                                         frame.geometry, "map", true, sprites);
                    QVERIFY2(hit && hit->screen, qPrintable(name));
                    QVERIFY2(std::abs(hit->click.iconX - 16) <= 2 && std::abs(hit->click.iconY - 16) <= 2, qPrintable(name));
                    ++overlayClicks;
                }
            }
            if (group) { // grabs matched before and after this change (KEEP_TOGETHER keeps its own master path)
                QVERIFY(row["blue"].isNull());
                continue;
            }
            QVERIFY2(masterSprite != sprites.end() && masterSprite->planeEffects && masterSprite->planeEffects->supported, qPrintable(name));
            const bool turnedPlane = master != std::array<float,6>{1,0,0,0,1,0};
            QCOMPARE(bool(masterSprite->planeEffects->matrix), turnedPlane);
            QCOMPARE(row["blue"].isNull(), group || solo);
            if (group || solo) continue;
            QVERIFY2(overlay != sprites.end(), qPrintable(name));
            // The stock blue box's size from the 16 px box: the overlay's own affine, then the plane's matrix.
            const QTransform l = overlay->affine ? overlay->affine->forward : QTransform();
            const double ex = (std::abs(l.m11()) + std::abs(l.m21())) * 16, ey = (std::abs(l.m12()) + std::abs(l.m22())) * 16;
            const double fx = std::abs(master[0]) * ex + std::abs(master[1]) * ey, fy = std::abs(master[3]) * ex + std::abs(master[4]) * ey;
            if (!name.contains("child-turn")) {
                const auto b = row["blue"].toArray();
                QVERIFY2(std::abs(b[2].toInt() - b[0].toInt() + 1 - fx) <= 2 && std::abs(b[3].toInt() - b[1].toInt() + 1 - fy) <= 2,
                         qPrintable(QString("%1: %2x%3").arg(name).arg(fx).arg(fy)));
            } else QVERIFY(overlay->affine && l.m12() != 0);
        }
        QVERIFY(overlayClicks >= 2);
    }
    // The same fixture's clicks.json (stock 516.1689): what stock named, with icon-x/y, for left clicks at MAP pixels on
    // and round the master's overlay, a 16 px box at 8..23 of its icon. The pointer goes back through the plane's matrix
    // and the overlay's without being rounded to a pixel in between, which a plane scale of 2, 3 or 1.5 shows.
    void masterTreeClicksLikeStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/master-transform-world/clicks.json";
        QJsonDocument doc; QString error;
        QVERIFY2(testinput::readRecordedJson(path, doc, error), qPrintable(error));
        const float c45 = 0.70710677f, c30 = 0.8660254f;
        QSet<QString> remaining{"master-turn", "master-scale", "master-turn-child-scale", "master-scale-child-turn",
                                "master-scale-child-lean", "master-triple-child-turn", "master-grow",
                                "master-grow-child-turn", "master-grow-child-lean"};
        int onOverlay = 0, offOverlay = 0;
        for (const auto& value : doc.object()["cases"].toArray()) {
            const auto row = value.toObject(); const auto name = row["case"].toString();
            if (!remaining.remove(name)) continue; // filters, a lone master and untransformed trees have their own tests
            std::array<float,6> master{1,0,0,0,1,0}, child{1,0,0,0,1,0};
            if (name.contains("master-turn")) master = {0,1,0,-1,0,0};
            if (name.contains("master-scale")) master = {2,0,0,0,1,0};
            if (name.contains("master-triple")) master = {3,0,0,0,1,0};
            if (name.contains("master-grow")) master = {1.5f,0,0,0,1.5f,0};
            if (name.contains("child-turn")) child = {c45,c45,0,-c45,c45,0};
            if (name.contains("child-lean")) child = {c30,.5f,0,-.5f,c30,0};
            if (name.contains("child-scale")) child = {2,0,0,0,1,0};
            auto w = world(); shell::TurfMap terrain; shell::EntityMap scene;
            w.bounds = {true, 20, 20, 0, 7, 7, 7, 7}; // the fixture's 7x7 view: the plane turns about its centre
            w.atoms.clear(); // the fixture's red box is clicked as itself; only the overlay is rebuilt here
            w.hudOrder = {100}; w.hudAppid[100] = 10; w.hudIdentity[100] = {2,7,11};
            setFields(w.appearances[10], affinefixture::fields(master,
                R"(,"appearanceFlags":268435456,"hasPlane":true,"plane":1,"screenLoc":"1,1","overlays":[11])"));
            w.appearances[11] = w.appearances[1]; w.appearances[11].fields.iconState = "green";
            setFields(w.appearances[11], affinefixture::fields(child, R"(,"hasPlane":true,"plane":1,"pixelX":64,"pixelY":32)"));
            const auto frame = terrain.prepare(w, {}, 0, false);
            QCOMPARE(frame.source.size(), QSize(224, 224));
            auto sprites = scene.prepare(w, frame, reader(), 0, nullptr, true);
            const auto overlay = std::find_if(sprites.begin(), sprites.end(), [](const auto& s) {
                return s.kind == shell::MapSprite::Screen && s.image.pixelColor(16, 16) == QColor(Qt::green); });
            QVERIFY2(overlay != sprites.end() && overlay->image.size() == QSize(32, 32), qPrintable(name));
            QImage box(32, 32, QImage::Format_RGBA8888); box.fill(Qt::transparent);
            for (int y = 8; y < 24; ++y) for (int x = 8; x < 24; ++x) box.setPixelColor(x, y, Qt::green);
            overlay->image = box;
            const auto clicks = row["clicks"].toArray();
            QVERIFY2(!clicks.isEmpty(), qPrintable(name));
            for (const auto& entry : clicks) {
                const auto click = entry.toObject(); const auto over = click["over"].toString();
                const QString context = QString("%1 at %2,%3").arg(name).arg(click["at"][0].toInt()).arg(click["at"][1].toInt());
                QVERIFY2(testinput::isIntegerTuple(click["at"], 2, 0, 223) &&
                         testinput::isIntegerTuple(QJsonArray{click["iconX"], click["iconY"]}, 2, 1, 32) &&
                         (over == "master" || over == "floor" || over == "part"), qPrintable(context));
                if (over == "part") continue;
                const auto hit = shell::mapPointerAt(QPointF(click["at"][0].toInt(), click["at"][1].toInt()), frame.source.size(),
                                                     frame.source.size() / 32, frame.source, frame.geometry, "map", true, sprites);
                QVERIFY2(hit, qPrintable(context));
                if (over == "floor") { QVERIFY2(!hit->screen, qPrintable(context)); ++offOverlay; continue; }
                QVERIFY2(hit->screen && hit->click.iconX == click["iconX"].toInt() && hit->click.iconY == click["iconY"].toInt(),
                         qPrintable(QString("%1: stock x=%2 y=%3, got%4 x=%5 y=%6").arg(context).arg(click["iconX"].toInt())
                                    .arg(click["iconY"].toInt()).arg(hit->screen ? "" : " the floor").arg(hit->click.iconX)
                                    .arg(hit->click.iconY)));
                ++onOverlay;
            }
        }
        QVERIFY2(remaining.isEmpty(), qPrintable(path + ": missing case " + remaining.values().join(", ")));
        QVERIFY2(onOverlay >= 120 && offOverlay >= 10, qPrintable(QString("%1 on, %2 off").arg(onOverlay).arg(offOverlay)));
    }
    // PIXEL_SCALE on a turned master reaches its sprite, alone or over a tree, so the planner samples the plane
    // unsmoothed (stock 516.1689, master-transform-world fx-pxs-scalex2); without it the master stays bilinear.
    void masterPixelScaleReachesSprite() {
        for (const bool pixelScale : {false, true}) for (const bool tree : {false, true}) {
            auto w = world(); shell::TurfMap terrain; shell::EntityMap scene;
            w.hudOrder = {100}; w.hudAppid[100] = 10; w.hudIdentity[100] = {2,7,11};
            // PLANE_MASTER, and PIXEL_SCALE: packed DM bits 7 and 9 (0x200000 << k)
            const uint32_t flags = 0x10000000u | (pixelScale ? 0x40000000u : 0u);
            setFields(w.appearances[10], affinefixture::fields({2,0,0,0,1,0}, ",\"appearanceFlags\":" + std::to_string(flags) +
                R"(,"hasPlane":true,"plane":1,"screenLoc":"1,1")" + (tree ? R"(,"overlays":[11])" : "")));
            w.appearances[11] = w.appearances[1];
            setFields(w.appearances[11], R"({"hasPlane":true,"plane":1,"pixelX":64,"pixelY":32})");
            const auto frame = terrain.prepare(w, {}, 0, false);
            const auto sprites = scene.prepare(w, frame, reader(), 0, nullptr, true);
            const auto master = std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.kind == shell::MapSprite::Master; });
            const auto context = QString("pixelScale=%1 tree=%2").arg(pixelScale).arg(tree);
            QVERIFY2(master != sprites.end() && master->planeEffects && master->planeEffects->supported &&
                     master->planeEffects->matrix, qPrintable(context));
            QVERIFY2(master->nearest == pixelScale, qPrintable(context));
        }
    }
    // An animating plane master keeps its filters and its matrix: both reach the master sprite at their tweened values,
    // in a single step and in a parallel one, and hold at their ends (stock 516.1689, master-transform-world ax-*: the
    // held ends draw what a settled master with those values draws, and mid-flight the plane keeps its outline and turns).
    void animatedMasterKeepsFiltersAndMatrix() {
        auto w = world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.hudOrder = {100}; w.hudIdentity[100] = {2,7,11};
        auto master = [](std::array<float,6> matrix, const char* size) {
            return affinefixture::fields(matrix, std::string(R"(,"appearanceFlags":268435456,"hasPlane":true,"plane":1,"screenLoc":"1,1")") +
                R"(,"filters":[{"type":2,"paramMask":35,"flags":1,"params":")" + size + R"(00ff00ff0000"}])");
        };
        const std::array<float,6> plain{1,0,0,0,1,0}, wide{2,0,0,0,1,0}, turn{0,1,0,-1,0,0};
        setFields(w.appearances[10], master(plain, "0000803f")); // outline 1
        setFields(w.appearances[11], master(wide, "00008040"));  // outline 4, scaled 2x across
        setFields(w.appearances[12], master(turn, "0000803f"));
        setFields(w.appearances[13], master(turn, "00008040"));
        const auto map = terrain.prepare(w, {}, 0, false);
        auto sprite = [&](int64_t now) -> std::optional<shell::MapSprite> {
            const auto sprites = scene.prepare(w, map, reader(), now, nullptr, true);
            const auto it = std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.kind == shell::MapSprite::Master; });
            if (it == sprites.end()) return std::nullopt;
            return *it;
        };
        auto check = [&](int64_t now, double size, std::array<double,6> matrix) {
            const auto s = sprite(now);
            const auto context = QString("at %1").arg(now);
            QVERIFY2(s && s->planeEffects && s->planeEffects->supported, qPrintable(context));
            QVERIFY2(s->planeEffects->filters.size() == 1 && s->planeEffects->ignoredFilters == 0, qPrintable(context));
            QVERIFY2(std::abs(s->planeEffects->filters[0].size - size) < 1e-6, qPrintable(context));
            QVERIFY2(s->planeEffects->matrix, qPrintable(context));
            for (int i = 0; i < 6; ++i) QVERIFY2(std::abs((*s->planeEffects->matrix)[i] - matrix[i]) < 1e-6, qPrintable(context));
        };
        // One step: outline 1 -> 4 and identity -> 2x across, then a hold at the end, as world.dm's ax-* cases do.
        auto& atom = w.atoms[100];
        w.hudAppid[100] = atom.appid = 11;
        atom.chain = {1000, 100, {{10, 11, std::bit_cast<uint32_t>(10.f), 0, 0, 1, 1000}, {11, 11, std::bit_cast<uint32_t>(100.f), 0, 0, 1, 0}}};
        check(1500, 2.5, {1.5,0,0,0,1,0});
        check(1750, 3.25, {1.75,0,0,0,1,0});
        check(2500, 4, {2,0,0,0,1,0});   // held
        check(20000, 4, {2,0,0,0,1,0});  // settled
        // A turn with a parallel step on the outline (ax-outline1-size4-spin90): the matrix rotates, the outline grows.
        w.hudAppid[100] = atom.appid = 13;
        atom.chain = {1000, 100, {{10, 12, std::bit_cast<uint32_t>(10.f), 0, 0, 1, 1000}, {12, 13, std::bit_cast<uint32_t>(10.f), 0, 4, 1, 1000}}};
        const auto half = shell::animation::interpolate({1,0,0,0,1,0}, {0,1,0,-1,0,0}, .5, false);
        check(1500, 2.5, half);
        check(2500, 4, {0,1,0,-1,0,0});
    }
    // Filters set on an animating master after its animate() calls are drawn at once while the animation plays on, the
    // list added to or cleared, with the matrix still tweening (stock 516.1689, master-transform-world ax-outline1-lateadd,
    // -lateclear; on an atom, ax-pline-lateadd, which animatedMasterReplay draws).
    void animatedMasterLateFilters() {
        auto w = world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.hudOrder = {100}; w.hudIdentity[100] = {2,7,11};
        const std::string outline = R"({"type":2,"paramMask":35,"flags":1,"params":"0000803f00ff00ff0000"})";
        const std::string blur = R"({"type":1,"flags":1,"paramMask":1,"params":{"bstr":"0000803f"}})";
        auto master = [](std::array<float,6> matrix, const std::string& extra) {
            return affinefixture::fields(matrix, R"(,"appearanceFlags":268435456,"hasPlane":true,"plane":1,"screenLoc":"1,1")" + extra);
        };
        const std::array<float,6> plain{1,0,0,0,1,0}, turn{0,1,0,-1,0,0};
        const auto half = shell::animation::interpolate({1,0,0,0,1,0}, {0,1,0,-1,0,0}, .5, false);
        const auto map = terrain.prepare(w, {}, 0, false);
        auto sprite = [&](int64_t now) { return masterSprite(scene.prepare(w, map, reader(), now, nullptr, true)); };
        auto matrixIs = masterMatrixIs;
        auto& atom = w.atoms[100];
        // A turn from an outlined master; the settled master then gains a blur after the outline, or loses its list.
        setFields(w.appearances[10], master(plain, R"(,"filters":[)" + outline + "]"));
        setFields(w.appearances[11], master(turn, R"(,"filters":[)" + outline + "]"));
        setFields(w.appearances[12], master(turn, R"(,"filters":[)" + outline + "," + blur + "]"));
        setFields(w.appearances[13], master(turn, ""));
        atom.chain = {1000, 100, {{10, 11, std::bit_cast<uint32_t>(10.f), 0, 0, 1, 1000}}};
        for (const uint32_t live : {12u, 13u}) {
            w.hudAppid[100] = atom.appid = live;
            const auto s = sprite(1500);
            QVERIFY2(s && s->planeEffects && s->planeEffects->supported && matrixIs(*s, half), qPrintable(QString::number(live)));
            QCOMPARE(s->planeEffects->filters.size(), live == 12 ? size_t(2) : size_t(0));
            if (live == 12) QCOMPARE(s->planeEffects->filters[1].kind, shell::PlaneFilter::Blur);
        }
    }
    // KEEP_TOGETHER on a master with nothing to group changes nothing: its matrix turns the plane, settled and animating;
    // with an overlay and a matrix nothing of the tree is drawn, settled or animating (stock 516.1689,
    // master-transform-world ax-group-turn90, ax-group-spin90, ax-tree-group-turn90, ax-tree-group-spin90).
    void animatedGroupMaster() {
        auto w = world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.hudOrder = {100}; w.hudIdentity[100] = {2,7,11};
        const std::array<float,6> plain{1,0,0,0,1,0}, turn{0,1,0,-1,0,0};
        const auto half = shell::animation::interpolate({1,0,0,0,1,0}, {0,1,0,-1,0,0}, .5, false);
        const auto map = terrain.prepare(w, {}, 0, false);
        auto sprite = [&](int64_t now) { return masterSprite(scene.prepare(w, map, reader(), now, nullptr, true)); };
        auto matrixIs = masterMatrixIs;
        auto& atom = w.atoms[100];
        // PLANE_MASTER | KEEP_TOGETHER on a master without overlays, settled and animating.
        const std::string group = R"(,"appearanceFlags":335544320)";
        auto grouped = [&](std::array<float,6> matrix, const std::string& extra) {
            return affinefixture::fields(matrix, group + R"(,"hasPlane":true,"plane":1,"screenLoc":"1,1")" + extra);
        };
        setFields(w.appearances[20], grouped(plain, ""));
        setFields(w.appearances[21], grouped(turn, ""));
        w.hudAppid[100] = atom.appid = 21;
        atom.chain = {};
        auto settled = sprite(1500);
        QVERIFY2(settled && matrixIs(*settled, {0,1,0,-1,0,0}), "settled KEEP_TOGETHER master without overlays turns its plane");
        atom.chain = {1000, 100, {{20, 21, std::bit_cast<uint32_t>(10.f), 0, 0, 1, 1000}}};
        auto turning = sprite(1500);
        QVERIFY2(turning && matrixIs(*turning, half), "animating KEEP_TOGETHER master without overlays turns its plane");
        // With an overlay the tree draws nothing while it is turned, as when it is settled.
        w.appearances[22] = w.appearances[1];
        setFields(w.appearances[22], R"({"hasPlane":true,"plane":1,"pixelX":64,"pixelY":32})");
        setFields(w.appearances[20], grouped(plain, R"(,"overlays":[22])"));
        setFields(w.appearances[21], grouped(turn, R"(,"overlays":[22])"));
        for (const bool animating : {false, true}) {
            atom.chain = animating ? model::Atom::Chain{1000, 100, {{20, 21, std::bit_cast<uint32_t>(10.f), 0, 0, 1, 1000}}}
                                   : model::Atom::Chain{};
            const auto sprites = scene.prepare(w, map, reader(), 1500, nullptr, true);
            // The refused tree leaves a master whose plane operation is unsupported, which hides its plane.
            const auto context = animating ? "animating" : "settled";
            QVERIFY2(std::none_of(sprites.begin(), sprites.end(), [](const auto& s) { return s.kind == shell::MapSprite::Screen; }), context);
            QVERIFY2(std::all_of(sprites.begin(), sprites.end(), [](const auto& s) {
                return s.kind != shell::MapSprite::Master || (s.planeEffects && !s.planeEffects->supported); }), context);
        }
    }
    void hudMasterMaterialAnimation() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        w.hudOrder={100}; w.hudAppid[100]=11; w.hudIdentity[100]={2,7,11};
        auto fields=[](const shell::ColorMatrix& color) {
            const auto hex=QByteArray(reinterpret_cast<const char*>(color.data()),80).toHex().toStdString();
            return std::string("{\"appearanceFlags\":268435456,\"screenLoc\":\"CENTER,CENTER\",\"overlays\":[1],\"vars\":[{\"bit\":1024,\"data\":\"")+
                hex+"\"}]}";
        };
        auto scalar=[](int red) {
            return std::string("{\"appearanceFlags\":268435456,\"screenLoc\":\"CENTER,CENTER\",\"overlays\":[1],\"hasColor\":true,\"colorR\":")+
                std::to_string(red)+",\"colorG\":255,\"colorB\":255,\"colorA\":255}";
        };
        auto start=shell::identityColor, end=start; end[0]=.25f; end[1]=.2f; end[5]=.5f; end[16]=.1f;
        setFields(w.appearances[10], scalar(255)); setFields(w.appearances[11], scalar(127));
        auto& atom=w.atoms[100]; atom.appid=11;
        atom.chain={1000,100,{{10,11,std::bit_cast<uint32_t>(10.f),0,0,1,1000}}};
        const auto map=terrain.prepare(w,{},0,false);
        auto draw=[&](int64_t now) { return scene.prepare(w,map,reader(),now,&stats,true); };
        auto master=[](const auto& sprites)->const shell::MapSprite& {
            return *std::find_if(sprites.begin(),sprites.end(),[](const auto& s){return s.kind==shell::MapSprite::Master;});
        };
        auto leaf=[](const auto& sprites)->const shell::MapSprite& {
            return *std::find_if(sprites.begin(),sprites.end(),[](const auto& s){return s.kind==shell::MapSprite::Screen;});
        };
        const auto first=draw(1000); QCOMPARE(first.size(),3); QCOMPARE(master(first).kind,shell::MapSprite::Master);
        const auto half=draw(1500); QCOMPARE(half.size(),3);
        QCOMPARE(master(half).tint[0],191.f/255);
        QCOMPARE(leaf(half).tint,master(half).tint); // inherited before plane material
        QCOMPARE(leaf(half).image.cacheKey(),leaf(first).image.cacheKey()); QCOMPARE(stats.frameDecodes,uint64_t(0));
        const auto later=draw(1750); QCOMPARE(master(later).tint[0],159.f/255);
        QCOMPARE(master(first).tint[0],1.f); QCOMPARE(master(half).tint[0],191.f/255);
        const auto saved=w.appearances[10]; w.appearances.erase(10);
        QCOMPARE(draw(1600).size(),1); w.appearances[10]=saved;
        QCOMPARE(master(draw(1750)).tint[0],159.f/255); // late endpoint does not restart
        const auto settled=draw(2100); QCOMPARE(master(settled).tint[0],127.f/255);
        QCOMPARE(draw(2200).constData(),settled.constData()); QCOMPARE(stats.templateBuilds,uint64_t(0));
        // Existing chain scheduling: parallel material deltas and infinite loops.
        setFields(w.appearances[12], scalar(0));
        w.hudAppid[100]=atom.appid=12;
        atom.chain.steps.push_back({11,12,std::bit_cast<uint32_t>(10.f),0,4,1,1000});
        QCOMPARE(master(draw(1500)).tint[0],128.f/255);
        w.hudAppid[100]=atom.appid=10;
        atom.chain.steps.resize(1); atom.chain.steps[0].loop=-1;
        atom.chain.steps.push_back({11,10,std::bit_cast<uint32_t>(10.f),0,0,1,0});
        QCOMPARE(master(draw(3500)).tint,master(half).tint);
        w.hudAppid[100]=atom.appid=11;
        atom.chain.steps.resize(1); atom.chain.steps[0].loop=1;
        for(uint8_t easing:{2,5}) { atom.chain.steps[0].easing=easing; QCOMPARE(draw(1500).size(),3); } // CIRCULAR, ELASTIC animate
        atom.chain.steps[0].easing=63; QCOMPARE(draw(1500).size(),1); // unknown curve
        atom.chain.steps[0].easing=0;
        // Placement/ownership transitions are not material interpolation.
        for(const auto* addition:{",\"pixelX\":1",",\"plane\":2,\"hasPlane\":true"}) {
            auto json=scalar(255); json.insert(json.size()-1,addition); setFields(w.appearances[10], json);
            QCOMPARE(draw(1500).size(),1);
        }
        // A filter list that is not the same at both ends switches at the step's midpoint, as on an atom. DreamDaemon
        // 516.1689 never sends such a step for a DM animate() (both ends carry the filters the atom has when animate() is
        // called), so this is Fugue's own rule, not a measured one.
        {
            auto json=scalar(255);
            json.insert(json.size()-1,",\"filters\":[{\"type\":1,\"flags\":1,\"paramMask\":1,\"params\":{\"bstr\":\"0000803f\"}}]");
            setFields(w.appearances[10], json);
            QCOMPARE(master(draw(1400)).planeEffects->filters.size(),size_t(1));
            QVERIFY(master(draw(1600)).planeEffects->filters.empty());
        }
        w.appearances[10]=saved;
        auto bad=start; bad[1]=std::numeric_limits<float>::infinity(); setFields(w.appearances[10], fields(bad));
        QCOMPARE(draw(1500).size(),1); w.appearances[10]=saved;
        // Changing color matrices need independent tween evidence, not an
        // assumed coefficient lerp or a discrete midpoint approximation.
        setFields(w.appearances[10], fields(end)); QCOMPARE(draw(1500).size(),1);
        w.appearances[10]=saved;
        // The VG no-op tween must keep its master present for its whole lifetime.
        setFields(w.appearances[11], fields(end));
        atom.chain={1000,100,{{11,11,std::bit_cast<uint32_t>(100.f),0,0,1,1000}}};
        for(int64_t now:{1000,1500,10999,11000,12000}) QCOMPARE(master(draw(now)).color,end);
        w.hudOrder.clear(); QCOMPARE(draw(1500).size(),1);
        QCOMPARE(master(half).tint[0],191.f/255); // published frames remain immutable
    }
    void chainInstanceTimingAndImmutableGeometry() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto map=terrain.prepare(w,{},0,false);
        const auto base=scene.prepare(w,map,reader()); QCOMPARE(base.size(),1);
        w.appearances[2]=w.appearances[1]; setFields(w.appearances[2], R"({"pixelX":32})");
        w.appearances[2].fields.hasPixelOffsets=true;
        auto& a=w.atoms[42]; a.appid=2; a.pixelX=32; a.animating=true;
        a.chain={1000,100,{{1,2,std::bit_cast<uint32_t>(10.f),0,0,1,1000}}};
        w.atoms[43]=a; w.atoms[43].chain.startNs=1250; w.atoms[43].chain.steps[0].startNs=1250;
        const auto half=scene.prepare(w,map,reader(),1500,&stats); QCOMPARE(half.size(),2);
        QCOMPARE(half[0].position,base[0].position+QPoint(16,0));
        QCOMPARE(half[1].position,base[0].position+QPoint(8,0));
        QCOMPARE(half[0].image.cacheKey(),base[0].image.cacheKey()); QCOMPARE(stats.frameDecodes,uint64_t(0));
        const auto later=scene.prepare(w,map,reader(),1750,&stats); QCOMPARE(later.size(),2);
        QCOMPARE(later[0].position,base[0].position+QPoint(24,0));
        QCOMPARE(half[0].position,base[0].position+QPoint(16,0)); // immutable publication
        QCOMPARE(stats.frameDecodes,uint64_t(0));
        // Picking is relative to the rendered sample, even after a newer sample exists.
        auto hit=shell::mapPointerAt({52,48},{96,96},{3,3},map.source,map.geometry,"map",true,half);
        QVERIFY(hit); QCOMPARE(hit->click.over.ref,43u);
        w.atoms.erase(43);
        // A direct A6 set lives on the atom; the settled flyweight has no copy.
        w.appearances[4]=w.appearances[1];
        a.appid=4; a.pixelX=6;
        const auto shifted=scene.prepare(w,map,reader(),1500); QCOMPARE(shifted.size(),1);
        QCOMPARE(shifted[0].position,base[0].position+QPoint(-10,0)); // direct set shifts the tween
        // Endpoint and resource arrival never restart the authoritative clock.
        const auto endpoint=w.appearances[1]; w.appearances.erase(1);
        QVERIFY(scene.prepare(w,map,reader(),1600).empty()); w.appearances[1]=endpoint;
        w.resources[0]={456,0}; bool ready=false;
        auto late=[&](uint32_t,QByteArray& out){out=iconBytes();return ready;};
        QVERIFY(scene.prepare(w,map,late,1600).empty()); ready=true;
        const auto recovered=scene.prepare(w,map,late,1750); QCOMPARE(recovered.size(),1);
        QCOMPARE(recovered[0].position,base[0].position+QPoint(-2,0));
        a.hasPos=false; QVERIFY(scene.prepare(w,map,late,1800).empty()); a.hasPos=true;
        const auto settled=scene.prepare(w,map,late,2100); QCOMPARE(settled.size(),1);
        QCOMPARE(settled[0].position,base[0].position+QPoint(6,0));
    }
    void chainSettledOverlaysAndDiscreteState() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        w.appearances[2]=w.appearances[1]; w.appearances[2].fields.iconState="green";
        setFields(w.appearances[2], R"({"pixelX":16,"overlays":[3],"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":128})");
        w.appearances[2].fields.hasPixelOffsets=true;
        w.appearances[3]=w.appearances[1]; setFields(w.appearances[3], R"({"pixelY":16})");
        auto& atom=w.atoms[42];atom.appid=2;atom.pixelX=16;atom.animating=true;
        atom.chain={1000,100,{{1,2,std::bit_cast<uint32_t>(10.f),65,0,1,1000}}};
        const auto map=terrain.prepare(w,{},0,false);
        const auto early=scene.prepare(w,map,reader(),1499);QCOMPARE(early.size(),2);
        QCOMPARE(early[0].image.pixelColor(16,16),QColor(Qt::red));
        QCOMPARE(early[1].position-early[0].position,QPoint(0,-16)); // live overlay stack, not endpoint stack
        const auto middle=scene.prepare(w,map,reader(),1500);QCOMPARE(middle.size(),2);
        QCOMPARE(middle[0].image.pixelColor(16,16),QColor(Qt::green)); // RAW midpoint, not eased progress
        QCOMPARE(middle[0].tint,middle[1].tint);
        QCOMPARE(early[1].image.cacheKey(),middle[1].image.cacheKey());
        const auto later=scene.prepare(w,map,reader(),1800,&stats);QCOMPARE(later.size(),2);
        QCOMPARE(later[0].image.cacheKey(),middle[0].image.cacheKey());QCOMPARE(stats.frameDecodes,uint64_t(0));
        const auto settled=scene.prepare(w,map,reader(),2100);QCOMPARE(settled.size(),2);
        QCOMPARE(scene.prepare(w,map,reader(),2200,&stats).constData(),settled.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); // finished chains regain the ordinary cached path
    }
    void imageChainSamplingAndTargetLifetime() {
        auto w=world(); w.atoms[42].renderIdentity=10;
        w.appearances[2]=w.appearances[1]; w.appearances[2].fields.iconState="green";
        w.appearances[3]=w.appearances[2]; setFields(w.appearances[3], R"({"pixelY":16,"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":0})");
        w.imageInstances[7]={3,42,1,false,20,10};
        w.imageChains[7]={1000,100,{{2,3,std::bit_cast<uint32_t>(10.f),0,0,1,1000}}};
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto map=terrain.prepare(w,{},0,false);
        const auto first=scene.prepare(w,map,reader(),1000,nullptr,true); QCOMPARE(first.size(),2);
        const auto half=scene.prepare(w,map,reader(),1500,&stats,true); QCOMPARE(half.size(),2);
        QCOMPARE(half[1].position,first[1].position+QPoint(0,-8));
        QCOMPARE(half[1].tint[3],128.f/255); QCOMPARE(first[1].tint[3],1.f);
        QCOMPARE(half[1].image.cacheKey(),first[1].image.cacheKey()); QCOMPARE(stats.frameDecodes,uint64_t(0));
        w.atoms[42].renderIdentity=11;
        QCOMPARE(scene.prepare(w,map,reader(),1600,nullptr,true).size(),1); // bind-once target, no retargeting
        w.imageInstances[7].targetIdentity=11; w.imageInstances[7].identity=21;
        w.imageChains.erase(7); w.imageInstances[7].appid=2;
        const auto rebound=scene.prepare(w,map,reader(),1700,nullptr,true); QCOMPARE(rebound.size(),2);
        QCOMPARE(rebound[1].imageIdentity,uint64_t(21)); QCOMPARE(rebound[1].tint[3],1.f);
        QCOMPARE(half[1].imageIdentity,uint64_t(20));
        w.imageInstances.erase(7); QCOMPARE(scene.prepare(w,map,reader(),1800,nullptr,true).size(),1);
    }
    void easingCurves() {
        using shell::animation::ease;
        for (uint8_t e : {0,1,2,3,4,5,6,7,1|64,1|128,4|64,5|192,6|128}) { // exact endpoints
            QCOMPARE(ease(0,e),0.0); QCOMPARE(ease(1,e),1.0);
        }
        QCOMPARE(ease(.5,3|64),.125); QCOMPARE(ease(.5,7|64),.25);          // CUBIC/QUAD in
        QCOMPARE(ease(.5,3),.5); QCOMPARE(ease(.25,7),.125);               // in-out by default
        QVERIFY(std::abs(ease(.5,2|64)-(1-std::sqrt(.75)))<1e-12);         // CIRCULAR in
        QVERIFY(ease(.9,4)>.95 && ease(.9,4)<1);                           // BOUNCE defaults to out
        QVERIFY(ease(.1,6|64)<0);                                          // BACK pulls back first
        QVERIFY(ease(.2,5)>1);                                             // ELASTIC (out) overshoots
        QCOMPARE(ease(.1,8),1.0); QCOMPARE(ease(.1,8|128),1.0);           // JUMP at the start
        QCOMPARE(ease(.9,8|64),0.0);                                       // EASE_IN: at the end
        QCOMPARE(ease(.4,8|192),0.0); QCOMPARE(ease(.6,8|192),1.0);        // JUMP in-out: middle
        QVERIFY(shell::animation::supportedEasing(8)); QVERIFY(!shell::animation::supportedEasing(9));
    }
    // Every curve x direction against stock samples (tests/data/easing-world/README.md). A sample's clock is the
    // linear row read from the same frame, so the tolerance is a pixel plus the curve's slope times about a pixel of
    // clock error.
    void easingMatchesStock() {
        using shell::animation::ease;
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/easing-world/stock.json";
        QJsonDocument document;QString readError;
        QVERIFY2(testinput::readRecordedJson(path,document,readError),qPrintable(readError));
        const auto doc=document.object();
        const auto columns=doc["columns"].toArray(), frames=doc["frames"].toArray();
        QStringList names{"linear"};
        for (int curve=1;curve<=8;++curve) for (int direction:{0,64,128,192})
            names.append(QString("%1:%2").arg(curve).arg(direction));
        QCOMPARE(columns.size(),names.size());QCOMPARE(frames.size(),146);
        for (int c=0;c<columns.size();++c) QCOMPARE(columns[c].toString(),names[c]);
        int missing=0;
        for (const auto& frame:frames) {
            const auto values=frame.toArray();QCOMPARE(values.size(),columns.size());
            QVERIFY(testinput::isNumberTuple(QJsonArray{values[0]},1));
            QVERIFY(values[0].toDouble()>=0 && values[0].toDouble()<=1);
            for (int c=1;c<values.size();++c) {
                if (values[c].isNull()) {QCOMPARE(names[c],QString("5:64"));++missing;}
                else QVERIFY2(testinput::isNumberTuple(QJsonArray{values[c]},1),qPrintable(path+": invalid sample for "+names[c]));
            }
        }
        QCOMPARE(missing,14); // ELASTIC EASE_IN retains fourteen explicit nulls; overshoot values are valid.
        const double px=1.0/320;
        int checked=0;
        for (int c=1;c<columns.size();++c) {
            const auto parts=columns[c].toString().split(':');
            const uint8_t easing=uint8_t(parts[0].toInt()|parts[1].toInt());
            for (const auto& row:frames) {
                const auto r=row.toArray(); if (r[c].isNull()) continue;
                const double t=r[0].toDouble(), v=r[c].toDouble();
                const double slope=std::abs(ease(std::min(1.0,t+px),easing)-ease(std::max(0.0,t-px),easing))/2;
                const double error=std::abs(ease(t,easing)-v);
                QVERIFY2(error<=1.5*px+2*slope,qPrintable(QString("easing %1 at t=%2: stock %3, fugue %4")
                    .arg(easing).arg(t).arg(v).arg(ease(t,easing))));
                ++checked;
            }
        }
        QCOMPARE(checked,4658);
    }
    // Transform tweens against stock (tests/data/transform-tween-world/README.md): every sampled pose of every case,
    // through the same per-step composition the renderer uses. Samples where the image is nearly flat
    // (|det| < 0.35) cannot be measured and are skipped.
    void transformTweenMatchesStock() {
        namespace an = shell::animation;
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/transform-tween-world/stock.json";
        QJsonDocument document;QString error;
        QVERIFY2(testinput::readRecordedJson(path,document,error),qPrintable(error));
        const auto sets=document.object()["sets"].toObject();
        QCOMPARE(sets.keys(),(QStringList{"a","b"}));
        auto matrix=[](const QJsonValue& v) { const auto a=v.toArray(); an::Matrix m;
            for (int i=0;i<6;++i) m[size_t(i)]=a[i].toDouble(); return m; };
        int checked=0;
        for (const char* key:{"a","b"}) {
            const auto set=sets[key].toObject();
            const auto cases=set["cases"].toArray(),frames=set["frames"].toArray();
            QCOMPARE(cases.size(),QString(key)=="a" ? 18 : 12);QCOMPARE(frames.size(),164);
            QStringList names;int stepCount=0;
            for (int c=0;c<cases.size();++c) {
                const auto row=cases[c].toObject();
                const auto name=row["name"].toString();
                QVERIFY2(!name.isEmpty() && name==name.trimmed() && !names.contains(name),qPrintable(path+": missing or duplicate transform case name"));
                names.append(name);
                QVERIFY2(testinput::isNumberTuple(row["from"],6),qPrintable(path+": invalid starting matrix for "+names[c]));
                const auto steps=row["steps"].toArray();
                QVERIFY(steps.size()==1 || steps.size()==2);stepCount+=int(steps.size());
                for (const auto& value:steps) {
                    const auto step=value.toObject();
                    QVERIFY2(testinput::isNumberTuple(step["to"],6),qPrintable(path+": invalid target matrix for "+names[c]));
                    QVERIFY(testinput::isIntegerTuple(QJsonArray{step["flags"]},1,0,2));
                    QVERIFY(step["flags"].toInt()==0 || step["flags"].toInt()==2);
                }
            }
            QCOMPARE(stepCount,QString(key)=="a" ? 20 : 12);
            int missing=0;
            for (const auto& frame:frames) {
                const auto values=frame.toArray();QCOMPARE(values.size(),cases.size()+1);
                QVERIFY(testinput::isNumberTuple(QJsonArray{values[0]},1));
                QVERIFY(values[0].toDouble()>=0 && values[0].toDouble()<=1);
                for (int c=1;c<values.size();++c) {
                    if (values[c].isNull()) ++missing;
                    else QVERIFY2(testinput::isNumberTuple(values[c],6),qPrintable(path+": invalid sampled matrix for "+names[c-1]));
                }
            }
            QCOMPARE(missing,QString(key)=="a" ? 498 : 382); // Unmeasurable poses are explicitly retained as null.
            for (int c=0;c<cases.size();++c) {
                const auto row=cases[c].toObject(); const auto steps=row["steps"].toArray();
                // The chain as the server sends it: 200 ds in one step or 100 ds in each of two.
                model::Atom::Chain chain; chain.tickNs=1'000'000; chain.startNs=0;
                std::vector<an::Matrix> values{matrix(row["from"])};
                for (int i=0;i<steps.size();++i) {
                    const auto st=steps[i].toObject(); values.push_back(matrix(st["to"]));
                    model::Atom::AnimStep step{uint32_t(i),uint32_t(i+1),std::bit_cast<uint32_t>(steps.size()==1?200.f:100.f),0};
                    step.flags=uint32_t(st["flags"].toInt()); chain.steps.push_back(step);
                }
                for (const auto& frame:frames) {
                    const auto f=frame.toArray(); if (f[c+1].isNull()) continue;
                    const double t=f[0].toDouble(); if (steps.size()>1 && std::abs(t-.5)<.02) continue;
                    const auto stock=matrix(f[c+1]);
                    if (std::abs(stock[0]*stock[4]-stock[1]*stock[3])<.35) continue;
                    an::Matrix out{}; uint32_t last=0;
                    QVERIFY(an::composeChain(chain,int64_t(t*200'000'000),[&](uint32_t id){return values[id];},
                        [](const an::Matrix& a,const an::Matrix& b,double,double e,uint32_t flags){
                            return an::interpolate(a,b,e,(flags&2u)!=0);},
                        [](an::Matrix&,const an::Matrix&,const an::Matrix&,const an::Matrix&,double,double,uint32_t){},out,last));
                    for (double element:out) QVERIFY(std::isfinite(element));
                    double linear=0,translation=0;
                    for (size_t i:{0,1,3,4}) linear=std::max(linear,std::abs(out[i]-stock[i]));
                    for (size_t i:{2,5}) translation=std::max(translation,std::abs(out[i]-stock[i]));
                    QVERIFY2(linear<=.1 && translation<=.6,qPrintable(QString("%1 at t=%2: linear %3 translation %4")
                        .arg(row["name"].toString()).arg(t).arg(linear).arg(translation)));
                    ++checked;
                }
            }
        }
        QCOMPARE(checked,3709); // Preserve the retained null, near-flat and step-boundary exclusions.
    }
    // Stock samples a transformed or scaled icon bilinearly unless the icon's own appearance has PIXEL_SCALE; a
    // parent's flag does not reach its overlays (tests/data/static-transform-world/README.md).
    void transformSamplingMatchesStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/static-transform-world/stock.json";
        QJsonDocument doc; QString error;
        QVERIFY2(testinput::readRecordedJson(path, doc, error), qPrintable(error));
        const QJsonValue caseValue = doc.object()["expect"].toObject()["cases"];
        QVERIFY2(caseValue.isArray(), qPrintable(path + ": expect.cases must be an array"));
        const auto cases = caseValue.toArray();
        QSet<QString> remaining{"rot45", "rot45 ps", "scale2", "scale2 ps", "scale1.5", "scale1.5 ps",
                                "scale0.75", "scale0.75 ps", "half", "half ps", "shift.5", "shift.5 ps",
                                "shear", "shear ps", "rot30scale", "rot30scale ps",
                                "ps-parent", "ps-child", "ps-rot-parent", "ps-rot-child"};
        const QSet<QString> childCases{"ps-parent", "ps-child", "ps-rot-parent", "ps-rot-child"};
        QVERIFY2(cases.size() == remaining.size(), qPrintable(path + ": expected 20 retained sampling cases"));
        for (qsizetype row = 0; row < cases.size(); ++row) {
            const auto c = cases[row].toObject();
            const QString context = path + QString(": expect.cases row %1 (%2): ").arg(row + 1).arg(c["case"].toString());
            QVERIFY2(cases[row].isObject() && c["case"].isString() && remaining.remove(c["case"].toString()),
                     qPrintable(context + "missing, unknown or duplicate case name"));
            QVERIFY2(testinput::isNumberTuple(c["matrix"], 6), qPrintable(context + "matrix requires six finite numbers"));
            for (const auto& element : c["matrix"].toArray())
                QVERIFY2(std::abs(element.toDouble()) <= std::numeric_limits<float>::max(),
                         qPrintable(context + "matrix element exceeds the replay float range"));
            QVERIFY2(c["flags"].isDouble() && (c["flags"].toDouble() == 0 || c["flags"].toDouble() == 512),
                     qPrintable(context + "flags must explicitly be 0 or 512"));
            QVERIFY2(c["root"].toString() == "bilinear" || c["root"].toString() == "nearest",
                     qPrintable(context + "root sampling must be bilinear or nearest"));
            const bool hasChild = childCases.contains(c["case"].toString());
            QVERIFY2(c.contains("child") == hasChild, qPrintable(context + "child observation does not match the retained case"));
            if (hasChild) {
                const auto child = c["child"].toObject();
                QVERIFY2(c["child"].isObject(), qPrintable(context + "child must be an object"));
                QVERIFY2(child["flags"].isDouble() && (child["flags"].toDouble() == 0 || child["flags"].toDouble() == 512),
                         qPrintable(context + "child.flags must explicitly be 0 or 512"));
                QVERIFY2(child["sampling"].toString() == "bilinear" || child["sampling"].toString() == "nearest",
                         qPrintable(context + "child.sampling must be bilinear or nearest"));
            }
        }
        for (const auto& value:cases) {
            const auto c=value.toObject(); const auto m=c["matrix"].toArray();
            std::array<float,6> matrix; for (int i=0;i<6;++i) matrix[size_t(i)]=float(m[i].toDouble());
            const auto packed=[](int dmFlags) { return QString::number(dmFlags&512 ? 0x40000000u : 0u).toStdString(); };
            auto w=world();
            std::string extra=",\"appearanceFlags\":"+packed(c["flags"].toInt());
            if (c.contains("child")) {
                extra+=",\"overlays\":[2]";
                w.appearances[2]=w.appearances[1]; w.appearances[2].fields.layerBits=std::bit_cast<uint32_t>(4.0f);
                setFields(w.appearances[2], "{\"pixelX\":10,\"appearanceFlags\":"+packed(c["child"].toObject()["flags"].toInt())+"}");
            }
            setFields(w.appearances[1], affinefixture::fields(matrix,extra));
            shell::TurfMap terrain; shell::EntityMap scene; const auto map=terrain.prepare(w,{},0,false);
            const auto sprites=scene.prepare(w,map,reader(),1000);
            QVERIFY2(sprites.size()==(c.contains("child")?2:1),qPrintable(c["case"].toString()));
            QVERIFY2(sprites[0].smooth==(c["root"].toString()=="bilinear"),qPrintable(c["case"].toString()+" root"));
            if (c.contains("child"))
                QVERIFY2(sprites[1].smooth==(c["child"].toObject()["sampling"].toString()=="bilinear"),
                         qPrintable(c["case"].toString()+" child"));
        }
    }
    // Offset tweens truncate the eased change toward zero: from + trunc((to - from) * f), against stock
    // (tests/data/parallel-world/README.md). The stock clock row is itself truncated, so each sample is taken at the
    // middle of its clock pixel, and samples within clock error of a whole-pixel step are skipped.
    void offsetTweensTruncateLikeStock() {
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/parallel-world/stock.json";
        QJsonDocument document;QString error;
        QVERIFY2(testinput::readRecordedJson(path,document,error),qPrintable(error));
        const auto doc=document.object();
        const auto names=doc["cases"].toArray(); const auto frames=doc["stockFrames"].toArray();
        const QStringList expected{"par-turn+scale","par-turn+move","par-turn+turn","par-turn+translate","img-follow","vis-follow",
            "move-turned","par-delayed","neg-move","pos-move-y","m12-to-12","12-to-m12"};
        QCOMPARE(names.size(),expected.size());QCOMPARE(frames.size(),147);
        for (int c=0;c<names.size();++c) QCOMPARE(names[c].toString(),expected[c]);
        for (const auto& frame:frames) {
            const auto values=frame.toArray();QCOMPARE(values.size(),names.size()+1);
            QVERIFY(testinput::isNumberTuple(QJsonArray{values[0]},1));
            QVERIFY(values[0].toDouble()>=0 && values[0].toDouble()<=1);
            for (int c=1;c<values.size();++c)
                QVERIFY2(testinput::isNumberTuple(values[c],6),qPrintable(path+": invalid sampled matrix for "+expected[c-1]));
        }
        struct Case { const char* name; bool y; int from, to; };
        int checked=0;
        for (const Case c:{Case{"m12-to-12",false,-12,12},Case{"12-to-m12",false,12,-12},Case{"neg-move",false,0,-24},
                           Case{"neg-move",true,0,-24},Case{"pos-move-y",true,0,24}}) {
            int column=-1; for (int i=0;i<names.size();++i) if (names[i].toString()==c.name) column=i+1;
            QVERIFY(column>0);
            int misses=0;
            auto w=world(); w.appearances[2]=w.appearances[1];
            auto fields=[&](int v) { return QString(c.y?R"({"pixelY":%1})":R"({"pixelX":%1})").arg(v).toStdString(); };
            setFields(w.appearances[1], fields(c.from)); setFields(w.appearances[2], fields(c.to));
            auto& atom=w.atoms[42]; atom.appid=2;
            atom.chain={0,1'000'000,{{1,2,std::bit_cast<uint32_t>(200.f),0,0,1,0}}};
            shell::TurfMap terrain; shell::EntityMap scene; const auto map=terrain.prepare(w,{},0,false);
            atom.chain={}; const auto rest=scene.prepare(w,map,reader(),0);
            atom.chain={0,1'000'000,{{1,2,std::bit_cast<uint32_t>(200.f),0,0,1,0}}};
            QCOMPARE(rest.size(),1);
            for (const auto& frame:frames) {
                const auto f=frame.toArray(); if (f[column].isNull()) continue;
                const double t=f[0].toDouble()+.5/320, change=double(c.to-c.from)*t;
                const double part=std::abs(change)-std::floor(std::abs(change));
                if (part<.05 || part>.95) continue;
                const auto m=f[column].toArray();
                // The arm's feature centroid sits 0.13 px right of and 0.01 px below its pivot when unturned.
                const int stock=int(std::lround(m[c.y?5:2].toDouble()-(c.y?-.01:.13)));
                const auto sprites=scene.prepare(w,map,reader(),int64_t(t*200'000'000)); QCOMPARE(sprites.size(),1);
                const QPoint moved=sprites[0].position-rest[0].position;
                // rest is the settled appearance, at the To offset; screen y grows downward.
                const int fugue=c.to+(c.y?-moved.y():moved.x());
                if (fugue!=stock) {
                    // One captured frame (clock 0.153) shows every tween a pixel past its clock row.
                    QVERIFY2(++misses<=1,qPrintable(QString("%1 at t=%2: stock %3, fugue %4").arg(c.name).arg(t).arg(stock).arg(fugue)));
                }
                ++checked;
            }
        }
        QCOMPARE(checked,650); // 130 retained samples per variant after the unchanged clock-boundary exclusion.
    }
    // vis_contents against stock 516.1688 (tests/data/vis-world): vis_flags (set b) and turf/mob members (sets a, d).
    // Each case is rebuilt with the fixture's icons, composited in draw order, and read at compare.py's probe pixels.
    void visFlagsMatchStock_data() {
        QTest::addColumn<QString>("name"); QTest::addColumn<int>("flags"); QTest::addColumn<double>("layer");
        QTest::addColumn<int>("plane");
        for (const auto& [name, flags, layer, plane] : std::vector<std::tuple<const char*, int, double, int>>{
                 {"none", 0, 3, 0}, {"icon", 1, 6, 0}, {"state", 2, 6, 0}, {"dir", 4, 6, 0}, {"layer", 8, 3, 0},
                 {"plane", 0x10, 3, 1}, {"plane+layer", 0x18, 3, 1}, {"id", 0x20, 6, 0}, {"underlay", 0x40, 6, 0},
                 {"underlay-l3", 0x40, 3, 0}, {"underlay+layer", 0x48, 6, 0}, {"float", 0, -1, 0},
                 {"underlay-float", 0x40, -1, 0}, {"hide", 0x80, 6, 0}, {"all-inherit", 63, 3, 1}, {"plane-holder", 0x10, 3, 0},
                 {"hide-grand", 0, 6, 0}})
            QTest::newRow(name) << QString(name) << flags << layer << plane;
    }
    void visFlagsMatchStock() {
        QFETCH(QString, name); QFETCH(int, flags); QFETCH(double, layer); QFETCH(int, plane);
        QJsonObject stock; QString error;
        QVERIFY2(visStockCase("b", name, stock, error), qPrintable(error));
        const QString context = QString(FUGUE_TEST_DATA_DIR) + "/vis-world/stock.json: sets.b case " + name + ": ";
        QVERIFY2(stock["drawn"].isBool(), qPrintable(context + "drawn must be an explicit boolean"));
        if (stock["drawn"].toBool()) {
            for (const char* field : {"look", "dir"})
                QVERIFY2(stock[field].isString() && !stock[field].toString().isEmpty(),
                         qPrintable(context + field + " must be a nonempty string"));
            QVERIFY2(stock["overWall"].isBool(), qPrintable(context + "overWall must be an explicit boolean"));
            // The holder's own colour cannot distinguish which copy is on top.
            QVERIFY2(stock.contains("overHolder") && (stock["look"].toString() == "holder.a" ?
                         stock["overHolder"].isNull() : stock["overHolder"].isBool()),
                     qPrintable(context + "overHolder must be boolean, or explicitly null for holder.a"));
        }
        VisFixture f;
        const auto varsFor = [](int value) {
            return QString(R"("vars":[{"bit":8192,"data":"%1%2"}])").arg(value & 255, 2, 16, QChar('0')).arg(value >> 8, 2, 16, QChar('0'))
                .toStdString();
        };
        const std::string holderPlane = name == "plane-holder" ? R"(,"hasPlane":true,"plane":1)" : "";
        const uint32_t holder = f.atom(42, f.look(VisFixture::Holder, "a", 5, R"({"dir":4)" + holderPlane + "}"), 6, 6);
        f.atom(44, f.look(VisFixture::Wall, "", 4), 6, 6);
        f.floor(6, 6, f.look(VisFixture::Floor, "", 2));
        std::string kid = R"({"pixelX":12,"pixelY":-12)";
        if (plane) kid += R"(,"hasPlane":true,"plane":)" + std::to_string(plane);
        if (flags) kid += "," + varsFor(flags);
        f.atom(43, f.look(VisFixture::Kid, "b", float(layer), kid + "}"));
        f.vis(holder, f.w.atoms[holder].renderIdentity, {{2, 43}});
        if (name == "hide" || name == "hide-grand") {
            f.atom(45, f.look(VisFixture::Kid, "b", 6, R"({"pixelX":-24,"pixelY":24)" + (name == "hide-grand" ? "," + varsFor(0x80) : "") + "}"));
            f.vis(43, f.w.atoms[43].renderIdentity, {{2, 45}});
        }
        const auto image = f.tile(6, 6);
        const auto at = [&](int x, int y) { return VisFixture::name(image.pixelColor(x, y)); };
        const QString look = at(33, 33);
        QCOMPARE(look != "floor" && look != "black", stock["drawn"].toBool());
        if (!stock["drawn"].toBool()) return;
        QCOMPARE(look, stock["look"].toString());
        const bool big = look.startsWith("holder");
        const QString dir = at(big ? 38 : 34, 27) == "white" ? "east" : at(27, big ? 38 : 34) == "white" ? "south" : "?";
        QCOMPARE(dir, stock["dir"].toString());
        QCOMPARE(at(28, 30) != "wall", stock["overWall"].toBool());
        if (!stock["overHolder"].isNull()) QCOMPARE(at(22, 22) == look, stock["overHolder"].toBool());
        if (name.startsWith("hide")) QCOMPARE(at(1, 1), QString("floor")); // the grandchild's box would start here
    }
    // vis-world set e: a turf holder lends its layer to FLOAT_LAYER copies, and its VIS_UNDERLAY copies draw under the
    // turf; a VIS_UNDERLAY member of a member draws just before that member; an animating member keeps its vis_flags.
    void visLeftoversMatchStock_data() {
        QTest::addColumn<QString>("name");
        for (const char* name : {"turf-float", "turf-underlay-float", "nested-underlay", "nested-float", "anim-kid-layer"})
            QTest::newRow(name) << QString(name);
    }
    void visLeftoversMatchStock() {
        QFETCH(QString, name);
        QJsonObject stock; QString error;
        QVERIFY2(visStockCase("e", name, stock, error), qPrintable(error));
        const QString context = QString(FUGUE_TEST_DATA_DIR) + "/vis-world/stock.json: sets.e case " + name + ": ";
        for (const char* field : {"fugueDiffOver8", name.startsWith("nested") ? "whiteInk" : "kidInk"}) {
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{stock[field]}, 1, 0, std::numeric_limits<int>::max()),
                     qPrintable(context + field + " must be an explicit nonnegative integer"));
        }
        QJsonObject nestedFloat;
        if (name.startsWith("nested")) {
            QVERIFY2(visStockCase("e", "nested-float", nestedFloat, error), qPrintable(error));
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{nestedFloat["whiteInk"]}, 1, 0, std::numeric_limits<int>::max()),
                     qPrintable(context + "nested-float.whiteInk must be an explicit nonnegative integer"));
        }
        QCOMPARE(stock["fugueDiffOver8"].toInt(), 0);
        VisFixture f;
        const uint32_t floor = f.look(VisFixture::Floor, "", 2);
        for (int y = 1; y <= 20; ++y) for (int x = 1; x <= 20; ++x) f.floor(x, y, floor);
        // vis_flags ride on var bit 0x2000 (low byte first): VIS_UNDERLAY 0x40, VIS_INHERIT_LAYER 0x08.
        auto kidLook = [&](int px, int py, float layer, const char* flags = nullptr) {
            QString fields = QString(R"({"pixelX":%1,"pixelY":%2)").arg(px).arg(py);
            if (flags) fields += QString(R"(,"vars":[{"bit":8192,"data":"%1"}])").arg(flags);
            return f.look(VisFixture::Kid, "b", layer, (fields + "}").toStdString());
        };
        shell::MapSprites drawn;
        if (name.startsWith("turf-")) {
            const uint32_t k = f.atom(60, kidLook(8, 4, -1, name == "turf-underlay-float" ? "4000" : nullptr));
            f.vis(model::VisContents::turf(5, 5, 0), 0, {{2, k}});
            const auto image = f.tile(6, 6, &drawn);
            bool kid = false;
            for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) kid |= VisFixture::name(image.pixelColor(x, y)) == "kid.b";
            QCOMPARE(kid, stock["kidInk"].toInt() > 0);
            return;
        }
        const uint32_t holder = f.atom(42, f.look(VisFixture::Holder, "a", 5), 6, 6);
        if (name.startsWith("nested")) {
            const uint32_t kid = f.atom(60, kidLook(8, 4, 5));
            const uint32_t grand = f.atom(61, kidLook(4, -6, -1, name == "nested-underlay" ? "4000" : nullptr));
            f.vis(kid, f.w.atoms[kid].renderIdentity, {{2, grand}});
            f.vis(holder, f.w.atoms[holder].renderIdentity, {{2, kid}});
            f.tile(6, 6, &drawn);
            auto at = [&](uint32_t appid) {
                return std::find_if(drawn.begin(), drawn.end(), [&](const auto& s) { return s.appearanceId == appid; }) - drawn.begin();
            };
            const auto h = at(f.w.atoms[holder].appid), k = at(f.w.atoms[kid].appid), g = at(f.w.atoms[grand].appid);
            QVERIFY(h < drawn.size() && k < drawn.size() && g < drawn.size());
            QVERIFY(h < k && h < g);
            // Stock: the kid's notch shows only when the grandchild draws under the kid.
            QCOMPARE(g < k, stock["whiteInk"].toInt() > nestedFloat["whiteInk"].toInt());
            return;
        }
        // An animating member with VIS_INHERIT_LAYER is drawn mid-animation.
        const uint32_t kid = f.atom(60, kidLook(12, -12, 3, "0800"));
        auto& atom = f.w.atoms[kid];
        const uint32_t to = kidLook(12, 0, 3, "0800");
        atom.chain = {0, 100'000'000, {{atom.appid, to, std::bit_cast<uint32_t>(200.f), 0, 0, 1, 0}}}; atom.appid = to;
        f.vis(holder, f.w.atoms[holder].renderIdentity, {{2, kid}});
        f.tile(6, 6, &drawn);
        QVERIFY(stock["kidInk"].toInt() > 0);
        QVERIFY(std::any_of(drawn.begin(), drawn.end(), [](const auto& s) { return s.kind == shell::MapSprite::Vis; }));
    }
    void visTurfAndMobMembersMatchStock_data() {
        QTest::addColumn<QString>("set"); QTest::addColumn<QString>("name");
        for (const char* set : {"a", "d"})
            for (const char* name : {"obj-turf", "obj-turf-om", "obj-turf-px", "turf-turf", "obj-mob", "turf-mob", "obj-turf-vis",
                                     "obj-mix", "obj-turf-layer", "obj-mob-turf", "obj-turf-bare", "obj-turf-high", "obj-turf-south",
                                     "obj-l2-north", "obj-l2-south", "obj-l2-null", "mob-holder", "mob-holder-id"})
                QTest::newRow(qPrintable(QString("%1 %2").arg(set, name))) << QString(set) << QString(name);
    }
    void visTurfAndMobMembersMatchStock() {
        QFETCH(QString, set); QFETCH(QString, name);
        QJsonObject stock; QString error;
        QVERIFY2(visStockCase(set, name, stock, error), qPrintable(error));
        const QString context = QString(FUGUE_TEST_DATA_DIR) + "/vis-world/stock.json: sets." + set + " case " + name + ": ";
        const QStringList fields = name.startsWith("mob-holder") ? QStringList{"npc", "kid"} :
                                                                 QStringList{"turf", "thing", "npc", "kid"};
        for (const auto& field : fields)
            QVERIFY2(stock[field].isBool(), qPrintable(context + field + " must be an explicit boolean"));
        VisFixture f;
        const int z = set == "d" ? 1 : 0; // set d keeps the remote turfs on another level
        const uint32_t floor = f.look(VisFixture::Floor, "", 2);
        for (int y = 1; y <= 20; ++y) for (int x = 1; x <= 20; ++x) f.floor(x, y, floor);
        const uint32_t remoteLook = f.look(VisFixture::Remote, "", 2);
        const uint32_t thing = f.look(VisFixture::Thing, "", 3), npc = f.look(VisFixture::Npc, "", 4);
        uint32_t next = 60;
        // A remote turf north of the case (or south), with what stands on it.
        auto remote = [&](int x, bool south, const QString& what, uint32_t appid) {
            const int y = south ? 1 : 20;
            f.w.tiles[{uint16_t(x - 1), uint16_t(y - 1), uint16_t(z)}] = appid;
            if (what.contains('o')) { auto& a = f.w.atoms[f.atom(next++, thing, x, y)]; a.z = z; }
            if (what.contains('m')) { auto& a = f.w.atoms[f.atom(model::kRegistryA | next++, npc, x, y)]; a.z = z; }
            return model::VisReference{1, f.flat(x, y) + uint32_t(z * f.w.bounds.maxX * f.w.bounds.maxY)};
        };
        const bool turfHolder = name.startsWith("turf-");
        std::string holderFields = name == "obj-turf-px" ? R"({"pixelX":6,"pixelY":-4})" : "{}";
        const uint32_t holder = f.atom(42, f.look(VisFixture::Holder, "a", name == "obj-turf-layer" ? 1 : 5, holderFields), 6, 6);
        if (turfHolder) f.w.atoms.erase(holder);
        const uint64_t holderKey = turfHolder ? model::VisContents::turf(5, 5, 0) : holder;
        const uint64_t holderIdentity = turfHolder ? 0 : f.w.atoms[holder].renderIdentity;
        auto kid = [&](int px, int py, float layer) {
            return f.atom(next++, f.look(VisFixture::Kid, "b", layer, QString(R"({"pixelX":%1,"pixelY":%2})").arg(px).arg(py).toStdString()));
        };
        std::vector<model::VisReference> members;
        if (name == "obj-turf") members.push_back(remote(2, false, "", remoteLook));
        else if (name == "obj-turf-om" || name == "turf-turf") members.push_back(remote(4, false, "om", remoteLook));
        else if (name == "obj-turf-px" || name == "obj-turf-layer") members.push_back(remote(6, false, "o", remoteLook));
        else if (name == "obj-turf-bare") { members.push_back(remote(3, false, "o", remoteLook)); f.w.tiles.erase({5, 5, 0}); }
        else if (name == "obj-turf-high") members.push_back(remote(5, false, "o", f.look(VisFixture::Remote, "", 6)));
        else if (name == "obj-turf-south") members.push_back(remote(13, true, "", remoteLook));
        else if (name == "obj-mob" || name == "turf-mob") {
            const uint32_t mob = f.atom(model::kRegistryA | next++, npc, 17, 1);
            if (name == "turf-mob") setFields(f.w.appearances[npc], R"({"pixelX":4})");
            members.push_back({3, mob & ~model::kRegistryA});
        } else if (name == "obj-mob-turf") {
            const uint32_t mob = f.atom(model::kRegistryA | next++, npc, 16, 1);
            f.vis(mob, f.w.atoms[mob].renderIdentity, {remote(18, false, "", remoteLook)});
            members.push_back({3, mob & ~model::kRegistryA});
        } else if (name == "obj-turf-vis") {
            const auto turf = remote(10, false, "", remoteLook);
            f.vis(model::VisContents::turf(9, 19, uint16_t(z)), 0, {{2, kid(-6, -6, 5)}});
            members.push_back(turf);
        } else if (name == "obj-mix") {
            members.push_back(remote(12, false, "o", remoteLook)); members.push_back({2, kid(8, 4, 5)});
        } else if (name.startsWith("mob-holder")) {
            // A mob holding an obj (vg's typing indicator); the flags case inherits id, plane and layer.
            const uint32_t mob = f.atom(model::kRegistryA | next++, npc, 6, 6);
            const uint32_t k = f.atom(next++, f.look(VisFixture::Kid, "b", 5, name == "mob-holder" ? R"({"pixelX":8,"pixelY":4})" :
                R"({"pixelX":8,"pixelY":4,"vars":[{"bit":8192,"data":"3800"}]})"));
            f.vis(mob, f.w.atoms[mob].renderIdentity, {{2, k}});
            f.w.atoms.erase(holder);
            f.tile(6, 6); // the holder list below names no holder
            const auto image = f.tile(6, 6);
            const auto at = [&](int x, int y) { return VisFixture::name(image.pixelColor(x, y)); };
            QCOMPARE(at(3, 20) == "npc", stock["npc"].toBool());
            QCOMPARE(at(29, 10) == "kid.b", stock["kid"].toBool());
            return;
        } else {
            const uint32_t k = kid(-8, -8, 2);
            if (name == "obj-l2-north") { auto& a = f.w.atoms[k]; a.hasPos = true; a.x = 9; a.y = 20; }
            if (name == "obj-l2-south") { auto& a = f.w.atoms[k]; a.hasPos = true; a.x = 14; a.y = 1; }
            members.push_back({2, k});
        }
        f.vis(holderKey, holderIdentity, members);
        const auto image = f.tile(6, 6);
        const int dx = name == "obj-turf-px" ? 6 : name == "turf-mob" ? 4 : 0, dy = name == "obj-turf-px" ? 4 : 0;
        const auto at = [&](int x, int y) { return VisFixture::name(image.pixelColor(x + dx, y + dy)); };
        QCOMPARE(at(2, 2) == "white", stock["turf"].toBool());
        QCOMPARE(at(28, 22) == "thing", stock["thing"].toBool());
        QCOMPARE(at(3, 20) == "npc", stock["npc"].toBool());
        QCOMPARE(at(10, 20) == "kid.b" || at(29, 10) == "kid.b" || at(2, 30) == "kid.b", stock["kid"].toBool());
    }
    void visUnderTransformedHolderMatchesStock_data() {
        QTest::addColumn<QString>("name");
        for (const char* name : {"rot90", "scale2", "rot45", "rot90-px", "kid-own", "shift", "nested", "kid-reset", "kid-ps",
                                 "holder-ps", "turf-rot", "scale-half"})
            QTest::newRow(name) << QString(name);
    }
    // A transformed holder's matrix reaches each copy about the copy's own centre; offsets stay untransformed.
    void visUnderTransformedHolderMatchesStock() {
        QFETCH(QString, name);
        QJsonObject stock; QString error;
        QVERIFY2(visStockCase("c", name, stock, error), qPrintable(error));
        const QString observedLabel = name == "turf-rot" ? "thing" : "kid";
        const QString context = QString(FUGUE_TEST_DATA_DIR) + "/vis-world/stock.json: sets.c case " + name + "." + observedLabel + ": ";
        QVERIFY2(stock[observedLabel].isObject(), qPrintable(context + "missing shape observation"));
        const auto shape = stock[observedLabel].toObject();
        for (const char* field : {"cx", "cy"}) {
            const auto value = shape[field];
            QVERIFY2(value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= -32 && value.toDouble() <= 63,
                     qPrintable(context + field + " must be a finite coordinate in the retained 96-pixel crop"));
        }
        for (const char* field : {"w", "h"}) {
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{shape[field]}, 1, 1, 96),
                     qPrintable(context + field + " must be an integer size from 1 to 96"));
        }
        // The plain thing has no notch; all eleven kid observations retain one.
        if (observedLabel == "kid") {
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{shape["notch"]}, 1, 0, 359),
                     qPrintable(context + "notch must be an explicit integer angle from 0 to 359"));
        }
        if (name == "rot45" || name == "kid-ps" || name == "holder-ps")
            QVERIFY2(shape["blended"].isBool(), qPrintable(context + "blended must be an explicit boolean"));
        VisFixture f;
        const uint32_t floor = f.look(VisFixture::Floor, "", 2);
        for (int y = 1; y <= 20; ++y) for (int x = 1; x <= 20; ++x) f.floor(x, y, floor);
        const auto turn = [](double degrees) {
            const double r = degrees * M_PI / 180;
            return std::array<float, 6>{float(std::cos(r)), float(std::sin(r)), 0, float(-std::sin(r)), float(std::cos(r)), 0};
        };
        std::array<float, 6> m = turn(90);
        if (name == "scale2") m = {2, 0, 0, 0, 2, 0};
        else if (name == "rot45") m = turn(45);
        else if (name == "shift") m = {1, 0, 8, 0, 1, 4};
        else if (name == "kid-ps" || name == "holder-ps") m = turn(30);
        else if (name == "scale-half") m = {0.5f, 0, 0, 0, 0.5f, 0};
        std::string extra = R"(,"dir":2)";
        if (name == "rot90-px") extra += R"(,"pixelX":6)";
        if (name == "holder-ps") extra += R"(,"appearanceFlags":1073741824)";
        const uint32_t holder = f.atom(42, f.look(VisFixture::Holder, "a", 5, affinefixture::fields(m, extra)), 6, 6);
        uint32_t next = 60;
        auto kid = [&](int px, int py, const std::string& more = "") {
            return f.atom(next++, f.look(VisFixture::Kid, "b", 5,
                more.empty() ? QString(R"({"pixelX":%1,"pixelY":%2})").arg(px).arg(py).toStdString() :
                affinefixture::fields({1, 0, 0, 0, 1, 0}, QString(R"(,"pixelX":%1,"pixelY":%2)").arg(px).arg(py).toStdString() + more)));
        };
        std::vector<model::VisReference> members;
        if (name == "kid-own") {
            const uint32_t k = f.atom(next++, f.look(VisFixture::Kid, "b", 5, affinefixture::fields({0.5f, 0, 0, 0, 0.5f, 0}, R"(,"pixelX":8,"pixelY":4)")));
            members.push_back({2, k});
        } else if (name == "nested") {
            const uint32_t k = kid(8, 4); f.vis(k, f.w.atoms[k].renderIdentity, {{2, kid(4, -10)}}); members.push_back({2, k});
        } else if (name == "kid-reset") members.push_back({2, kid(8, 4, R"(,"appearanceFlags":16777216)")});
        else if (name == "kid-ps") members.push_back({2, kid(8, 4, R"(,"appearanceFlags":1073741824)")});
        else if (name == "turf-rot") {
            f.w.tiles[{1, 19, 0}] = f.look(VisFixture::Remote, "", 2);
            f.atom(next++, f.look(VisFixture::Thing, "", 3), 2, 20);
            members.push_back({1, f.flat(2, 20)});
        } else if (name == "scale-half") members.push_back({2, kid(16, 0)});
        else members.push_back({2, kid(8, 4)});
        f.vis(holder, f.w.atoms[holder].renderIdentity, members);
        shell::MapSprites sprites;
        const auto image = f.tile(6, 6, &sprites, 32);
        // Stock reads facts from the 96 px crop around the case tile; the composite covers it from (-32, -32).
        for (const auto& [label, colour] : std::vector<std::pair<QString, QString>>{{"kid", "kid.b"}, {"thing", "thing"}}) {
            if (!stock.contains(label)) continue;
            const auto s = stock[label].toObject();
            double sx = 0, sy = 0, wx = 0, wy = 0; int n = 0, white = 0, x0 = 999, x1 = -999, y0 = 999, y1 = -999;
            for (int y = -32; y < 64; ++y) for (int x = -32; x < 64; ++x) {
                if (VisFixture::name(image.pixelColor(x + 32, y + 32)) != colour) continue;
                sx += x; sy += y; ++n; x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
            QVERIFY2(n, qPrintable(label));
            const double cx = sx / n, cy = sy / n;
            QVERIFY2(std::abs(cx - s["cx"].toDouble()) <= 1 && std::abs(cy - s["cy"].toDouble()) <= 1,
                     qPrintable(QString("%1 centre %2,%3 vs stock %4,%5").arg(label).arg(cx).arg(cy).arg(s["cx"].toDouble()).arg(s["cy"].toDouble())));
            QVERIFY2(std::abs(x1 - x0 + 1 - s["w"].toInt()) <= 2 && std::abs(y1 - y0 + 1 - s["h"].toInt()) <= 2,
                     qPrintable(QString("%1 size %2x%3").arg(label).arg(x1 - x0 + 1).arg(y1 - y0 + 1)));
            if (!s.contains("notch")) continue;
            for (int y = -32; y < 64; ++y) for (int x = -32; x < 64; ++x) {
                if (VisFixture::name(image.pixelColor(x + 32, y + 32)) != "white") continue;
                bool touches = false;
                for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                    const QPoint q(x + dx + 32, y + dy + 32);
                    touches |= image.rect().contains(q) && VisFixture::name(image.pixelColor(q)) == colour;
                }
                if (touches) { wx += x; wy += y; ++white; }
            }
            QVERIFY(white);
            const int angle = int(std::lround(std::atan2(wy / white - cy, wx / white - cx) * 180 / M_PI / 15)) * 15;
            const int delta = ((angle - s["notch"].toInt()) % 360 + 540) % 360 - 180;
            QVERIFY2(std::abs(delta) <= 15, qPrintable(QString("%1 notch %2 vs stock %3").arg(label).arg(angle).arg(s["notch"].toInt())));
        }
        // At 30 and 45 degrees stock blends the kid's edges unless the kid itself has PIXEL_SCALE.
        if (name == "rot45" || name == "kid-ps" || name == "holder-ps") {
            const auto copy = std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.kind == shell::MapSprite::Vis; });
            QVERIFY(copy != sprites.end());
            QCOMPARE(copy->smooth, stock["kid"].toObject()["blended"].toBool());
        }
    }
    // Which parts an atom's filter reaches, against stock 516.1688 (tests/data/atom-filter-world): its own icon only;
    // KEEP_TOGETHER brings in its overlays and underlays; an overlay's own filter filters that overlay; an image on a
    // filtered target is outside the filter.
    void atomFiltersReachLikeStock_data() {
        QTest::addColumn<QString>("set"); QTest::addColumn<QString>("name");
        for (const auto& [set, name] : std::vector<std::pair<const char*, const char*>>{{"a", "overlay"}, {"b", "kt-overlay"},
                 {"b", "underlay"}, {"b", "kt-underlay"}, {"b", "overlay-own"}, {"b", "image-target"}})
            QTest::newRow(name) << QString(set) << QString(name);
    }
    void atomFiltersReachLikeStock() {
        QFETCH(QString, set); QFETCH(QString, name);
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/atom-filter-world/stock.json";
        QJsonObject stock; QString error;
        QVERIFY2(readStockCase(path, set, name, stock, error, set == "a" ? 14 : 12), qPrintable(error));
        const QString context = path + ": sets." + set + " case " + name + ": ";
        QVERIFY2(testinput::isIntegerTuple(QJsonArray{stock["fugueDiffOver8"]}, 1, 0, std::numeric_limits<int>::max()),
                 qPrintable(context + "fugueDiffOver8 must be an explicit nonnegative integer"));
        const QStringList fields = name == "overlay-own" ? QStringList{"ownRingBelowOverlay"} :
            QStringList{"ringAroundBox", name.endsWith("underlay") ? "ringBesideUnderlay" : "ringBelowOverlay"};
        for (const auto& field : fields)
            QVERIFY2(stock[field].isBool(), qPrintable(context + field + " must be an explicit boolean"));
        QCOMPARE(stock["fugueDiffOver8"].toInt(), 0); // the grabs agreed after the change
        // outline size 1, colour #ff0000, DM flags 0; record flag 1 is the captured stock envelope.
        const std::string outline = R"({"type":2,"paramMask":35,"flags":1,"params":"0000803fff0000ff0000"})";
        auto w = world();
        const bool together = name.startsWith("kt-"), underlay = name.endsWith("underlay");
        std::string root = R"({"filters":[)" + (name == "overlay-own" ? std::string() : outline) + "]";
        if (together) root += R"(,"appearanceFlags":67108864)";
        if (name != "image-target") root += underlay ? R"(,"underlays":[2])" : R"(,"overlays":[2])";
        setFields(w.appearances[1], root + "}");
        w.appearances[2] = w.appearances[1]; w.appearances[2].fields.iconState = "green";
        setFields(w.appearances[2], R"({"pixelX":14)" + (name == "overlay-own" ? R"(,"filters":[)" + outline + "]" : std::string()) + "}");
        if (name == "image-target") w.imageInstances[7] = {2, 42, 1, false, 20, 10};
        w.atoms[42].renderIdentity = 10;
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w, {}, 0, false);
        const auto sprites = scene.prepare(w, map, reader(), 1000, nullptr, true);
        QCOMPARE(sprites.size(), 2);
        const auto& base = *std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.image.pixelColor(16, 16) == QColor(Qt::red); });
        const auto& child = *std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.image.pixelColor(16, 16) == QColor(Qt::green); });
        auto filterOf = [](const shell::MapSprite& s) -> const shell::AppearanceGroup* {
            for (auto g = s.appearanceGroup; g; g = g->parent) if (g->effects) return g.get();
            return nullptr;
        };
        if (name == "overlay-own") {
            QVERIFY(!filterOf(base));
            QCOMPARE(filterOf(child) != nullptr, stock["ownRingBelowOverlay"].toBool());
            return;
        }
        QVERIFY(filterOf(base)); QVERIFY(stock["ringAroundBox"].toBool());
        const bool reached = filterOf(child) == filterOf(base);
        QCOMPARE(reached, stock[underlay ? "ringBesideUnderlay" : "ringBelowOverlay"].toBool());
        QCOMPARE(filterOf(base)->filterOnly, !together);
        // The plan runs the stock outline pass (operation 5) on the group's surface; the parts stay clickable, with or
        // without KEEP_TOGETHER (stock 516.1688, tests/data/click-world kt-outline).
        const auto plan = shell::CompositionPlan::build(sprites, map.source, {true});
        QVERIFY(plan->supported);
        bool outlined = false;
        for (const auto& pass : plan->passes) for (const auto& item : pass.items) outlined |= item.effect[0] == 5;
        QVERIFY(outlined);
        QVERIFY(!plan->bindings[size_t(&base - sprites.constData())].refused);
    }
    // Transforms with filters and groups (tests/data/transform-group-world): a filter runs on the part's untransformed
    // surface and the matrix then moves the filtered surface; overlays outside a filter or KEEP_TOGETHER surface take
    // the matrix about their own pivot; KEEP_APART outside a group changes nothing.
    void transformedGroupsLikeStock_data() {
        QTest::addColumn<QString>("set"); QTest::addColumn<QString>("name");
        for (const auto& [set, name] : std::vector<std::pair<const char*, const char*>>{{"a", "outline-scale"},
                 {"a", "outline-turn-overlay"}, {"a", "kt-outline-turn"}, {"b", "overlay-own-turn"}, {"b", "ka-turn"},
                 {"b", "kt-child-turn"}, {"b", "overlay-own-scale"}})
            QTest::newRow(name) << QString(set) << QString(name);
    }
    void transformedGroupsLikeStock() {
        QFETCH(QString, set); QFETCH(QString, name);
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/transform-group-world/stock.json";
        QJsonObject stock; QString error;
        QVERIFY2(readStockCase(path, set, name, stock, error, 12), qPrintable(error));
        const QString context = path + ": sets." + set + " case " + name + ": ";
        QVERIFY2(stock["drawn"].isBool(), qPrintable(context + "drawn must be an explicit boolean"));
        QVERIFY2(testinput::isIntegerTuple(QJsonArray{stock["fugueDiffOver8"]}, 1, 0, std::numeric_limits<int>::max()),
                 qPrintable(context + "fugueDiffOver8 must be an explicit nonnegative integer"));
        QVERIFY(stock["drawn"].toBool()); QCOMPARE(stock["fugueDiffOver8"].toInt(), 0);
        if (name == "outline-scale") { // filter first: the 1 px outline is 2 px wide where the matrix stretches it
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{stock["redColumnsLeft"], stock["redRowsAbove"]}, 2, 0, 96),
                     qPrintable(context + "outline widths must be explicit integer counts in the 96-pixel crop"));
            QCOMPARE(stock["redColumnsLeft"].toInt(), 2); QCOMPARE(stock["redRowsAbove"].toInt(), 1);
        }
        const std::string outline = R"({"type":2,"paramMask":35,"flags":1,"params":"0000803fff0000ff0000"})";
        const std::array<float,6> quarter{0,1,0,-1,0,0}, wide{2,0,0,0,1,0};
        auto w = world();
        const bool rootFilter = name.startsWith("outline") || name == "kt-outline-turn";
        const bool childFilter = name.startsWith("overlay-own");
        std::string root = rootFilter ? R"(,"filters":[)" + outline + "]" : std::string();
        if (name.startsWith("kt-outline")) root += R"(,"appearanceFlags":67108864)";
        if (name != "outline-scale") root += R"(,"overlays":[2])";
        setFields(w.appearances[1], name == "overlay-own-scale" ? "{" + root.substr(1) + "}"
            : affinefixture::fields(name == "outline-scale" ? wide : quarter, root));
        w.appearances[2] = w.appearances[1]; w.appearances[2].fields.iconState = "green";
        std::string child = R"(,"pixelX":14)";
        if (childFilter) child += R"(,"filters":[)" + outline + "]";
        if (name == "ka-turn") child += R"(,"appearanceFlags":134217728)";
        if (name == "kt-child-turn") child += R"(,"appearanceFlags":67108864,"overlays":[3])";
        setFields(w.appearances[2], name == "overlay-own-scale" ? affinefixture::fields(wide, child) : "{" + child.substr(1) + "}");
        w.appearances[3] = w.appearances[2]; setFields(w.appearances[3], R"({"pixelX":4,"pixelY":4})");
        w.atoms[42].renderIdentity = 10;
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w, {}, 0, false);
        const auto sprites = scene.prepare(w, map, reader(), 1000, nullptr, true);
        QCOMPARE(sprites.size(), name == "outline-scale" ? 1 : name == "kt-child-turn" ? 3 : 2); // nothing vanishes
        auto filterOf = [](const shell::MapSprite& s) -> const shell::AppearanceGroup* {
            for (auto g = s.appearanceGroup; g; g = g->parent) if (g->effects) return g.get();
            return nullptr;
        };
        auto moved = [](const shell::MapSprite& s) { // the matrix on the part or on a surface holding it
            bool any = s.affine.has_value();
            for (auto g = s.appearanceGroup; g; g = g->parent) any |= !g->transform.isIdentity();
            return any;
        };
        const auto& base = *std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.image.pixelColor(16, 16) == QColor(Qt::red); });
        QVERIFY(moved(base) || name == "overlay-own-scale");
        for (const auto& s : sprites) if (&s != &base) {
            QVERIFY(moved(s));
            QCOMPARE(filterOf(s) != nullptr, childFilter || name == "kt-outline-turn");
        }
        QCOMPARE(filterOf(base) != nullptr, rootFilter);
        if (!rootFilter && !childFilter) return;
        // The plan filters the untransformed surface: the outline pass (operation 5) feeds a quad that carries the matrix.
        const auto plan = shell::CompositionPlan::build(sprites, map.source, {true});
        QVERIFY(plan->supported);
        bool filterThenMatrix = false;
        auto check = [&](const shell::CompositionPlan::Item& item) {
            if (item.sprite->affine && item.input >= 0)
                for (const auto& inner : plan->passes[size_t(item.input)].items) filterThenMatrix |= inner.effect[0] == 5;
        };
        for (const auto& pass : plan->passes) for (const auto& item : pass.items) check(item);
        for (const auto& item : plan->final) check(item);
        QVERIFY(filterThenMatrix);
    }
    // Filters reach images, animating atoms and flicks (tests/data/filter-reach-world): stock draws each
    // filtered; an image's own filter runs before its target's matrix.
    // Wave, ripple and bloom records (tests/data/filter-types-world, stock 516.1688): every captured record decodes
    // and the atom's filter surface runs stock's operation for it. Pixels: map_test::filterTypesPixelsLikeStock.
    void filterTypesDecodeLikeStock() {
        QJsonArray rows;
        QString error;
        QVERIFY2(test::filtertypes::recordRows(rows, error), qPrintable(error));
        QCOMPARE(rows.size(), 21);
        QMap<QString, QJsonObject> byName;
        int checked = 0;
        for (const auto& value : rows) {
            const auto row = value.toObject();
            const auto name = row["case"].toString();
            byName.insert(name, row);
            QCOMPARE(row["fugueDiffOver8"].toInt(), 0);
            const auto effects = test::filtertypes::effects(row);
            QVERIFY2(effects.supported && !effects.ignoredFilters, qPrintable(name));
            QCOMPARE(effects.filters.size(), size_t(row["records"].toArray().size()));
            const auto kind = effects.filters[0].kind;
            QCOMPARE(int(kind), row["records"].toArray()[0].toObject()["type"].toInt());
            const shell::MapSprites sprites{test::filtertypes::sprite(row, {64, 64})};
            const auto plan = shell::CompositionPlan::build(sprites, {0, 0, 160, 160}, {true});
            QVERIFY2(plan->supported, qPrintable(name));
            const float op = kind == shell::PlaneFilter::Wave ? 15 : kind == shell::PlaneFilter::Ripple ? 16 : 17;
            int passes = 0;
            for (const auto& pass : plan->passes) for (const auto& item : pass.items)
                if (item.effect[0] == op) {
                    ++passes; if (op != 17) QVERIFY(item.params);
                    // The bar sign's seven waves (record bytes 1..7) share one pass, like stock's ten-wave pass.
                    if (name == "sign-static") QCOMPARE((*item.params)[0], 7.f);
                }
            // wave-split: a WAVE_BOUNDED record starts its own pass.
            QVERIFY2(passes == (name == "wave-split" ? 2 : 1), qPrintable(name + ": " + QString::number(passes)));
            ++checked;
        }
        QCOMPARE(checked, 21);
        // Parameters as stock reads them: wave-diag x=8 y=8 size=1.5 offset=0.25; ripple-xy y=-2 (screen +2).
        const auto wave = test::filtertypes::effects(byName.value("wave-diag")).filters.at(0);
        QCOMPARE(wave.x, 8.0); QCOMPARE(wave.y, -8.0); QCOMPARE(wave.size, 1.5); QCOMPARE(wave.offset, .25);
        const auto ripple = test::filtertypes::effects(byName.value("ripple-xy")).filters.at(0);
        QCOMPARE(ripple.y, 2.0); QCOMPARE(ripple.radius, 12.0); QCOMPARE(ripple.repeat, 1.0); QCOMPARE(ripple.falloff, .5);
        const auto bloom = test::filtertypes::effects(byName.value("bloom-soft")).filters.at(0);
        QCOMPARE(bloom.size, 2.0); QCOMPARE(bloom.offset, 0.0); QCOMPARE(bloom.color[3], 128.f/255);
    }
    // vg's bar sign animates each wave's offset (filter-types-world set c, stock 516.1688: the phase moves
    // continuously at the step's rate). Halfway through a 0.2 -> -0.8 step the surface's wave has offset -0.3.
    void filterParametersTweenLikeStock() {
        auto w = world(); w.atoms[42].renderIdentity = 10;
        auto wave = [](const char* params) {
            return std::string(R"({"filters":[{"type":5,"paramMask":61,"flags":1,"params":")") + params + "\"}]}";
        };
        w.appearances[2] = w.appearances[1];
        setFields(w.appearances[1], wave("0000c03f00004041000010c1cdcc4c3e0000"));
        setFields(w.appearances[2], wave("0000c03f00004041000010c1cdcc4cbf0000"));
        w.atoms[42].appid = 2; w.atoms[42].chain = {0, 100'000'000, {{1, 2, std::bit_cast<uint32_t>(40.f), 0, 0, 1, 0}}};
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w, {}, 0, false);
        const auto sprites = scene.prepare(w, map, reader(), 2'000'000'000, nullptr, true);
        const shell::PlaneEffects* effects = nullptr;
        for (const auto& s : sprites) for (auto g = s.appearanceGroup; g; g = g->parent) if (g->effects) effects = g->effects.get();
        QVERIFY(effects); QCOMPARE(effects->filters.size(), size_t(1));
        QVERIFY(std::abs(effects->filters[0].offset - -.3) < 1e-6);
        QCOMPARE(effects->filters[0].x, 12.0); QCOMPARE(effects->filters[0].size, 1.5);
    }
    // Clicks on group parts and image trees (tests/data/click-world, stock 516.1688): KEEP_TOGETHER root and overlay ink
    // hit as the root; a turned group or target maps the pointer back through its matrix, reporting the root's icon
    // coordinates. (Turf copies: viscontents_test::turfMembersClickLikeStock.)
    void clicksLikeStock_data() {
        QTest::addColumn<QString>("label");
        for (const char* label : {"kt-root", "kt-overlay", "ktturn-root", "ktturn-overlay", "fturn-box", "image-overlay"})
            QTest::newRow(label) << QString(label);
    }
    void clicksLikeStock() {
        QFETCH(QString, label);
        QJsonObject row;
        {
            QFile file(QString(FUGUE_TEST_DATA_DIR) + "/click-world/stock.json"); QVERIFY(file.open(QIODevice::ReadOnly));
            for (const auto& c : QJsonDocument::fromJson(file.readAll()).object()["clicks"].toArray())
                if (c.toObject()["label"].toString() == label) row = c.toObject();
        }
        QVERIFY(!row.isEmpty()); QVERIFY(row["fugueMatches"].toBool());
        const auto stock = row["stock"].toObject();
        const std::array<float,6> quarter{0,1,0,-1,0,0};
        const float c = 0.70710677f;
        const std::array<float,6> eighth{c,c,0,-c,c,0};
        const std::string kt = R"("appearanceFlags":67108864,"overlays":[2])";
        const std::string outline = R"("filters":[{"type":2,"paramMask":35,"flags":1,"params":"00000040ff0000ff0000"}])";
        auto w = world(); w.atoms[42].renderIdentity = 10;
        w.appearances[2] = w.appearances[1]; w.appearances[2].fields.iconState = "green";
        setFields(w.appearances[2], R"({"pixelX":12})");
        const auto kase = row["case"].toString();
        if (kase == "kt-root") setFields(w.appearances[1], "{" + kt + "}");
        else if (kase == "kt-turned") setFields(w.appearances[1], affinefixture::fields(quarter, "," + kt));
        else if (kase == "filter-turned") setFields(w.appearances[1], affinefixture::fields(eighth, "," + outline));
        else {
            // The target turned 90; its image carries an overlay 14 px right.
            setFields(w.appearances[1], affinefixture::fields(quarter));
            setFields(w.appearances[2], R"({"overlays":[3]})"); w.appearances[2].fields.iconState = "hollow";
            w.appearances[3] = w.appearances[2]; w.appearances[3].fields.iconState = "green";
            setFields(w.appearances[3], R"({"pixelX":14})");
            w.imageInstances[7] = {2, 42, 1, false, 20, 10};
        }
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w, {}, 0, false);
        const auto sprites = scene.prepare(w, map, reader(), 1000, nullptr, true);
        QVERIFY(!sprites.isEmpty());
        // The atom's tile: its untransformed icon box (the root's icon bottom less one tile).
        const auto& root = *std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.kind != shell::MapSprite::Turf; });
        const QPoint tile = root.iconBottom - QPoint(0, 32);
        const QPoint pixel = tile + QPoint(row["point"].toArray()[0].toInt(), row["point"].toArray()[1].toInt());
        const auto hit = shell::mapPointerAt(pixel - map.source.topLeft(), map.source.size(), map.source.size() / 32,
                                             map.source, map.geometry, "map", true, sprites);
        QVERIFY(hit);
        QCOMPARE(hit->click.over.kind, proto::enc::RefObj); QCOMPARE(hit->click.over.ref, 42u);
        QCOMPARE(bool(hit->imageIdentity), kase == "image-turned-target"); // the image's overlay, not the target
        QCOMPARE(hit->click.iconX, stock["x"].toInt()); QCOMPARE(hit->click.iconY, stock["y"].toInt());
    }
    void filterReachLikeStock_data() {
        QTest::addColumn<QString>("set"); QTest::addColumn<QString>("name");
        // maptext needs a GUI font database: maptext_test::filteredTextJoinsIconSurface covers it.
        for (const auto& [set, name] : std::vector<std::pair<const char*, const char*>>{{"a", "image"}, {"a", "image-turn"},
                 {"b", "anim-turn"}, {"b", "flick"}})
            QTest::newRow(name) << QString(set) << QString(name);
    }
    void filterReachLikeStock() {
        QFETCH(QString, set); QFETCH(QString, name);
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/filter-reach-world/stock.json";
        QJsonObject stock; QString error;
        QVERIFY2(readStockCase(path, set, name, stock, error, set == "a" ? 15 : 8), qPrintable(error));
        QVERIFY2(testinput::isIntegerTuple(QJsonArray{stock["red"], stock["magenta"]}, 2, 0, std::numeric_limits<int>::max()),
                 qPrintable(path + ": sets." + set + " case " + name + ": red and magenta must be explicit nonnegative integer counts"));
        QVERIFY(stock["red"].toInt() > 0 || stock["magenta"].toInt() > 0); // stock draws the filter's ink
        const std::string outline = R"({"type":2,"paramMask":35,"flags":1,"params":"0000803fff0000ff0000"})";
        const std::array<float,6> quarter{0,1,0,-1,0,0};
        auto w = world(); w.atoms[42].renderIdentity = 10;
        const std::string filters = R"("filters":[)" + outline + "]";
        if (name.startsWith("image")) {
            setFields(w.appearances[1], name == "image-turn" ? affinefixture::fields(quarter) : "{}");
            w.appearances[2] = w.appearances[1]; w.appearances[2].fields.iconState = "green";
            setFields(w.appearances[2], "{" + filters + "}");
            w.imageInstances[7] = {2, 42, 1, false, 20, 10};
        } else if (name == "anim-turn") {
            w.appearances[2] = w.appearances[1];
            setFields(w.appearances[1], "{" + filters + "}");
            setFields(w.appearances[2], affinefixture::fields(quarter, "," + filters));
            w.atoms[42].appid = 2; w.atoms[42].chain = {0, 100'000'000, {{1, 2, std::bit_cast<uint32_t>(200.f), 0, 0, 1, 0}}};
        } else {
            setFields(w.appearances[1], "{" + filters + "}");
            w.flicks[42] = {"green", 0, true, 1000, 99, true};
        }
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w, {}, 0, false);
        const auto sprites = scene.prepare(w, map, reader(), name == "anim-turn" ? 10'000'000'000 : 1000, nullptr, true);
        auto filterOf = [](const shell::MapSprite& s) -> const shell::AppearanceGroup* {
            for (auto g = s.appearanceGroup; g; g = g->parent) if (g->effects) return g.get();
            return nullptr;
        };
        const shell::AppearanceGroup* group = nullptr;
        for (const auto& s : sprites) {
            const bool own = name.startsWith("image") ? s.kind == shell::MapSprite::Image : true;
            if (!own) { QVERIFY(!filterOf(s)); continue; } // the target stays unfiltered
            QVERIFY2(filterOf(s), qPrintable(QString("unfiltered part of %1").arg(name)));
            group = filterOf(s);
            if (name == "image-turn" || name == "anim-turn") { QVERIFY(!s.affine); QVERIFY(!group->transform.isIdentity()); }
        }
        QVERIFY(group);
        const auto plan = shell::CompositionPlan::build(sprites, map.source, {true});
        QVERIFY(plan->supported);
    }
    // Images with their own matrix, and image trees on transformed targets (tests/data/image-tree-transform-world):
    // the image's own matrix applies first and the target's after it; RESET_TRANSFORM drops the target's; overlays
    // turn about their own pivot.
    // tests/data/stock-parity-world images-groups: an image on a KEEP_TOGETHER target is composed in the target's
    // surface, where the target's alpha applies once; KEEP_APART keeps it outside with the target's alpha; a float
    // image is at 5. Stock 516.1689 draws the yellow image box 128,118,28 under the surface's alpha (img-kt) and
    // 134,164,81 blended by itself over the translucent target (img-kt-apart); the replay test checks those pixels.
    void imagesJoinKeepTogetherTargetsLikeStock() {
        auto w = world(); w.atoms[42].renderIdentity = 10;
        setFields(w.appearances[1], R"({"appearanceFlags":67108864,"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":128})");
        w.appearances[2] = w.appearances[1]; w.appearances[2].fields.iconState = "green";
        w.appearances[2].fields.layerBits = std::bit_cast<uint32_t>(-1.f);
        w.imageInstances[7] = {2, 42, 1, false, 20, 10};
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w, {}, 0, false);
        auto picture = [&](const char* fields) {
            setFields(w.appearances[2], fields);
            const auto sprites = scene.prepare(w, map, reader(), 1, nullptr, true);
            std::pair<shell::MapSprite, shell::MapSprite> out;
            for (const auto& s : sprites) (s.kind == shell::MapSprite::Image ? out.second : out.first) = s;
            return out;
        };
        const auto [target, image] = picture("{}");
        QVERIFY(target.appearanceGroup); QCOMPARE(image.appearanceGroup, target.appearanceGroup);
        QCOMPARE(image.groupOccurrence, target.groupOccurrence); QCOMPARE(image.tint[3], 1.f);
        QCOMPARE(image.groupLayer, std::nextafter(5.f, 0.f)); QCOMPARE(image.layer, target.layer);
        const auto [group, outside] = picture(R"({"appearanceFlags":134217728})");
        QVERIFY(group.appearanceGroup); QVERIFY(!outside.appearanceGroup);
        QCOMPARE(outside.tint[3], 128.f / 255); QCOMPARE(outside.layer, std::nextafter(5.f, 0.f));
    }
    // A one-pixel particle at (0.5, 0.5) of a 96 px set lands at the icon's centre column and one row above its centre
    // row: stock 516.1689 and Fugue both draw such a dot on tile 12,4 of a 15x15 view at 368,367.
    void particleDotsLandWhereStockDrawsThem() {
        auto w = world();
        auto& set = w.particles[42];
        set.width = set.height = 96; set.count = 1; set.spawning = 1;
        set.gen[model::PLifespan].type = 0x80; set.gen[model::PLifespan].a = {100000};
        set.gen[model::PPosition].type = 0x81; set.gen[model::PPosition].a = {0.5f, 0.5f, 0};
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w, {}, 0, false);
        shell::MapSprites sprites;
        for (int64_t now : {1'000'000'000ll, 2'000'000'000ll, 3'000'000'000ll}) sprites = scene.prepare(w, map, reader(), now, nullptr, true);
        QVERIFY(!sprites.isEmpty());
        const auto& holder = sprites.front();
        const auto dot = std::find_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.image.size() == QSize(1, 1); });
        QVERIFY(dot != sprites.end());
        QCOMPARE(dot->position, holder.position + QPoint(16, 15));
        QCOMPARE(dot->plane, holder.plane); QCOMPARE(dot->layer, holder.layer); QVERIFY(dot->mouseTransparent);
    }
    void imageTreesTransformLikeStock_data() {
        QTest::addColumn<QString>("name");
        for (const char* name : {"own-turn", "own-turn-target-scale", "own-scale-target-turn", "own-reset", "overlay-turn"})
            QTest::newRow(name) << QString(name);
    }
    void imageTreesTransformLikeStock() {
        QFETCH(QString, name);
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/image-tree-transform-world/stock.json";
        QJsonObject stock; QString error;
        QVERIFY2(readStockCase(path, "a", name, stock, error, 16), qPrintable(error));
        const QString context = path + ": sets.a case " + name + ": ";
        QVERIFY2(testinput::isIntegerTuple(QJsonArray{stock["fugueDiffOver8"]}, 1, 0, std::numeric_limits<int>::max()),
                 qPrintable(context + "fugueDiffOver8 must be an explicit nonnegative integer"));
        QCOMPARE(stock["fugueDiffOver8"].toInt(), 0);
        const auto yellow = stock["yellow"].toArray(); QCOMPARE(yellow.size(), 4);
        QVERIFY2(testinput::isIntegerTuple(QJsonArray{yellow[2], yellow[3]}, 2, 1, 96),
                 qPrintable(context + "yellow width and height must be positive integer sizes in the 96-pixel crop"));
        const std::array<float,6> quarter{0,1,0,-1,0,0}, wide{2,0,0,0,1,0};
        auto w = world(); w.atoms[42].renderIdentity = 10;
        setFields(w.appearances[1], name == "own-turn" ? "{}" : affinefixture::fields(name.endsWith("turn") ? quarter : wide));
        w.appearances[2] = w.appearances[1]; w.appearances[2].fields.iconState = "green";
        if (name == "overlay-turn") setFields(w.appearances[2], R"({"overlays":[3]})");
        else setFields(w.appearances[2], affinefixture::fields(name == "own-scale-target-turn" ? wide : quarter,
                                                                 name == "own-reset" ? R"(,"appearanceFlags":16777216)" : ""));
        w.appearances[3] = w.appearances[2]; setFields(w.appearances[3], R"({"pixelX":14})");
        w.imageInstances[7] = {2, 42, 1, false, 20, 10};
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w, {}, 0, false);
        const auto sprites = scene.prepare(w, map, reader(), 1, nullptr, true);
        QCOMPARE(sprites.size(), name == "overlay-turn" ? 3 : 2); // nothing refused
        std::vector<const shell::MapSprite*> images;
        for (const auto& s : sprites) if (s.kind == shell::MapSprite::Image) images.push_back(&s);
        QVERIFY(!images.empty());
        for (const auto* image : images) QVERIFY(image->affine);
        // The 32 px test icon's drawn shape has the same orientation as stock's 16 px box: wide, tall or square.
        const QRect r = images.front()->rect();
        const int sw = yellow[2].toInt(), sh = yellow[3].toInt();
        QCOMPARE(r.width() > r.height() + 4, sw > sh + 4); QCOMPARE(r.height() > r.width() + 4, sh > sw + 4);
        if (name == "own-reset") QCOMPARE(r.size(), QSize(32, 32)); // only its own quarter turn
    }
    // ANIMATION_SLICE (tests/data/anim-flags-world): a leading slice step plays a window of the sequence behind it over
    // and over, and holds at the window's end once its loops run out.
    void animationSliceLikeStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/anim-flags-world/stock.json";
        QJsonDocument document;QString error;
        QVERIFY2(testinput::readRecordedJson(path, document, error), qPrintable(error));
        const auto doc = document.object();
        const auto cases = doc["cases"].toObject(), wire = doc["wire"].toObject();
        for (const char* name : {"slice", "slice-once"}) {
            const auto lead = wire[name].toObject()["leadingStep"].toObject();
            const auto context = path + ": " + name;
            QCOMPARE(lead.keys(), (QStringList{"ext", "flags", "from", "loop", "time", "to"}));
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{lead["from"], lead["to"]}, 2, 65535, 65535), qPrintable(context + ": invalid slice endpoints"));
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{lead["flags"]}, 1, 8, 8), qPrintable(context + ": missing slice flag"));
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{lead["loop"]}, 1, -1, 1) && lead["loop"].toInt() != 0, qPrintable(context + ": invalid loop input"));
            QVERIFY2(testinput::isNumberTuple(QJsonArray{lead["time"], lead["ext"]}, 2), qPrintable(context + ": invalid slice timing"));
            QVERIFY(lead["time"].toDouble() > 0 && lead["ext"].toDouble() >= 0 && lead["time"].toDouble() + lead["ext"].toDouble() <= 40);
        }
        const auto window = cases["slice"].toObject()["window"].toArray();
        const auto runs = cases["slice"].toObject()["pixelXSeen"].toArray();
        const auto held = cases["slice-once"].toObject()["heldPixelX"].toArray();
        QVERIFY2(testinput::isIntegerTuple(window, 2, 0, 32), qPrintable(path + ": invalid slice window"));
        QVERIFY(window[0].toInt() <= window[1].toInt());
        QCOMPARE(runs.size(), 2);
        for (const auto& run : runs)
            QVERIFY2(testinput::isIntegerTuple(run, 2, 0, 32), qPrintable(path + ": expected two pixel observations in each slice run"));
        QVERIFY2(testinput::isIntegerTuple(held, 2, 0, 32), qPrintable(path + ": invalid held pixel observations"));
        QVERIFY(held[0].toInt() <= held[1].toInt());
        constexpr int64_t base = 50'000'000'000;
        auto chainFor = [&](const char* name) {
            const auto lead = wire[name].toObject()["leadingStep"].toObject();
            model::Atom::Chain ch; ch.tickNs = 100'000'000; ch.startNs = base; // the slice call, on the client's clock
            model::Atom::AnimStep slice{0xffffu, 0xffffu, std::bit_cast<uint32_t>(float(lead["time"].toDouble())), 0,
                                        uint32_t(lead["flags"].toInt()), lead["loop"].toInt(), base};
            slice.extBits = std::bit_cast<uint32_t>(float(lead["ext"].toDouble()));
            ch.steps = {slice, {10, 11, std::bit_cast<uint32_t>(20.f), 0, 0, -1, base - 1'000'000'000}, {11, 10, std::bit_cast<uint32_t>(20.f), 0, 0, 1, 0}};
            return ch;
        };
        auto value = [&](const model::Atom::Chain& ch, int64_t now, double& out) {
            out = 0; uint32_t last = 0;
            auto get = [](uint32_t id) { return id == 11 ? 32.0 : 0.0; };
            auto lerp = [](double a, double b, double, double f) { return a + (b - a) * f; };
            auto add = [](double& o, double base, double a, double b, double, double f) { o += a + (b - a) * f - base; };
            uint64_t work = 1'000'000;
            return shell::animation::admit(ch, work) && shell::animation::composeChain(ch, base + now, get, lerp, add, out, last);
        };
        const auto loop = chainFor("slice");
        for (const auto& run : runs)
            for (const auto& seen : run.toArray()) QVERIFY(seen.toInt() >= window[0].toInt() && seen.toInt() <= window[1].toInt());
        for (int64_t ms = 0; ms < 5000; ms += 125) {
            double v = 0; QVERIFY(value(loop, ms * 1'000'000, v));
            QVERIFY2(v >= window[0].toDouble() && v <= window[1].toDouble(), qPrintable(QString("%1 at %2 ms").arg(v).arg(ms)));
        }
        double v = 0;
        QVERIFY(value(loop, 250'000'000, v)); QCOMPARE(v, 20.0);
        QVERIFY(value(loop, 1'750'000'000, v)); QCOMPARE(v, 28.0); // 8 px per half window
        const auto once = chainFor("slice-once");
        QVERIFY(value(once, 1'000'000'000, v)); QCOMPARE(v, 16.0);
        for (int64_t s : {2, 3, 10}) {
            QVERIFY(value(once, s * 1'000'000'000, v));
            QVERIFY(v >= held[0].toDouble() && v <= held[1].toDouble()); // held at the window's end, not the settled 0
        }
    }
    // A 64 px icon world turns appearance trees and images on turned targets like a 32 px one
    // (tests/data/wide-icon-transform-world).
    void wideIconTransformTreesLikeStock() {
        QFile file(QString(FUGUE_TEST_DATA_DIR) + "/wide-icon-transform-world/stock.json"); QVERIFY(file.open(QIODevice::ReadOnly));
        std::map<QString, QJsonObject> stock;
        for (const auto& c : QJsonDocument::fromJson(file.readAll()).object()["sets"].toObject()["a"].toArray())
            stock[c.toObject()["case"].toString()] = c.toObject();
        QVERIFY(stock["turn-overlay"]["overlayDrawn"].toBool()); QVERIFY(stock["image-turn"]["imageDrawn"].toBool());
        const std::array<float,6> quarter{0,1,0,-1,0,0};
        for (const char* name : {"turn-overlay", "image-turn"}) {
            auto w = world(); w.stepSizeBits = std::bit_cast<uint32_t>(64.f); w.atoms[42].renderIdentity = 10;
            w.appearances[2] = w.appearances[1]; w.appearances[2].fields.iconState = "green";
            setFields(w.appearances[2], R"({"pixelX":28})");
            if (QString(name) == "turn-overlay") setFields(w.appearances[1], affinefixture::fields(quarter, R"(,"overlays":[2])"));
            else { setFields(w.appearances[1], affinefixture::fields(quarter)); w.imageInstances[7] = {2, 42, 1, false, 20, 10}; }
            shell::TurfMap terrain; shell::EntityMap scene;
            const auto map = terrain.prepare(w, {}, 0, false);
            const auto sprites = scene.prepare(w, map, reader(), 1, nullptr, true);
            QCOMPARE(sprites.size(), 2); // nothing refused for the icon size
            for (const auto& s : sprites) QVERIFY(s.affine);
        }
    }
    // Screen objects animate on the world clock (tests/data/hud-anim-world): a 20 s alpha tween and a 20 s quarter turn
    // sampled at every stock clock reading, against the stock fade brightness and the turning arm's centroid.
    void hudAnimationMatchesStock() {
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/hud-anim-world/stock.json";
        QJsonDocument document;QString error;
        QVERIFY2(testinput::readRecordedJson(path,document,error),qPrintable(error));
        const auto frames=document.object()["frames"].toArray();QCOMPARE(frames.size(),14);
        for (const auto& value:frames) {
            const auto frame=value.toObject();
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{frame["clock"]},1,0,320),qPrintable(path+": invalid clock reading"));
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{frame["fade"]},1,0,255),qPrintable(path+": invalid fade observation"));
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{frame["slideX"]},1,0,0),qPrintable(path+": missing stationary slide observation"));
            QVERIFY2(testinput::isNumberTuple(frame["turnArm"],2),qPrintable(path+": invalid arm centroid"));
        }
        auto hudWorld=[&](const std::string& from, const std::string& to) {
            auto w=world(); w.atoms[42].renderIdentity=10; w.atoms[42].hasPos=false;
            w.appearances[2]=w.appearances[1]; setFields(w.appearances[1], from); setFields(w.appearances[2], to);
            w.hudOrder={42}; w.hudAppid[42]=2; w.hudIdentity[42]={2,99,10};
            w.atoms[42].appid=2; w.atoms[42].chain={0,100'000'000,{{1,2,std::bit_cast<uint32_t>(200.f),0,0,1,0}}};
            return w;
        };
        auto fade=hudWorld(R"({"screenLoc":"1,1"})",R"({"screenLoc":"1,1","hasColor":true,"colorA":0})");
        const std::array<float,6> quarter{0,1,0,-1,0,0}; // Turn(90)
        auto turn=hudWorld(R"({"screenLoc":"1,1"})",affinefixture::fields(quarter,R"(,"screenLoc":"1,1")"));
        shell::TurfMap terrain; const auto map=terrain.prepare(fade,{},0,false);
        shell::EntityMap fading, turning;
        auto rest=turn; rest.atoms[42].chain={}; setFields(rest.appearances[2], R"({"screenLoc":"1,1"})");
        shell::EntityMap still; const auto resting=still.prepare(rest,map,reader(),0,nullptr,true);
        QCOMPARE(resting.size(),1);const auto origin=resting[0].position;
        for (const auto& value:frames) {
            const auto f=value.toObject();
            const double t=(f["clock"].toDouble()+.5)/320; const int64_t now=int64_t(t*20e9);
            const auto a=fading.prepare(fade,map,reader(),now,nullptr,true);
            QCOMPARE(a.size(),1); // stock draws it mid-animation
            QVERIFY2(std::abs(a[0].tint[3]*255-f["fade"].toDouble())<=3,
                     qPrintable(QString("alpha %1 vs stock %2 at t=%3").arg(a[0].tint[3]*255).arg(f["fade"].toDouble()).arg(t)));
            const auto r=turning.prepare(turn,map,reader(),now,nullptr,true);
            QCOMPARE(r.size(),1); QVERIFY(r[0].affine);
            // The red arm's centroid in icon pixels is (24, 16); stock reads it in the HUD tile.
            const QPointF arm=r[0].affine->forward.map(QPointF(24,16))+QPointF(r[0].position-origin);
            const auto s=f["turnArm"].toArray();
            QVERIFY2(std::abs(arm.x()-s[0].toDouble())<=1 && std::abs(arm.y()-s[1].toDouble())<=1,
                     qPrintable(QString("arm %1,%2 vs stock %3,%4 at t=%5").arg(arm.x()).arg(arm.y()).arg(s[0].toDouble()).arg(s[1].toDouble()).arg(t)));
            QCOMPARE(f["slideX"].toInt(),0); // and the sliding marker never left its screen_loc
        }
    }
    // Atoms the client knows on another level or far off the view must not use up the frame's candidate budget before
    // the visible turf vis_contents (vg's ZAS gas overlays) and the HUD are placed.
    void offViewAtomsDoNotStarveVisAndHud() {
        auto w = world(); w.atoms[42].renderIdentity = 10;
        for (uint32_t id = 1000; id < 71000; ++id) {
            auto& a = w.atoms[id]; a.hasPos = true; a.appid = 1; a.renderIdentity = id;
            if (id % 2) { a.x = 6; a.y = 6; a.z = 1; } else { a.x = 1 + id % 20; a.y = 20; a.z = 0; w.bounds.maxY = 20; }
        }
        // Even ids stand on the top row, far north of the 3x3 view around (6,6) once the map is tall enough.
        w.bounds.maxX = w.bounds.maxY = 400;
        for (uint32_t id = 1000; id < 71000; id += 2) w.atoms[id].y = 300;
        auto& gas = w.atoms[43]; gas.appid = 1; gas.renderIdentity = 11; // a shared nullspace obj
        w.tiles[{5, 5, 0}] = 1;
        QVERIFY(w.vis.replace(model::VisContents::turf(5, 5, 0), 0, {{2, 43}}, [](uint32_t) { return uint64_t(11); }));
        w.appearances[3] = w.appearances[1]; setFields(w.appearances[3], R"({"screenLoc":"1,1"})");
        w.hudOrder = {44}; w.hudAppid[44] = 3; w.hudIdentity[44] = {2, 98, 12};
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w, {}, 0, false);
        const auto sprites = scene.prepare(w, map, reader(), 1000, nullptr, true);
        auto count = [&](shell::MapSprite::Kind kind) {
            return std::count_if(sprites.begin(), sprites.end(), [&](const auto& s) { return s.kind == kind; });
        };
        QCOMPARE(count(shell::MapSprite::Vis), 1);
        QCOMPARE(count(shell::MapSprite::Screen), 1);
        QCOMPARE(count(shell::MapSprite::Entity), 1); // atom 42; none of the far or other-level ones
    }
    // Colour tweens against stock (tests/data/color-tween-world): matrices blend element by element with the eased
    // factor, a plain colour counting as its diagonal matrix, and a matrix set over a plain colour drops its stale RGB.
    void colorTweensMatchStock() {
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/color-tween-world/stock.json";
        QJsonDocument document;QString error;
        QVERIFY2(testinput::readRecordedJson(path,document,error),qPrintable(error));
        const auto doc=document.object();
        const auto names=doc["cases"].toArray(); const auto frames=doc["frames"].toArray();
        const QStringList expected{"id-to-m1","m1-to-m2","plain-to-m1","m1-to-plain","id-to-m1-sine","static-plain-then-m1"};
        QCOMPARE(names.size(),expected.size());QCOMPARE(frames.size(),10);
        for (int c=0;c<names.size();++c) QCOMPARE(names[c].toString(),expected[c]);
        for (const char* key:{"M1","M2"})
            QVERIFY2(testinput::isNumberTuple(doc[key],20),qPrintable(path+": invalid colour matrix "+key));
        QCOMPARE(doc["plain"].toString(),QString("#ff8040"));
        for (const auto& value:frames) {
            const auto frame=value.toObject();
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{frame["clock"]},1,0,320),qPrintable(path+": invalid clock reading"));
            const auto rgb=frame["rgb"].toArray();QCOMPARE(rgb.size(),names.size());
            for (int c=0;c<rgb.size();++c)
                QVERIFY2(testinput::isIntegerTuple(rgb[c],3,0,255),qPrintable(path+": invalid RGB observation for "+expected[c]));
        }
        auto matrixVar=[&](const char* key) {
            QByteArray bytes;
            for (const auto& v:doc[key].toArray()) { const float f=float(v.toDouble()); bytes.append(reinterpret_cast<const char*>(&f),4); }
            return QString(R"("vars":[{"bit":1024,"data":"%1"}])").arg(QString::fromLatin1(bytes.toHex())).toStdString();
        };
        const std::string plain=R"("hasColor":true,"colorR":255,"colorG":128,"colorB":64,"colorA":255)";
        const std::map<QString,std::string> looks{{"id","{}"},{"m1","{"+matrixVar("M1")+"}"},{"m2","{"+matrixVar("M2")+"}"},
            {"plain","{"+plain+"}"},{"m1-over-plain","{"+plain+","+matrixVar("M1")+"}"}};
        struct Case { const char* name; const char* from; const char* to; uint8_t easing; };
        for (const Case c:{Case{"id-to-m1","id","m1",0},Case{"m1-to-m2","m1","m2",0},Case{"plain-to-m1","plain","m1-over-plain",0},
                           Case{"m1-to-plain","m1","plain",0},Case{"id-to-m1-sine","id","m1",1},
                           Case{"static-plain-then-m1","m1-over-plain","m1-over-plain",0}}) {
            int column=-1; for (int i=0;i<names.size();++i) if (names[i].toString()==c.name) column=i;
            QVERIFY(column>=0);
            auto w=world(); w.appearances[2]=w.appearances[1];
            setFields(w.appearances[1], looks.at(c.from)); setFields(w.appearances[2], looks.at(c.to));
            auto& atom=w.atoms[42]; atom.appid=2;
            if (QString(c.from)!=c.to) atom.chain={0,100'000'000,{{1,2,std::bit_cast<uint32_t>(200.f),c.easing,0,1,0}}};
            shell::TurfMap terrain; shell::EntityMap scene; const auto map=terrain.prepare(w,{},0,false);
            for (const auto& value:frames) {
                const auto f=value.toObject(); const double t=(f["clock"].toDouble()+.5)/320;
                const auto sprites=scene.prepare(w,map,reader(),int64_t(t*20e9));
                QCOMPARE(sprites.size(),1);
                const auto& s=sprites[0]; const auto stock=f["rgb"].toArray()[column].toArray();
                for (int ch=0;ch<3;++ch) {
                    // The grey icon (100,100,100,255) through the sprite's matrix, then its tint.
                    double v=s.color[16+ch];
                    for (int k=0;k<4;++k) v+=double(s.color[4*k+ch])*(k<3 ? 100.0/255 : 1.0);
                    const double fugue=std::clamp(v*s.tint[ch],0.0,1.0)*255;
                    QVERIFY2(std::abs(fugue-stock[ch].toDouble())<=2,
                             qPrintable(QString("%1 channel %2 at t=%3: fugue %4 stock %5").arg(c.name).arg(ch).arg(t).arg(fugue).arg(stock[ch].toInt())));
                }
            }
        }
    }
    // A 16 px image on a turned target is drawn turned about its own centre, and each icon samples by its own
    // PIXEL_SCALE (tests/data/static-transform-world set c: img-plain, img-ps-target, img-ps-own).
    void imageOnTransformedTargetMatchesStock() {
        const std::array<float,6> turn{0.866025f,0.5f,0,-0.5f,0.866025f,0};
        struct Case { const char* name; uint32_t target, image; };
        for (const Case c:{Case{"img-plain",0,0},Case{"img-ps-target",0x40000000u,0},Case{"img-ps-own",0,0x40000000u}}) {
            auto w=world(); w.atoms[42].renderIdentity=10; w.resources={{123,0},{456,0}};
            setFields(w.appearances[1], affinefixture::fields(turn,",\"appearanceFlags\":"+std::to_string(c.target)));
            w.appearances[2]=w.appearances[1]; w.appearances[2].fields.iconResID=1; w.appearances[2].fields.iconState="";
            w.appearances[2].fields.layerBits=std::bit_cast<uint32_t>(4.f);
            setFields(w.appearances[2], "{\"pixelX\":8,\"appearanceFlags\":"+std::to_string(c.image)+"}");
            w.imageInstances[7]={2,42,1,false,20,10};
            auto read=[](uint32_t id, QByteArray& out) {
                if (id!=456) { out=iconBytes(); return true; }
                QImage small(16,16,QImage::Format_RGBA8888); small.fill(QColor(240,120,0)); out=iconfixture::png(small); return true; };
            shell::TurfMap terrain; shell::EntityMap scene; const auto map=terrain.prepare(w,{},0,false);
            const auto sprites=scene.prepare(w,map,read,1000,nullptr,true);
            QVERIFY2(sprites.size()==2,qPrintable(c.name));
            const auto image=std::find_if(sprites.begin(),sprites.end(),[](const auto& s){return s.imageIdentity!=0;});
            const auto target=std::find_if(sprites.begin(),sprites.end(),[](const auto& s){return s.imageIdentity==0;});
            QVERIFY(image!=sprites.end() && target!=sprites.end());
            QVERIFY2(image->affine && image->image.size()==QSize(16,16),qPrintable(c.name));
            QCOMPARE(target->smooth,c.target==0); QCOMPARE(image->smooth,c.image==0);
        }
    }
    void chainAdmissionAndIngestLifetime() {
        model::Session session; session.worldMut()=world(); auto& w=session.worldMut();
        w.appearances[2]=w.appearances[1];
        auto frame=[](float duration,uint16_t count,uint16_t key=99,bool append=false) {
            QByteArray b; auto put=[&](uint32_t n,int bytes){for(int i=0;i<bytes;++i)b.append(char(n>>(8*i)));};
            put(2,1);put(42,4);put(key,2);put(0,4);put(append?0:1,1);put(count,2);
            for(int i=0;i<count;++i) {
                put(1,4);put(2,4);put(std::bit_cast<uint32_t>(duration),4);put(1,4);put(0,1);put(0,1);put(0,2);
                if(i==0&&!append)put(0,4);
            }
            return b;
        };
        auto feed=[&](const QByteArray& b) {session.setIngestClockNs(1'000'000'000);
            session.ingest(0xf0,{reinterpret_cast<const uint8_t*>(b.constData()),size_t(b.size())});};
        w.tickNanos=25'000'000; // fps 40: step times stay in deciseconds (easing-world playback check)
        feed(frame(10,1)); QCOMPARE(w.atoms[42].chain.startNs,int64_t(1'000'000'000));
        QCOMPARE(w.atoms[42].chain.tickNs,model::kAnimUnitNanos);
        QCOMPARE(w.atoms[42].chain.steps.size(),size_t(1));
        const auto identity=w.atoms[42].renderIdentity;
        feed(frame(10,0,100)); QVERIFY(w.atoms[42].chain.steps.empty());
        QVERIFY(w.atoms[42].renderIdentity!=identity); QCOMPARE(w.atoms[42].key,uint16_t(100));
        feed(frame(std::numeric_limits<float>::quiet_NaN(),1,100)); QVERIFY(w.atoms[42].chain.rejected);
        feed(frame(10,1,100,true)); QVERIFY(w.atoms[42].chain.rejected); // append cannot revive a partial chain
        feed(frame(10,1,100)); QVERIFY(!w.atoms[42].chain.rejected);
        feed(frame(10,128,100)); QCOMPARE(w.atoms[42].chain.steps.size(),size_t(128));
        feed(frame(10,1,100,true)); QVERIFY(w.atoms[42].chain.rejected); QVERIFY(w.atoms[42].chain.steps.empty());
        feed(frame(10,0,100)); QVERIFY(!w.atoms[42].chain.rejected);
        QCOMPARE(session.ingestErrors(),0);
        shell::TurfMap terrain; shell::EntityMap scene; const auto map=terrain.prepare(w,{},0,false);
        auto& atom=w.atoms[42];atom.hasPos=true;atom.x=atom.y=6;atom.appid=2;
        feed(frame(10,1,100));
        for(uint8_t easing:{2,5}) {atom.chain.steps[0].easing=easing;QVERIFY(!scene.prepare(w,map,reader(),1'500'000'000).empty());}
        atom.chain.steps[0].easing=63; QVERIFY(scene.prepare(w,map,reader(),1'500'000'000).empty()); // unknown curve
        atom.chain.steps[0].easing=0; atom.chain.steps[0].flags=8;
        QVERIFY(scene.prepare(w,map,reader(),1'500'000'000).empty());
        atom.chain.steps[0].flags=0; setFields(w.appearances[1], R"({"filters":[{"type":99,"flags":1}]})");
        QVERIFY(!scene.prepare(w,map,reader(),1'500'000'000).empty()); // animates, drawn unfiltered
        setFields(w.appearances[1], affinefixture::fields({0,-1,0,1,0,0}));
        {   // Turn(90) -> identity rotates through 45 degrees at unit scale; a
            // coefficient lerp would instead shrink the midpoint to ~0.71.
            const auto mid=scene.prepare(w,map,reader(),1'500'000'000); QCOMPARE(mid.size(),1); QVERIFY(mid[0].affine);
            const auto& t=mid[0].affine->forward;
            QVERIFY(std::abs(std::hypot(t.m11(),t.m12())-1)<1e-6); QVERIFY(std::abs(std::abs(t.m11())-std::sqrt(.5))<1e-6);
        }
        atom.chain.steps[0].flags=2; // ANIMATION_LINEAR_TRANSFORM: component lerp
        {   const auto mid=scene.prepare(w,map,reader(),1'500'000'000); QCOMPARE(mid.size(),1); QVERIFY(mid[0].affine);
            QVERIFY(std::abs(std::hypot(mid[0].affine->forward.m11(),mid[0].affine->forward.m12())-std::sqrt(.5))<1e-6);
        }
        atom.chain.steps[0].flags=0;
        uint64_t work=0; QVERIFY(!shell::animation::admit(atom.chain,work));
    }
    void clientOffsetsRecenterAndUnsupportedChains() {
        auto w=world(); shell::TurfMap terrain;
        const auto initial=terrain.prepare(w,{},0,false); QVERIFY(!initial.image.isNull());
        w.viewPixelX=300; w.viewPixelY=-200;
        const auto shifted=terrain.prepare(w,{},0,false); QVERIFY(!shifted.image.isNull());
        QVERIFY(shifted.image.rect().contains(shifted.source));
        QCOMPARE(shifted.geometry.region.size(),initial.geometry.region.size());
        w.clientChain={1000,100,{{1,1,std::bit_cast<uint32_t>(10.f),63,0,1,1000}}}; // unknown easing curve
        QVERIFY(terrain.prepare(w,{},1500,false).image.isNull());
        w.clientChain={}; QVERIFY(!terrain.prepare(w,{},1500,false).image.isNull());
    }
    void staticAffineGeometryAndReuse() {
        auto w = world(); shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto map = terrain.prepare(w,{},0,false);
        auto prepare = [&] { return scene.prepare(w,map,reader(),0,&stats); };
        const auto identity = prepare(); QCOMPARE(identity.size(),1);
        const auto key = identity[0].image.cacheKey(); const QPoint origin = identity[0].position;
        const std::array<std::array<float,6>,5> matrices{{
            {0,-1,0,1,0,0}, {1,0,9,0,1,5}, {-1,0,0,0,1,0}, {1,.5f,0,0,1,0}, {.8f,-.6f,0,.6f,.8f,0}}};
        for (const auto& m : matrices) {
            setFields(w.appearances[1], affinefixture::fields(m));
            const auto sprites = prepare(); QCOMPARE(sprites.size(),1); QVERIFY(sprites[0].affine);
            QCOMPARE(sprites[0].image.cacheKey(),key); QCOMPARE(stats.frameDecodes,uint64_t(0));
            QCOMPARE(sprites[0].position,origin); QVERIFY(!(sprites[0] == identity[0]));
            QCOMPARE(sprites[0].affine->forward.map(QPointF(16,16)),QPointF(16+m[2],16-m[5]));
            QCOMPARE(sprites[0].rect(),sprites[0].affine->bounds.translated(origin));
            const auto unchanged = prepare(); QVERIFY(unchanged == sprites); QCOMPARE(stats.templateBuilds,uint64_t(0));
        }
        QVERIFY(!identity[0].affine); // previously published frame is immutable
        setFields(w.appearances[1], affinefixture::fields({1.5f,0,0,0,.75f,0}));
        const auto scaled = prepare(); QCOMPARE(scaled.size(),1); QCOMPARE(scaled[0].rect(),QRect(origin+QPoint(-8,4),QSize(48,24)));
        QVERIFY(!scaled[0].affine); QCOMPARE(scaled[0].image.cacheKey(),key);
        for (float value : {0.f, 1e-7f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1e30f}) {
            setFields(w.appearances[1], affinefixture::fields({value,0,0,0,1,0})); QVERIFY(prepare().empty());
        }
        setFields(w.appearances[1], affinefixture::fields({1,2,0,.5f,1,0})); QVERIFY(prepare().empty()); // singular
        setFields(w.appearances[1], affinefixture::fields({1,0,9000,0,1,0})); QVERIFY(prepare().empty());
        setFields(w.appearances[1], affinefixture::fields({0,-1,0,1,0,0},",\"overlays\":[2]"));
        w.appearances[2] = w.appearances[1]; setFields(w.appearances[2], "{}");
        QCOMPARE(prepare().size(),2); // verified root inheritance, not partial transformation
        setFields(w.appearances[1], "{\"overlays\":[2]}");
        setFields(w.appearances[2], affinefixture::fields({0,-1,0,1,0,0}));
        { const auto t = prepare(); QCOMPARE(t.size(),2); QVERIFY(!t[0].affine); QVERIFY(t[1].affine); } // child turns alone
        setFields(w.appearances[1], affinefixture::fields({1,0,9,0,1,5}));
        w.atoms[model::kRegistryA|42] = w.atoms[42]; w.atoms.erase(42); QCOMPARE(prepare().size(),1);
        w.atoms[42] = w.atoms[model::kRegistryA|42]; w.atoms.erase(model::kRegistryA|42);
        setFields(w.appearances[1], "{}"); QCOMPARE(prepare().size(),1);
        // Culling must use transformed bounds, not the off-view source cell.
        w.atoms[42].x = 1; QVERIFY(prepare().empty());
        setFields(w.appearances[1], affinefixture::fields({1,0,160,0,1,0})); QCOMPARE(prepare().size(),1);
    }
    void childlessMobAffineAdmission() {
        auto w = world(); shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const uint32_t mob = model::kRegistryA | 42;
        w.atoms[mob] = w.atoms[42]; w.atoms[mob].renderIdentity = 123;
        // Actual failure also uses a whole-icon opacity recipe after regeneration.
        w.resources.push_back({456,12}); w.appearances[1].fields.iconResID = 1;
        const auto read = [](uint32_t id,QByteArray& out) { out = id==456 ? opacityfixture::recipe(0) : iconBytes(); return true; };
        const auto map = terrain.prepare(w,{},0,false);
        auto prepare = [&] { return scene.prepare(w,map,read,0,&stats); };
        const auto standing = prepare(); QCOMPARE(standing.size(),2);
        const auto asset = standing[0].image.cacheKey();
        // Captured settled lying matrix: identical appearance in both registries.
        setFields(w.appearances[1], affinefixture::fields({0,1,0,-1,0,0}));
        w.atoms[mob].pixelY = -6;
        const auto lying = prepare(); QCOMPARE(lying.size(),2);
        for (const auto& sprite : lying) {
            QVERIFY(sprite.affine); QCOMPARE(sprite.image.cacheKey(),asset);
            if (sprite.atom == mob) {
                QCOMPARE(sprite.targetIdentity,uint64_t(123)); QCOMPARE(sprite.key,uint16_t(99));
                QCOMPARE(sprite.position,standing[1].position + QPoint(0,6));
            }
        }
        QCOMPARE(stats.frameDecodes,uint64_t(0));
        QVERIFY(!standing[0].affine); QVERIFY(!standing[1].affine);
        // Same scalar geometry subset as objects; no registry-specific asset work.
        for (const auto& matrix : std::array<std::array<float,6>,4>{{
                {1,0,9,0,1,5}, {-1,0,0,0,1,0}, {1,.5f,0,0,1,0}, {1.5f,0,0,0,.75f,0}}}) {
            setFields(w.appearances[1], affinefixture::fields(matrix));
            const auto sprites = prepare(); QCOMPARE(sprites.size(),2);
            QCOMPARE(sprites[0].image.cacheKey(),asset); QCOMPARE(sprites[1].image.cacheKey(),asset);
        }
        w.appearances[2] = w.appearances[1]; setFields(w.appearances[2], "{}");
        for (const char* children : {",\"overlays\":[2]", ",\"underlays\":[2]"}) {
            setFields(w.appearances[1], affinefixture::fields({0,1,0,-1,0,0},children));
            QCOMPARE(prepare().size(),4); // ordinary root inheritance in BOTH registries
        }
        setFields(w.appearances[1], "{\"overlays\":[2]}");
        setFields(w.appearances[2], affinefixture::fields({0,1,0,-1,0,0}));
        { const auto t = prepare(); QVERIFY(std::any_of(t.begin(),t.end(),[](const auto& p){return p.affine.has_value();})); } // transformed child composes
        setFields(w.appearances[1], affinefixture::fields({0,0,0,0,1,0}));
        QVERIFY(prepare().empty()); // invalid matrices never fall back to identity
        w.atoms.erase(42);
        setFields(w.appearances[1], "{}"); w.atoms[mob].x = 1;
        QVERIFY(prepare().empty());
        setFields(w.appearances[1], affinefixture::fields({1,0,160,0,1,0}));
        QCOMPARE(prepare().size(),1); // transformed bounds, not the source cell
    }
    void ordinaryRootTrees() {
        auto w=roottreefixture::world(true,true); shell::TurfMap terrain; shell::EntityMap scene;
        const auto frame=terrain.prepare(w,{},0,false); shell::EntityMap::Stats stats;
        auto prepare=[&]{return scene.prepare(w,frame,roottreefixture::read,1,&stats,true);};
        const auto standing=prepare(); QCOMPARE(standing.size(),4);
        const auto rootTop=standing[1].position; const auto key=standing[1].image.cacheKey();
        for(int cycle=0;cycle<3;++cycle) for(const auto& m:std::array<std::array<float,6>,5>{{
                {0,-1,9,1,0,5},{0,1,9,-1,0,5},{.5f,0,9,0,.5f,5},{1,0,9,0,1,5},{1,0,0,0,1,0}}}) {
            roottreefixture::pose(w,m); const auto s=prepare(); QCOMPARE(s.size(),4);
            QCOMPARE(s[1].image.cacheKey(),key); QCOMPARE(stats.frameDecodes,uint64_t(0));
            for(int i=0;i<4;++i) {
                QCOMPARE(s[i].position,standing[i].position); QCOMPARE(s[i].targetIdentity,uint64_t(10));
                if(m==std::array<float,6>{1,0,0,0,1,0}) QVERIFY(!s[i].rootAffine);
                else {QVERIFY(s[i].affine); QVERIFY(s[i].rootAffine); QCOMPARE(s[i].rootSize,QSize(32,32));
                    QCOMPARE(s[i].rootAffine,s[1].affine);}
            }
            QCOMPARE(s[2].position-rootTop,QPoint(25,7)); QCOMPARE(s[3].position-rootTop,QPoint(38,12));
            QVERIFY(prepare()==s); QCOMPARE(stats.templateBuilds,uint64_t(0));
        }
        QVERIFY(!standing[1].affine);
        roottreefixture::pose(w,{0,-1,9,1,0,5});
        const auto child=fieldsJson(w.appearances[2]);
        for(const auto& fields:std::vector<std::string>{
                R"({"renderTarget":"x"})", R"({"overlays":[1]})"}) { // a relay overlay draws (stock 516.1689)
            setFields(w.appearances[2], fields); QVERIFY(prepare().empty());
        }
        // A KEEP_TOGETHER or KEEP_APART child under a turned root is drawn (stock 516.1688, transform-group-world).
        for(const auto& fields:std::vector<std::string>{R"({"appearanceFlags":67108864})", R"({"appearanceFlags":134217728})"}) {
            setFields(w.appearances[2], fields); QCOMPARE(prepare().size(),3); // root, underlay and the childless overlay
        }
        // Child transforms compose with the root's quarter turn; RESET_TRANSFORM
        // drops it; maptext turns with the tree.
        auto childPart=[](const auto& s)->const shell::MapSprite* {
            for(const auto& p:s) if(p.image.size()==QSize(48,16)) return &p;
            return nullptr;
        };
        setFields(w.appearances[2], affinefixture::fields({0,-1,0,1,0,0}));
        { const auto s=prepare(); const auto* p=childPart(s); QVERIFY(p && p->affine); // 90 + 90 = 180
          QVERIFY(std::abs(p->affine->forward.m11()+1)<1e-9); QVERIFY(std::abs(p->affine->forward.m22()+1)<1e-9); }
        setFields(w.appearances[2], affinefixture::fields({.5f,0,0,0,.5f,0}));
        { const auto s=prepare(); const auto* p=childPart(s); QVERIFY(p && p->affine);
          QVERIFY(std::abs(std::abs(p->affine->forward.m12())-.5)<1e-9); QVERIFY(std::abs(p->affine->forward.m11())<1e-9); }
        setFields(w.appearances[2], R"({"appearanceFlags":16777216})");
        { const auto s=prepare(); const auto* p=childPart(s); QVERIFY(p && p->affine); QVERIFY(!p->affine->visualCoordinates); }
        // Transformed maptext needs a GUI font database: see maptext_test.
        // RESET_COLOR / RESET_ALPHA children only reset inherited RGBA (vg's
        // bloody-feet overlay on a lying human): the transformed tree stays.
        setFields(w.appearances[2], "{}"); const auto plain=prepare(); QVERIFY(!plain.empty());
        for(const char* fields:{R"({"appearanceFlags":4194304})",R"({"appearanceFlags":8388608})"}) {
            setFields(w.appearances[2], fields); const auto s=prepare(); QCOMPARE(s.size(),plain.size());
            for(qsizetype i=0;i<s.size();++i) { QVERIFY(s[i].rootAffine); QCOMPARE(s[i].position,plain[i].position); }
        }
        setFields(w.appearances[2], R"({"filters":[{"type":99,"flags":1}]})"); QCOMPARE(prepare().size(),plain.size()); // drawn unfiltered
        setFields(w.appearances[2], child);
        // Any invertible matrix: shear, scale 2, fractional translation, vg's 0.7 shrink.
        for(const auto& m:std::array<std::array<float,6>,4>{{
                {1,.5f,0,0,1,0},{2,0,0,0,2,0},{0,-1,.5f,1,0,0},{1,0,0,0,.7f,0}}}) {
            roottreefixture::pose(w,m); const auto s=prepare(); QCOMPARE(s.size(),4);
            for(const auto& sprite:s) { QVERIFY(sprite.affine); QVERIFY(sprite.rootAffine); }
        }
        roottreefixture::pose(w,{0,0,0,0,1,0}); QVERIFY(prepare().empty()); // singular
        roottreefixture::pose(w,{0,-1,9,1,0,5});
        // Late dependency: no stale or partially transformed pose; retry normally.
        shell::EntityMap late; bool available=false;
        auto read=[&](uint32_t id,QByteArray& bytes){return (id!=101 || available) && roottreefixture::read(id,bytes);};
        QVERIFY(late.prepare(w,frame,read,1,nullptr,true).empty()); available=true;
        QCOMPARE(late.prepare(w,frame,read,1,nullptr,true).size(),4);
        w.resources[1].first=999;
        auto oversized=[](uint32_t id,QByteArray& bytes){
            if(id!=999) return roottreefixture::read(id,bytes);
            QImage im(64,32,QImage::Format_RGBA8888);im.fill(Qt::red);bytes=iconfixture::png(im);return true;};
        // Larger parts keep their own center pivot instead of refusing the tree.
        { const auto s=late.prepare(w,frame,oversized,1,nullptr,true); QCOMPARE(s.size(),4);
          QVERIFY(std::any_of(s.begin(),s.end(),[](const auto& p){return p.image.size()==QSize(64,32) && p.affine;})); }
        w.resources[1].first=101;
        w.appearances[5]=w.appearances[4]; setFields(w.appearances[4], R"({"overlays":[5]})");
        { const auto s=prepare(); QCOMPARE(s.size(),5); // deeper nesting composes too (vg clothing)
          for(const auto& p:s) QVERIFY(p.rootAffine); }
        setFields(w.appearances[4], "{}");
        w.atoms.begin()->second.x=1; QVERIFY(prepare().empty());
        roottreefixture::pose(w,{0,-1,160,1,0,0}); QCOMPARE(prepare().size(),4); // transformed off-view bounds
    }
    void imageIngestIdentityAndTiming_data() {
        QTest::addColumn<bool>("mob"); QTest::newRow("object") << false; QTest::newRow("mob") << true;
    }
    void imageIngestIdentityAndTiming() {
        QFETCH(bool,mob); const uint32_t target=42u | (mob ? model::kRegistryA : 0u);
        model::Session session; session.worldMut()=world(); auto& w=session.worldMut();
        w.appearances[2]=w.appearances[1];w.appearances[3]=w.appearances[1];w.appearances[3].fields.iconState="green";
        auto feed=[&](uint16_t type,const char* hex,int64_t ns){
            auto bytes=QByteArray::fromHex(hex);session.setIngestClockNs(ns);
            if(mob) {
                if(type==0xc7) { // a mob's FlickTarget makes no record; a stepless animate does
                    bytes[0]=3; const auto made=bytes.left(7)+QByteArray(7,char(0));
                    session.ingest(0xf0,{reinterpret_cast<const uint8_t*>(made.constData()),size_t(made.size())});
                }
                if(type==0x33 || type==0x35) bytes[8]=2;
                if(type==0x6f) type=0x6e;
                if(type==0x24) type=0x09;
            }
            session.ingest(type,{reinterpret_cast<const uint8_t*>(bytes.constData()),size_t(bytes.size())});
        };
        feed(0xc7,"022a000000630001000000",90);
        feed(0x33,"0700000001000000012a0000000000",100);
        const auto first=w.imageInstances.at(7);
        QVERIFY(w.imageTargetCurrent(7,first.identity,target,first.targetIdentity));
        feed(0x35,"0700000002000000012a0000000000",110);
        QCOMPARE(w.imageIconStart.at(7).ns,int64_t(100));QCOMPARE(w.imageInstances.at(7).identity,first.identity);
        feed(0x35,"0700000003000000012a0000000000",120);
        QCOMPARE(w.imageIconStart.at(7).ns,int64_t(120));
        feed(0x33,"0700000003000000012a0000000000",130);
        QVERIFY(w.imageInstances.at(7).identity!=first.identity);QCOMPARE(w.imageIconStart.at(7).ns,int64_t(130));
        const auto second=w.imageInstances.at(7);
        feed(0xc7,"022a000000640001000000",140);
        QVERIFY(!w.imageTargetCurrent(7,second.identity,target,second.targetIdentity));
        feed(0x35,"0700000003000000012a0000000000",150);
        QCOMPARE(w.imageInstances.at(7).targetIdentity,second.targetIdentity); // no silent rebinding
        feed(0x33,"0800000001000000012b0000000000",160); // normal image-before-target delivery
        QVERIFY(w.pendingImageTargets.count(target+1));
        feed(0xc7,"022b000000650001000000",170);
        QVERIFY(w.pendingImageTargets.empty());
        auto late=w.imageInstances.at(8);QVERIFY(w.imageTargetCurrent(8,late.identity,target+1,late.targetIdentity));
        feed(0x6f,"2b000000",180);feed(0xc7,"022b000000650001000000",190); // same key after deletion
        QVERIFY(!w.imageTargetCurrent(8,late.identity,target+1,late.targetIdentity));
        feed(0x33,"0900000001000000012c0000000000",200);
        feed(0x6f,"2c000000",210);feed(0xc7,"022c000000650001000000",220);
        QVERIFY(w.pendingImageTargets.empty());QCOMPARE(w.imageInstances.at(9).targetIdentity,UINT64_MAX);
        feed(0x33,"0700000001000000012a0000000000",225);
        auto pinned=w.imageInstances.at(7);
        feed(0xc7,"022a000000000001000000",226); // zero-key transition: stricter render identity
        QVERIFY(!w.imageTargetCurrent(7,pinned.identity,target,pinned.targetIdentity));
        feed(0x33,"0700000001000000012a0000000000",227);pinned=w.imageInstances.at(7);
        w.resync.freed.insert(target);
        feed(0x24,"2a00000001000000",228); // GC resurrection via binding reply, no new key
        QVERIFY(!w.imageTargetCurrent(7,pinned.identity,target,pinned.targetIdentity));
        feed(0x34,"070000000800000009000000",230);
        QVERIFY(w.imageInstances.empty());QVERIFY(w.imageIconStart.empty());QVERIFY(w.images.empty());
        QCOMPARE(session.ingestErrors(),0);
    }
    void imageRegistryQualifiedBinding() {
        model::Session session; auto& w=session.worldMut();
        const uint32_t mob=42u|model::kRegistryA;
        auto feed=[&](uint16_t type,const char* hex){const auto b=QByteArray::fromHex(hex);
            session.ingest(type,{reinterpret_cast<const uint8_t*>(b.constData()),size_t(b.size())});};
        feed(0x33,"0100000001000000012a0000000000");
        feed(0x33,"0200000001000000022a0000000000");
        QCOMPARE(w.pendingImageTargets.size(),size_t(2));
        feed(0xc7,"022a000000630001000000");
        QVERIFY(w.imageInstances.at(1).targetIdentity); QVERIFY(!w.imageInstances.at(2).targetIdentity);
        QVERIFY(w.pendingImageTargets.count(mob));
        // Appearance-only replies create a provisional unkeyed atom. Wait for
        // its first keyed record rather than pinning the placeholder lifetime.
        feed(0x09,"2a00000001000000");
        QVERIFY(!w.imageInstances.at(2).targetIdentity); QVERIFY(w.pendingImageTargets.count(mob));
        feed(0xc7,"032a000000630001000000");
        const auto image=w.imageInstances.at(2);
        QVERIFY(w.imageTargetCurrent(2,image.identity,mob,image.targetIdentity));
        QVERIFY(!w.imageTargetCurrent(2,image.identity,42,image.targetIdentity));
        QVERIFY(w.pendingImageTargets.empty());
        feed(0x6f,"2a000000"); // object removal must not invalidate same-number mob
        QVERIFY(w.imageTargetCurrent(2,image.identity,mob,image.targetIdentity));
        feed(0x35,"0200000002000000012a0000000000"); // explicit registry retarget
        QCOMPARE(w.imageInstances.at(2).target,42u); QVERIFY(w.pendingImageTargets.count(42));
        feed(0x34,"02000000"); QVERIFY(w.pendingImageTargets.empty());
        feed(0xc7,"022a000000630001000000"); QVERIFY(!w.imageInstances.count(2));
        feed(0x33,"0200000001000000022a0000000000");
        QVERIFY(w.imageTargetCurrent(2,w.imageInstances.at(2).identity,mob,image.targetIdentity));
        feed(0x33,"0300000001000000022b0000000000");
        feed(0x6f,"2b000000"); QVERIFY(w.pendingImageTargets.count(mob+1));
        feed(0x6e,"2b000000"); QVERIFY(w.pendingImageTargets.empty());
        feed(0x35,"0300000002000000022b0000000000");
        feed(0x09,"2b00000001000000"); QCOMPARE(w.imageInstances.at(3).targetIdentity,UINT64_MAX);
        feed(0x09,"2c00000001000000"); // provisional target already exists before image
        feed(0x33,"0400000001000000022c0000000000");
        QVERIFY(!w.imageInstances.at(4).targetIdentity); QVERIFY(w.pendingImageTargets.count(mob+2));
        feed(0xc7,"032c000000630001000000");
        const auto bound=w.imageInstances.at(4);
        QVERIFY(w.imageTargetCurrent(4,bound.identity,mob+2,bound.targetIdentity));
        QCOMPARE(session.ingestErrors(),0);
    }
    void mobImagePlacementTreesAndAssets() {
        auto w=world(); const uint32_t mob=42u|model::kRegistryA;
        w.atoms[mob]=w.atoms.at(42); auto& target=w.atoms[mob]; target.renderIdentity=10;
        w.atoms[42].renderIdentity=11; w.atoms[42].x=1; // independent same-number object
        w.appearances[2]=w.appearances[1]; w.appearances[2].fields.iconState="green";
        setFields(w.appearances[2], R"({"pixelX":25,"pixelY":9})");
        w.appearances[2].fields.layerBits=std::bit_cast<uint32_t>(-1.f);
        w.appearances[3]=w.appearances[1]; setFields(w.appearances[3], R"({"pixelX":-12,"pixelY":-9})");
        w.imageInstances[7]={2,mob,2,false,20,10};
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto map=terrain.prepare(w,{},0,false);
        auto prepare=[&]{return scene.prepare(w,map,reader(),150,&stats,true);};
        const auto first=prepare(); QCOMPARE(first.size(),2); QCOMPARE(first.back().atom,mob);
        const auto asset=first.back().image.cacheKey();
        setFields(w.appearances[1], affinefixture::fields({0,-1,0,1,0,0},",\"appearanceFlags\":6"));
        const auto leaf=prepare(); QCOMPARE(leaf.size(),2); QVERIFY(leaf.back().affine);
        for(const auto& m:std::array<std::array<float,6>,4>{{{1,0,0,0,1,0},{0,-1,0,1,0,0},{0,1,0,-1,0,0},{.5f,0,0,0,.5f,0}}}) {
            setFields(w.appearances[1], affinefixture::fields(m,",\"overlays\":[3],\"pixelX\":7,\"pixelY\":5,\"appearanceFlags\":540035652"));
            const auto parts=prepare(); QCOMPARE(parts.size(),3);
            const auto& image=parts.back(); QCOMPARE(image.kind,shell::MapSprite::Image);
            QCOMPARE(image.image.cacheKey(),asset); QCOMPARE(image.position,first.back().position+QPoint(7,-5));
            // The target tree is not copied, but clicks report the target's icon through its matrix (stock 516.1688,
            // tests/data/click-world image-turned-target).
            const bool turned=m!=std::array<float,6>{1,0,0,0,1,0};
            QCOMPARE(image.layer,std::nextafter(5.f,0.f)); QCOMPARE(bool(image.rootAffine),turned); // a float image: FLY_LAYER
        }
        const auto settled=prepare(); target.gliding=true; target.glide={-1,0,100,100};
        const auto moved=prepare();
        const auto imagePart=[](const auto& parts)->const shell::MapSprite& {
            return *std::find_if(parts.begin(),parts.end(),[](const auto& p){return p.kind==shell::MapSprite::Image;});
        };
        QCOMPARE(imagePart(moved).position-imagePart(settled).position,QPoint(-16,0));
        QCOMPARE(stats.frameDecodes,uint64_t(0)); QCOMPARE(stats.templateBuilds,uint64_t(0));
        target.gliding=false;
        setFields(w.appearances[3], R"({"overlays":[4]})"); w.appearances[4]=w.appearances[2];
        auto hasImage=[](const auto& parts){return std::any_of(parts.begin(),parts.end(),[](const auto& p){return p.kind==shell::MapSprite::Image;});};
        // Nested target overlays (vg clothing) keep the attachment while every
        // resolved target part stays inside the 32px cell.
        QVERIFY(hasImage(prepare()));
        setFields(w.appearances[3], R"({"filters":[{"type":99,"flags":1}]})"); QVERIFY(hasImage(prepare())); // filtered targets are drawn unfiltered
        setFields(w.appearances[3], "{}");
        for(const auto& m:std::array<std::array<float,6>,2>{{{.8f,0,0,0,.8f,0},{1,0,0,0,.7f,0}}}) {
            setFields(w.appearances[1], affinefixture::fields(m,",\"overlays\":[3]"));
            QVERIFY(hasImage(prepare())); // vg's 0.7 shrink keeps speech bubbles on the target
        }
        setFields(w.appearances[1], affinefixture::fields({0,-1,0,1,0,0},",\"overlays\":[3]"));
        w.resources[0]={456,0}; bool ready=false;
        auto late=[&](uint32_t,QByteArray& bytes){bytes=iconBytes();return ready;};
        QVERIFY(!hasImage(scene.prepare(w,map,late,150,nullptr,true))); ready=true;
        QVERIFY(hasImage(scene.prepare(w,map,late,150,nullptr,true)));
        target.renderIdentity++; QVERIFY(!hasImage(prepare())); QCOMPARE(first.back().targetIdentity,uint64_t(10));
    }
    void imageLimitsAndGeneratedDependencies() {
        model::Session session;auto& limited=session.worldMut();
        for(uint32_t i=0;i<65536;++i)limited.images[i]={};
        auto feed=[&](const char* hex){const auto b=QByteArray::fromHex(hex);
            session.ingest(0x33,{reinterpret_cast<const uint8_t*>(b.constData()),size_t(b.size())});};
        feed("0000010001000000012a0000000000");QCOMPARE(limited.images.size(),size_t(65536));QVERIFY(limited.imageInstances.empty());
        feed("0000000001000000012a0000000000");QCOMPARE(limited.pendingImageTargets.at(42).size(),size_t(1));
        auto w=world();w.atoms[42].renderIdentity=1;w.appearances[1].fields.iconResID=UINT32_MAX;
        w.appearances[2]=w.appearances[1];w.appearances[2].fields.iconResID=0;w.appearances[2].fields.iconState="base";
        w.imageInstances[7]={2,42,1,false,1,1};w.resources={{100,0},{200,0},{300,0}};
        bool ready=false;auto read=[&](uint32_t id,QByteArray& out){
            if(id==100)out=iconfixture::recipe(iconfixture::select(1,""));
            else if(id==200)out=iconfixture::recipe(iconfixture::select(2,"base"));
            else {out=iconfixture::sheet(id==300?Qt::red:Qt::blue);return ready;}return true;};
        shell::TurfMap terrain;shell::EntityMap scene;const auto map=terrain.prepare(w,{},0,false);
        QVERIFY(scene.prepare(w,map,read,0,nullptr,true).isEmpty());ready=true;
        const auto first=scene.prepare(w,map,read,0,nullptr,true);QCOMPARE(first.size(),1);QCOMPARE(first[0].image.pixelColor(16,16),QColor(Qt::red));
        w.resources[2]={400,0};const auto remap=scene.prepare(w,map,read,0,nullptr,true);
        QCOMPARE(remap[0].image.pixelColor(16,16),QColor(Qt::blue));QCOMPARE(first[0].image.pixelColor(16,16),QColor(Qt::red));
        // Cached attachment copies still charge traversal work. Hostile input cannot
        // grow published output or retained instances beyond the shared scene budget.
        for(uint32_t i=0;i<65536;++i)w.imageInstances[i]={2,42,1,false,uint64_t(i)+1,1};
        shell::EntityMap::Stats stats;const auto bounded=scene.prepare(w,map,read,0,&stats,true);
        QVERIFY(bounded.size()<=65536);QVERIFY(stats.candidates<=65536);
    }
    void imagePlacementInheritanceOrderingAndReuse() {
        auto w=world();w.atoms[42].renderIdentity=10;
        setFields(w.appearances[1], R"({"pixelX":4,"pixelY":3,"hasPlane":true,"plane":5,"hasColor":true,"colorR":128,"colorG":255,"colorB":255,"colorA":128})");
        w.appearances[2]=w.appearances[1];w.appearances[2].fields.iconState="green";w.appearances[2].fields.layerBits=std::bit_cast<uint32_t>(-1.f);
        setFields(w.appearances[2], R"({"pixelX":16,"hasPlane":true,"plane":-32767,"overlays":[3]})");
        w.appearances[3]=w.appearances[2];setFields(w.appearances[3], R"({"pixelY":16})");
        w.imageInstances[42]={2,42,1,false,20,10}; // same numeric ID as atom, separate namespace
        w.imageInstances[7]={2,42,1,false,21,10};
        shell::TurfMap terrain;shell::EntityMap scene;shell::EntityMap::Stats stats;
        auto map=terrain.prepare(w,{},0,false);
        const auto first=scene.prepare(w,map,reader(),100,nullptr,true);QCOMPARE(first.size(),5);
        QCOMPARE(first[1].kind,shell::MapSprite::Image);QCOMPARE(first[1].imageId,42u);QCOMPARE(first[3].imageId,7u);
        QCOMPARE(first[1].position-first[0].position,QPoint(16,0));QCOMPARE(first[2].position-first[1].position,QPoint(0,-16));
        QCOMPARE(first[1].plane,5);QCOMPARE(first[1].layer,std::nextafter(5.f,0.f));QCOMPARE(first[1].tint,first[0].tint);
        QCOMPARE(scene.prepare(w,map,reader(),100,&stats,true).constData(),first.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0));QCOMPARE(stats.sceneReuses,uint64_t(1));
        w.atoms[42].gliding=true;w.atoms[42].glide={-1,0,100,100};w.atoms[42].pixelX=2;
        const auto moved=scene.prepare(w,map,reader(),150,&stats,true);
        const auto firstPart=[](const auto& parts,uint32_t id)->const shell::MapSprite& {
            return *std::find_if(parts.begin(),parts.end(),[&](const auto& p){return p.imageId==id;});
        };
        QCOMPARE(firstPart(moved,42).position-first[1].position,QPoint(-14,0));
        QCOMPARE(firstPart(moved,0).position-first[0].position,QPoint(-14,0));
        QCOMPARE(stats.templateBuilds,uint64_t(0)); // westward cell-key crossing can reorder
        w.atoms[42].hasPos=false;QVERIFY(scene.prepare(w,map,reader(),160,nullptr,true).isEmpty());w.atoms[42].hasPos=true;
        QCOMPARE(scene.prepare(w,map,reader(),160,nullptr,true).size(),5);
        w.imageInstances[42].identity=22;
        const auto reordered=scene.prepare(w,map,reader(),160,nullptr,true);QCOMPARE(reordered[1].imageId,7u);
        w.atoms[42].renderIdentity=11;QCOMPARE(scene.prepare(w,map,reader(),160,nullptr,true).size(),1);
        QCOMPARE(first[1].targetIdentity,uint64_t(10));QCOMPARE(first[1].imageIdentity,uint64_t(20));
        w.atoms[42].renderIdentity=10;w.atoms[42].gliding=false;w.atoms[42].x=3;
        setFields(w.appearances[2], R"({"pixelX":64})");
        const auto outside=scene.prepare(w,map,reader(),200,nullptr,true);
        QCOMPARE(outside.size(),2);for(const auto& sprite:outside)QCOMPARE(sprite.kind,shell::MapSprite::Image);
        w.atoms.erase(42);QVERIFY(scene.prepare(w,map,reader(),200,nullptr,true).isEmpty());
    }
    void imageHudBindingRepetitionOverrideAndInput() {
        model::Session session; auto& w=session.worldMut(); w=world(); w.atoms.clear();
        setFields(w.appearances[1], R"({"screenLoc":"2,2 to 3,2"})");
        w.appearances[2]=w.appearances[1]; w.appearances[2].fields.iconState="green";
        setFields(w.appearances[2], R"({"pixelX":16})");
        auto image=[&](uint16_t type) { Bytes b;putLe32(b,7);putLe32(b,2);b.push_back(1);putLe32(b,42);putLe16(b,0);session.ingest(type,b); };
        auto hud=[&](uint8_t version) { const std::array<uint8_t,14> b{2,14,0,2,42,0,0,0,version,0,1,0,0,0};session.ingest(0x76,b); };
        image(0x33); QCOMPARE(w.pendingImageTargets.size(),size_t(1)); hud(9);
        QVERIFY(w.imageInstances.at(7).targetHud); QVERIFY(w.imageHudCurrent(7));
        shell::TurfMap terrain; shell::EntityMap scene; const auto frame=terrain.prepare(w,{},0,false);
        auto draw=[&]{return scene.prepare(w,frame,reader(),1,nullptr,true);};
        const auto first=draw(); QCOMPARE(first.size(),4);
        int attached=0; for(const auto& sprite:first) if(sprite.imageIdentity) {++attached;QCOMPARE(sprite.kind,shell::MapSprite::Screen);QVERIFY(sprite.hudIdentity);}
        QCOMPARE(attached,2);
        auto hit=shell::mapPointerAt({180,120},{320,240},{3,3},frame.source,frame.geometry,"map",true,first);
        QVERIFY(hit && hit->imageIdentity); QVERIFY(hit->screen); QCOMPARE(hit->click.loc.kind,proto::enc::RefNull);
        QVERIFY(shell::mapPointerCurrent(*hit,w,0));
        setFields(w.appearances[2], R"({"appearanceFlags":262144,"pixelX":16})");
        const auto replaced=draw(); QCOMPARE(replaced.size(),2); for(const auto& sprite:replaced) QVERIFY(sprite.imageIdentity);
        // A supported empty override still hides the parent. Unsupported effects do not.
        w.appearances[2].fields.iconResID=UINT32_MAX; QVERIFY(draw().empty());
        setFields(w.appearances[2], R"({"appearanceFlags":262144,"filters":[{"type":99,"flags":1}]})"); QVERIFY(draw().empty()); // filters no longer make it unsupported
        setFields(w.appearances[2], R"({"appearanceFlags":262144,"renderTarget":"x"})"); QCOMPARE(draw().size(),2);
        const std::array<uint8_t,9> remove{1,9,0,0,0,0,0,0,0};session.ingest(0x76,remove);
        QVERIFY(!shell::mapPointerCurrent(*hit,w,0)); QVERIFY(draw().empty()); hud(9);
        QVERIFY(!w.imageHudCurrent(7)); QCOMPARE(draw().size(),2); // same wire key, new membership: no rebind
        image(0x35); QVERIFY(!w.imageHudCurrent(7)); image(0x33); QVERIFY(w.imageHudCurrent(7));
        hud(10); QVERIFY(!w.imageHudCurrent(7)); QCOMPARE(session.ingestErrors(),0);
    }
    void imageOverridePreservesIndependentAttachments() {
        auto w=world(); w.atoms[42].renderIdentity=1;
        w.appearances[2]=w.appearances[1]; w.appearances[2].fields.iconState="green"; setFields(w.appearances[2], R"({"appearanceFlags":262144})");
        w.appearances[3]=w.appearances[1]; setFields(w.appearances[3], R"({"pixelY":32})");
        w.imageInstances[7]={2,42,1,false,20,1}; w.imageInstances[8]={3,42,1,false,21,1};
        shell::TurfMap terrain; shell::EntityMap scene; const auto frame=terrain.prepare(w,{},0,false);
        auto draw=[&]{return scene.prepare(w,frame,reader(),1,nullptr,true);};
        const auto first=draw(); QCOMPARE(first.size(),2); for(const auto& sprite:first) QCOMPARE(sprite.kind,shell::MapSprite::Image);
        QCOMPARE(first[0].image.pixelColor(16,16),QColor(Qt::green));
        w.imageInstances.erase(7); QCOMPARE(draw().size(),2); QCOMPARE(draw()[0].kind,shell::MapSprite::Entity);
        QCOMPARE(first[0].imageId,7u);
    }
    void imageUnsupportedAndVisibility() {
        auto w=world();w.atoms[42].renderIdentity=1;
        w.appearances[2]=w.appearances[1];w.appearances[2].fields.iconState="green";
        w.imageInstances[7]={2,42,1,false,1,1};
        shell::TurfMap terrain;shell::EntityMap scene;auto map=terrain.prepare(w,{},0,false);
        auto count=[&]{return scene.prepare(w,map,reader(),0,nullptr,true).size();};
        QCOMPARE(count(),2);
        for(uint8_t kind:{0,7,255}){w.imageInstances[7].locKind=kind;QCOMPARE(count(),1);}w.imageInstances[7].locKind=1;
        w.imageInstances[7].hasVis=true;QCOMPARE(count(),2);w.imageInstances[7].hasVis=false; // tail independent of root
        setFields(w.appearances[2], R"({"appearanceFlags":262148})"); QCOMPARE(count(),1); // override replaces target
        setFields(w.appearances[2], R"({"filters":[{"type":99,"flags":1}]})"); QCOMPARE(count(),2); // drawn unfiltered
        setFields(w.appearances[2], R"({"renderTarget":"x"})");QCOMPARE(count(),1);
        // A KEEP_TOGETHER image is drawn (stock 516.1689).
        setFields(w.appearances[2], R"({"appearanceFlags":67108864})");QCOMPARE(count(),2);
        setFields(w.appearances[2], R"({"vars":[{"bit":4,"data":"65"}]})");
        setFields(w.appearances[1], fieldsJson(w.appearances[2]));
        QCOMPARE(count(),1); // invisible target, visible image, image root invisibility ignored
        setFields(w.appearances[1], affinefixture::fields({1,0,9,0,1,5}));QCOMPARE(count(),2);
        setFields(w.appearances[1], "{}");setFields(w.appearances[2], R"({"overlays":[3]})");
        w.appearances[3]=w.appearances[2];setFields(w.appearances[3], R"({"appearanceFlags":262144})");
        QCOMPARE(count(),1); // replacement child rejects the entire attachment
        setFields(w.appearances[3], "{}");QCOMPARE(count(),3);
        w.appearances[2].fields.layerBits=std::bit_cast<uint32_t>(-2.f);QCOMPARE(count(),3); // any negative layer floats
    }
    void imageStaticTargetTransforms() {
        auto w=world(); w.atoms[42].renderIdentity=10;
        w.appearances[2]=w.appearances[1]; w.appearances[2].fields.iconState="green";
        setFields(w.appearances[2], R"({"pixelX":25,"pixelY":9})");
        w.imageInstances[7]={2,42,1,false,20,10};
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        auto map=terrain.prepare(w,{},0,false);
        auto prepare=[&]{return scene.prepare(w,map,reader(),1,&stats,true);};
        const auto absent=prepare(); QCOMPARE(absent.size(),2);
        const auto asset=absent.back().image.cacheKey();
        const std::array<std::array<float,6>,5> matrices{{
            {1,0,0,0,1,0},{0,-1,9,1,0,5},{.5f,0,9,0,.5f,5},{0,1,0,-1,0,0},{1,0,9,0,1,5}}};
        for(int cycle=0;cycle<3;++cycle) for(const auto& m:matrices) {
            setFields(w.appearances[1], affinefixture::fields(m,",\"pixelX\":7,\"pixelY\":5"));
            const auto sprites=prepare(); QCOMPARE(sprites.size(),2);
            const auto& image=sprites.back(); QCOMPARE(image.kind,shell::MapSprite::Image);
            QCOMPARE(image.position,absent.back().position+QPoint(7,-5));
            QCOMPARE(image.image.cacheKey(),asset); QCOMPARE(stats.frameDecodes,uint64_t(0));
            QCOMPARE(image.imageIdentity,uint64_t(20)); QCOMPARE(image.targetIdentity,uint64_t(10));
            if(m==matrices[0]) QVERIFY(!image.affine);
            else {
                QVERIFY(image.affine);
                QCOMPARE(image.affine->forward.map(QPointF(16,16)),QPointF(16+m[2],16-m[5]));
                QCOMPARE(image.rect(),image.affine->bounds.translated(image.position));
            }
        }
        QVERIFY(!absent.back().affine);
        // Explicit target identity retains the original image tree path, not just leaves.
        setFields(w.appearances[1], affinefixture::fields({1,0,0,0,1,0}));
        w.appearances[3]=w.appearances[2]; setFields(w.appearances[2], R"({"overlays":[3]})");
        QCOMPARE(prepare().size(),3);
        // Image trees on a transformed target are drawn, each part about its own pivot (image-tree-transform-world).
        setFields(w.appearances[1], affinefixture::fields({.5f,0,0,0,.5f,0})); QCOMPARE(prepare().size(),3);
        setFields(w.appearances[2], "{}");
        // Any invertible target matrix carries the image (shear, mirror, fractional
        // translation, nonuniform scale); image-owned transforms and image trees
        // compose below.
        for(const auto& m:std::array<std::array<float,6>,4>{{{1,.5f,0,0,1,0},{-1,0,0,0,1,0},{1,0,.5f,0,1,0},{.5f,0,0,0,1,0}}}) {
            setFields(w.appearances[1], affinefixture::fields(m)); const auto s=prepare(); QCOMPARE(s.size(),2);
            QCOMPARE(s.back().kind,shell::MapSprite::Image); QVERIFY(s.back().affine);
        }
        setFields(w.appearances[1], affinefixture::fields({0,0,0,0,1,0})); // singular: no image
        { const auto s=prepare(); QVERIFY(std::none_of(s.begin(),s.end(),[](const auto& p){return p.kind==shell::MapSprite::Image;})); }
        setFields(w.appearances[1], affinefixture::fields({0,-1,0,1,0,0}));
        setFields(w.appearances[2], R"({"filters":[{"type":99,"flags":1}]})"); QCOMPARE(prepare().size(),2); // drawn unfiltered
        setFields(w.appearances[2], affinefixture::fields({1,0,0,0,1,0})); QCOMPARE(prepare().size(),2);
        setFields(w.appearances[2], R"({"overlays":[3]})"); QCOMPARE(prepare().size(),3);
        // RESET_TRANSFORM (runechat) ignores the target matrix; NO_CLIENT_COLOR is
        // material-only and keeps the inherited quarter turn.
        setFields(w.appearances[2], R"({"appearanceFlags":16777216})");
        { const auto s=prepare(); QCOMPARE(s.size(),2); QVERIFY(!s.back().affine); }
        setFields(w.appearances[2], R"({"appearanceFlags":33554432})");
        { const auto s=prepare(); QCOMPARE(s.size(),2); QVERIFY(s.back().affine); }
        setFields(w.appearances[2], R"({"pixelX":64})"); w.atoms[42].x=3;
        const auto offview=prepare(); QCOMPARE(offview.size(),1); QCOMPARE(offview[0].kind,shell::MapSprite::Image);
        w.atoms[42].renderIdentity=11; QVERIFY(prepare().empty());
        w.atoms[42].renderIdentity=10;
        // Missing target/image assets retry without losing the transform or bind clock.
        w.resources[0]={456,0}; bool ready=false;
        auto late=[&](uint32_t,QByteArray& out){out=iconBytes();return ready;};
        QVERIFY(scene.prepare(w,map,late,1,nullptr,true).empty()); ready=true;
        const auto rebound=scene.prepare(w,map,late,1,nullptr,true); QCOMPARE(rebound.size(),1); QVERIFY(rebound[0].affine);
        w.atoms[42].x=6; w.resources[0]={789,0}; setFields(w.appearances[2], "{}");
        w.imageIconStart[7]={2,1'000'000'000};
        auto animated=[](uint32_t,QByteArray& out){out=iconfixture::animation(1);return true;};
        for(const auto& m:matrices) {
            setFields(w.appearances[1], affinefixture::fields(m));
            const auto playing=scene.prepare(w,map,animated,1'200'000'000,nullptr,true);
            QCOMPARE(playing.size(),2); QCOMPARE(playing.back().image.pixelColor(16,0),QColor(210,30,20));
        }
        w.imageInstances.erase(7); QCOMPARE(scene.prepare(w,map,animated,1'250'000'000,nullptr,true).size(),1);
        w.imageInstances[7]={2,42,1,false,21,10}; w.imageIconStart[7]={2,1'300'000'000};
        const auto restarted=scene.prepare(w,map,animated,1'300'000'000,nullptr,true);
        QCOMPARE(restarted.back().image.pixelColor(16,0),QColor(50,30,20)); QCOMPARE(restarted.back().imageIdentity,uint64_t(21));
    }
    void imageRootDirectionFlags() {
        auto w=world();w.atoms[42].renderIdentity=1;w.atoms[42].dir=4;
        w.appearances[1].fields.iconResID=UINT32_MAX;
        w.appearances[2]=w.appearances[1];w.appearances[2].fields.iconResID=0;w.appearances[2].fields.iconState="";
        setFields(w.appearances[2], R"({"dir":2,"appearanceFlags":4,"overlays":[3,4]})");
        w.appearances[3]=w.appearances[2];setFields(w.appearances[3], R"({"dir":2})");
        w.appearances[4]=w.appearances[2];setFields(w.appearances[4], R"({"dir":1,"appearanceFlags":131072})");
        w.imageInstances[7]={2,42,1,false,1,1};
        shell::TurfMap terrain;shell::EntityMap scene;auto map=terrain.prepare(w,{},0,false);
        auto read=[](uint32_t,QByteArray& out){out=iconfixture::animation();return true;};
        const auto east=scene.prepare(w,map,read,1,nullptr,true);QCOMPARE(east.size(),3);
        QCOMPARE(east[0].image.pixelColor(16,0),QColor(50,70,20));QCOMPARE(east[1].image.cacheKey(),east[0].image.cacheKey());
        QCOMPARE(east[2].image.pixelColor(16,0),QColor(50,50,20));
        w.atoms[42].dir=8;const auto west=scene.prepare(w,map,read,1,nullptr,true);
        QCOMPARE(west[0].image.pixelColor(16,0),QColor(50,90,20));
        QCOMPARE(west[1].image.cacheKey(),west[0].image.cacheKey());QCOMPARE(west[2].image.cacheKey(),east[2].image.cacheKey());
        setFields(w.appearances[2], R"({"dir":2,"appearanceFlags":516,"overlays":[3,4]})");
        const auto assigned=scene.prepare(w,map,read,1,nullptr,true);
        QCOMPARE(assigned[0].image.pixelColor(16,0),QColor(50,30,20));
        w.atoms[42].dir=1;QCOMPARE(scene.prepare(w,map,read,1,nullptr,true).constData(),assigned.constData());
    }
    void imageAnimationLateAssetsAndDirection_data() {
        QTest::addColumn<bool>("mob"); QTest::newRow("object") << false; QTest::newRow("mob") << true;
    }
    void imageAnimationLateAssetsAndDirection() {
        QFETCH(bool,mob); const uint32_t key=42u | (mob ? model::kRegistryA : 0u);
        auto w=world(); if(mob) {w.atoms[key]=w.atoms.at(42);w.atoms.erase(42);}
        w.atoms[key].renderIdentity=1;w.atoms[key].dir=8;
        w.appearances[1].fields.iconResID=UINT32_MAX;
        w.appearances[2]=w.appearances[1];w.appearances[2].fields.iconResID=0;w.appearances[2].fields.iconState="";
        setFields(w.appearances[2], R"({"dir":4,"appearanceFlags":512,"overlays":[3]})");
        w.imageInstances[7]={2,key,uint8_t(mob?2:1),false,1,1};w.imageIconStart[7]={2,1000000000};
        bool ready=false;auto read=[&](uint32_t,QByteArray& out){out=iconfixture::animation(1);return ready;};
        shell::TurfMap terrain;shell::EntityMap scene;shell::EntityMap::Stats stats;auto map=terrain.prepare(w,{},0,false);
        QVERIFY(scene.prepare(w,map,read,1100000000,nullptr,true).isEmpty());ready=true;
        const auto first=scene.prepare(w,map,read,1200000000,nullptr,true);QCOMPARE(first.size(),1);
        QCOMPARE(first[0].image.pixelColor(16,0),QColor(210,70,20));
        w.appearances[3]=w.appearances[2];setFields(w.appearances[3], R"({"pixelY":8})");
        const auto children=scene.prepare(w,map,read,1250000000,nullptr,true);QCOMPARE(children.size(),2);
        QCOMPARE(children[0].image.cacheKey(),children[1].image.cacheKey());
        w.atoms[key].dir=1;
        QCOMPARE(scene.prepare(w,map,read,1260000000,&stats,true).constData(),children.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(1)); // target direction template only; image output reused
        w.atoms[key].hasPos=false;scene.prepare(w,map,read,1270000000,nullptr,true);w.atoms[key].hasPos=true;
        const auto back=scene.prepare(w,map,read,1280000000,nullptr,true);QCOMPARE(back[0].image.cacheKey(),first[0].image.cacheKey());
        w.resources[0]={456,0};ready=false;QVERIFY(scene.prepare(w,map,read,1290000000,nullptr,true).isEmpty());ready=true;
        const auto remap=scene.prepare(w,map,read,1300000000,nullptr,true);QCOMPARE(remap.size(),2);
        QCOMPARE(remap[0].image.pixelColor(16,0),QColor(210,70,20));QVERIFY(remap[0].image.cacheKey()!=first[0].image.cacheKey());
        w.imageIconStart[7].ns=1300000000;w.imageInstances[7].identity=2;
        const auto restarted=scene.prepare(w,map,read,1300000000,nullptr,true);
        QCOMPARE(restarted[0].image.pixelColor(16,0),QColor(50,70,20));QCOMPARE(first[0].image.pixelColor(16,0),QColor(210,70,20));
    }
    void screenLocationGrammar() {
        using shell::ScreenLoc;
        auto at = [](const char* text, QSize view = {7,5}, int tile = 32) {
            const auto loc = ScreenLoc::parse(text);
            return loc ? loc->positions(view,tile) : std::vector<QPoint>{};
        };
        QCOMPARE(at("1,1"), (std::vector<QPoint>{{0,0}}));
        QCOMPARE(at("2:3,2:5"), (std::vector<QPoint>{{35,37}}));
        QCOMPARE(at("1.5,1.5"), (std::vector<QPoint>{{16,16}}));
        QCOMPARE(at("CENTER"), (std::vector<QPoint>{{96,64}}));
        QCOMPARE(at("CENTER,CENTER",{8,6}), (std::vector<QPoint>{{112,80}}));
        QCOMPARE(at("CENTER:1,CENTER:-1"), (std::vector<QPoint>{{97,63}}));
        QCOMPARE(at("EAST-1:28,CENTER-1.5:15"), (std::vector<QPoint>{{188,31}}));
        QCOMPARE(at("WEST,SOUTH"),at("SOUTH,WEST"));
        QCOMPARE(at("SOUTHWEST"),at("1,1"));
        QCOMPARE(at("NORTHEAST"),at("7,5"));
        QCOMPARE(at("NORTHWEST"),at("1,5"));
        QCOMPARE(at("SOUTHEAST"),at("7,1"));
        QCOMPARE(at("EAST,NORTH",{7,5},16), (std::vector<QPoint>{{96,64}}));
        QCOMPARE(at("1,1 TO 3,2"), (std::vector<QPoint>{{0,0},{32,0},{64,0},{0,32},{32,32},{64,32}}));
        QCOMPARE(at("3,2 TO 1,1"),at("1,1 TO 3,2"));
        QCOMPARE(at("4:16,2:16 to 10:16,2:16",{15,7}),
                 (std::vector<QPoint>{{112,48},{144,48},{176,48},{208,48},{240,48},{272,48},{304,48}}));
        QCOMPARE(at("4:16,2:16 To 10:16,2:16",{15,7}),at("4:16,2:16 to 10:16,2:16",{15,7}));
        // Written from the upper end down, both ends' offsets move the grid (master-screen-loc-world "3:4,1 to 1:8,1").
        QCOMPARE(at("10:16,2:16 to 4:16,2:16",{15,7}),
                 (std::vector<QPoint>{{128,48},{160,48},{192,48},{224,48},{256,48},{288,48},{320,48}}));
        QCOMPARE(at("3:-8,3:8 to 4:-8,2:8"),
                 (std::vector<QPoint>{{56,48},{88,48},{56,80},{88,80}}));
        QCOMPARE(at("3:-8,2:8 tO 4:-8,3:8"),
                 (std::vector<QPoint>{{56,40},{88,40},{56,72},{88,72}}));
        QCOMPARE(at("3:8,2:8 to 4:8,3:8",{7,5},16),
                 (std::vector<QPoint>{{40,24},{56,24},{40,40},{56,40}}));
        QCOMPARE(at("1:16,1:16 to 16:16,16:16",{16,16}).size(),size_t(256));
        QVERIFY(at("1:16,1:16 to 17:16,16:16",{17,16}).empty());
        QCOMPARE(at("WEST,SOUTH TO EAST,NORTH").size(),size_t(35));
        QCOMPARE(at("1,1 TO 16,16",{16,16}).size(),size_t(256));
        QVERIFY(at("1,1 TO 17,16",{17,16}).empty());
        QVERIFY(at("0,0").empty());QVERIFY(at("EAST+1,NORTH").empty());
        // The first end's pixel offsets move the grid; the second end's are ignored (master-screen-loc-world
        // "1:8,1 to 3,3|obj", "1,1 to 3:8,3|obj", "1:4,1:8 to 3:12,3|obj").
        QCOMPARE(at("1:5,1 TO 2,2"), (std::vector<QPoint>{{5,0},{37,0},{5,32},{37,32}}));
        QCOMPARE(at("1,1:5 to 2,2:6"), (std::vector<QPoint>{{0,5},{32,5},{0,37},{32,37}}));
        QCOMPARE(at("1,1 to 3:8,2:40"), at("1,1 TO 3,2"));
        QCOMPARE(at("1:5, 1 TO 2, 2"), at("1:5,1 TO 2,2"));
        // Written backwards, the grid starts at the lower end moved by both ends' offsets ("3:8,3 to 1,1|obj",
        // "3,1 to 1:8,1|obj"); a percent moves a range like an offset, and its tile part orders the ends and counts the
        // copies ("1,1 to 50%,50%|obj" one copy, "50%,50% to 3,3|obj" three from 4,4 on master-screen-loc-world's 7x7).
        QCOMPARE(at("3:8,3 to 1,1"), at("1:8,1 to 3,3"));
        QCOMPARE(at("3,3 to 1:8,1"), at("1:8,1 to 3,3"));
        QCOMPARE(at("1,1 to 50%,50%",{7,7}), (std::vector<QPoint>{{0,0}}));
        QCOMPARE(at("50%,50% to 3,3",{7,7}), at("4,4 to 6,6",{7,7}));
        // A percent's pixels added to a tile are cut towards zero, from an edge they go left and up (plane-relay-world
        // locpd-mark, locpe, locpf-mark: 19.2 is 19 from 1, 20 from TOP; -38.4 is -38).
        QCOMPARE(at("1+10%,1+10%",{7,7}), (std::vector<QPoint>{{19,19}}));
        QCOMPARE(at("1-20%,1-20%",{7,7}), (std::vector<QPoint>{{-38,-38}}));
        QCOMPARE(at("RIGHT-90%,TOP-90%",{7,7}), (std::vector<QPoint>{{19,20}}));
        for (const auto* invalid : {"", "garbage", "1", "1,1,1", "map:1,1", "1+1+1,1",
                                  "1:16,1:16 to 2:16,2:16 to 3:16,3:16",
                                  "2:16,2:16 to 1.5:16,1:16", "2,2 TO 1.5,1", "NORTH,EAST TO CENTER,CENTER",
                                  "1,1 TO 17,16", "1,1 TO 16384,16384", "1e30,1", "1:999999999999999,1"})
            QVERIFY2(at(invalid).empty(),invalid);
        QVERIFY(!ScreenLoc::parse(QString(257,'1')));
        // Tiles outside the view grow the scene; a pixel offset does not, a larger icon does (stock-parity-world screen-loc-off-view-*).
        QCOMPARE(ScreenLoc::parse("0,6")->outside({18,12}),QMargins(1,0,0,0));
        QCOMPARE(ScreenLoc::parse("18:8,6")->outside({18,12}),QMargins());
        QCOMPARE(ScreenLoc::parse("18,12")->outside({18,12},{2,2}),QMargins(0,1,1,0));
        QCOMPARE(ScreenLoc::parse("RIGHT,TOP")->outside({18,12},{2,2}),QMargins());
        QCOMPARE(at("0,6"),std::vector<QPoint>{});
        QCOMPARE(ScreenLoc::parse("0,6")->positions({18,12},32,QMargins(1,0,0,0)),(std::vector<QPoint>{{-32,160}}));
        QVERIFY(at("EAST,NORTH",{INT_MAX,INT_MAX}).empty());
    }
    void screenMapTargets() {
        using shell::ScreenLoc;
        for (const QString location : {"1:8,1:4", "SOUTH:6, CENTER+5:7", "CENTER:-1,CENTER:+1",
                                       "-1.5+2:7,1", "1:8,1:4 TO 2:8,2:4", "SOUTHWEST", ""}) {
            const auto local = ScreenLoc::target(location);
            QVERIFY(local); QVERIFY(local->map.isEmpty()); QCOMPARE(local->location, location);
            for (const QString name : {"preview", "character_preview_[0x200123]", "main.map"}) {
                const auto named = ScreenLoc::target(name + ":" + location);
                QVERIFY(named); QCOMPARE(named->map, name); QCOMPARE(named->location, location);
                QCOMPARE(bool(ScreenLoc::parse(named->location)), bool(ScreenLoc::parse(location)));
            }
        }
        const auto named = ScreenLoc::target("map:1,1");
        QVERIFY(named); QCOMPARE(named->map, QString("map")); // not an alias for the default MAP
        QVERIFY(!ScreenLoc::target(QString(257,'x')));
        QVERIFY(!ScreenLoc::target(":1,1"));
        const auto unsupported = shell::decodeEntityAppearance(model::appearanceOf(R"({"screenLoc":"preview:unsupported placement","appearanceFlags":268435456})"),32);
        QVERIFY(unsupported.fields.master); QVERIFY(!unsupported.fields.screenPlacement);
        QVERIFY(unsupported.fields.screenTarget);
        QCOMPARE(unsupported.fields.screenTarget->map, QString("preview"));
        const auto reset = shell::decodeEntityAppearance(model::appearanceOf("{}"),32,unsupported.fields);
        QVERIFY(reset.fields.screenTarget); QVERIFY(reset.fields.screenTarget->map.isEmpty());
        QVERIFY(!reset.fields.screenPlacement);
    }
    void screenMapMasterIsolation() {
        shell::test::PlaneSourceFixture f;
        const auto baseline = f.sprites;
        QVERIFY(!baseline.empty());
        // A second MAP reuses the same plane numbers AND target names. Both
        // leaf and grouped masters must be routed before any selection/refusal.
        f.world.hudOrder.insert(f.world.hudOrder.end(),{210,211,212});
        f.world.hudAppid[210]=10; f.world.hudAppid[211]=11; f.world.hudAppid[212]=12;
        for (const QString location : {"preview:1,1", "preview:bogus", "preview:1,1 TO 2,1",
                                      "character_preview_[0x200123]:SOUTHWEST"}) {
            auto fields = QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(f.world.appearances[2]))).object();
            fields["screenLoc"]=location; fields["hasColor"]=true; fields["colorA"]=0;
            f.world.appearances[10]=f.world.appearances[2];
            setFields(f.world.appearances[10], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
            fields["appearanceFlags"]=268435972+0x04000000; fields["overlays"]=QJsonArray{4};
            f.world.appearances[11]=f.world.appearances[10];
            setFields(f.world.appearances[11], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
            f.world.appearances[12]=f.world.appearances[3];
            fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(f.world.appearances[3]))).object();
            fields["screenLoc"]=location;
            setFields(f.world.appearances[12], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
            f.refresh();
            QVERIFY2(samePixelsAndMetadata(f.sprites,baseline),qPrintable(location));
        }
        // Closing/reopening leaves immutable earlier publications and resident
        // assets alone; an idle frame after opening reuses the published scene.
        shell::EntityMap::Stats stats;
        f.refresh(&stats); QCOMPARE(stats.templateBuilds,uint64_t(0));
        QCOMPARE(stats.frameDecodes,uint64_t(0)); QCOMPARE(stats.sceneReuses,uint64_t(1));
        f.world.hudOrder.resize(2); f.refresh();
        QVERIFY(samePixelsAndMetadata(f.sprites,baseline));
        // Scope-only appearance updates invalidate admission in both directions.
        const auto local=fieldsJson(f.world.appearances[2]);
        auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(local)).object();
        fields["screenLoc"]="preview:1,1";
        setFields(f.world.appearances[2], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
        f.refresh();
        for (const auto& sprite : f.sprites) QVERIFY(sprite.kind != shell::MapSprite::Master);
        setFields(f.world.appearances[2], local); f.refresh();
        QVERIFY(samePixelsAndMetadata(f.sprites,baseline));
        // A leaf master acts whatever coordinates it has, including a form this does not place.
        fields["screenLoc"]="1,1 TO 50%,50%";
        setFields(f.world.appearances[2], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
        f.refresh(); QVERIFY(samePixelsAndMetadata(f.sprites,baseline));
        // Text that is not coordinates makes it no master (stock 516.1689, master-screen-loc-world "bogus").
        fields["screenLoc"]="unsupported";
        setFields(f.world.appearances[2], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
        f.refresh();
        for (const auto& sprite : f.sprites) QVERIFY(sprite.kind != shell::MapSprite::Master);
    }
    void namedMapCompositionIsolation() {
        shell::test::PlaneSourceFixture f;
        shell::NamedMap named("preview");
        auto qualify=[&](uint32_t id) {
            auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(f.world.appearances[id]))).object();
            fields["screenLoc"]="preview:1,1";
            setFields(f.world.appearances[id], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
        };
        for (uint32_t id : {1u,2u,3u}) { f.world.appearances[id+10]=f.world.appearances[id]; qualify(id+10); }
        f.world.hudOrder.insert(f.world.hudOrder.end(),{210,211,212});
        f.world.hudAppid[210]=11; f.world.hudAppid[211]=12; f.world.hudAppid[212]=13;
        const auto read=[&](uint32_t id,QByteArray& out) { out=f.resources.at(id); return true; };
        const auto frame=named.prepare(f.world,read,0,17);
        QCOMPARE(frame.sprites.size(),3);
        const auto plan=shell::CompositionPlan::build(frame.sprites,frame.terrain.source,{true});
        QVERIFY(plan->supported); QCOMPARE(plan->masters.size(),size_t(1));
        QCOMPARE(plan->targets.size(),size_t(1)); QVERIFY(!plan->final.empty());
        for (const auto& sprite : frame.sprites) QVERIFY(sprite.atom>=210 && sprite.atom<=212);
        f.refresh();
        for (const auto& sprite : f.sprites) QVERIFY(sprite.atom<210);
    }
    void namedMapEscapedGroup() {
        auto w=world(); w.atoms.clear();
        w.hudOrder={50}; w.hudAppid[50]=2;
        w.appearances[2]=w.appearances[1]; w.appearances[3]=w.appearances[1];
        w.appearances[2].fields.iconState="green";
        setFields(w.appearances[2], R"({"screenLoc":"preview:1,1","hasPlane":true,"plane":60,"appearanceFlags":67108864,"overlays":[3]})");
        setFields(w.appearances[3], R"({"hasPlane":true,"plane":80,"pixelX":16,"appearanceFlags":201326592})");
        shell::NamedMap preview("preview");
        const auto frame=preview.prepare(w,reader(),0,42);
        QCOMPARE(frame.sprites.size(),2);
        QCOMPARE(frame.sprites[0].plane,60); QCOMPARE(frame.sprites[1].plane,80);
        QVERIFY(frame.sprites[0].appearanceGroup); QVERIFY(frame.sprites[1].appearanceGroup);
        QVERIFY(!frame.sprites[1].appearanceGroup->parent);
        QCOMPARE(frame.sprites[1].position,QPoint(16,0));
        // Crossing without KEEP_APART leaves the surface the same way (stock-parity-world kt-overlay-plane).
        setFields(w.appearances[3], R"({"hasPlane":true,"plane":80,"appearanceFlags":67108864})");
        const auto crossed=preview.prepare(w,reader(),0,42);
        QCOMPARE(crossed.sprites.size(),2); QCOMPARE(crossed.sprites[1].plane,80);
        QVERIFY(!crossed.sprites[1].appearanceGroup->parent);
    }
    void namedMapLifecycle() {
        auto w=world();
        w.hudOrder={50,51}; w.hudAppid[50]=2; w.hudAppid[51]=3;
        w.hud[50]={}; w.hud[51]={};
        w.hudIdentity[50]={2,100,18}; w.hudIdentity[51]={2,101,19};
        w.appearances[2]=w.appearances[1]; w.appearances[3]=w.appearances[1];
        setFields(w.appearances[2], R"({"screenLoc":"1,1"})");
        setFields(w.appearances[3], R"({"screenLoc":"preview:1,1"})");
        shell::TurfMap terrain; shell::EntityMap main;
        const auto base=terrain.prepare(w,{},0,false);
        const auto mainBefore=main.prepare(w,base,reader(),0,nullptr,true);
        shell::NamedMap preview("preview"), other("other");
        const auto before=preview.prepare(w,reader(),0,42);
        QCOMPARE(before.terrain.source,QRect(0,0,32,32));
        QVERIFY(before.terrain.geometry.world.isEmpty());
        QCOMPARE(before.sprites.size(),1); QCOMPARE(before.sprites[0].atom,uint32_t(51));
        QCOMPARE(before.sprites[0].position,QPoint(0,0));
        QVERIFY(other.prepare(w,reader(),0,42).sprites.empty());
        auto hit=shell::mapPointerAt({16,16},{32,32},{1,1},before.terrain.source,
            before.terrain.geometry,"popup.preview",false,before.sprites);
        QVERIFY(hit); QCOMPARE(hit->click.screenLocMap,std::string("preview"));
        QVERIFY(shell::mapPointerCurrent(*hit,w,42));
        shell::EntityMap::Stats stats;
        const auto warm=preview.prepare(w,reader(),0,42,&stats);
        QCOMPARE(warm.sprites.constData(),before.sprites.constData());
        QCOMPARE(stats.frameDecodes,uint64_t(0)); QCOMPARE(stats.templateBuilds,uint64_t(0));
        QCOMPARE(warm.terrain.image.cacheKey(),before.terrain.image.cacheKey());
        setFields(w.appearances[3], R"({"screenLoc":"preview:1:4,1"})");
        const auto update=preview.prepare(w,reader(),0,42);
        QCOMPARE(update.sprites[0].position,QPoint(4,0));
        QCOMPARE(before.sprites[0].position,QPoint(0,0)); // immutable old frame
        QVERIFY(samePixelsAndMetadata(main.prepare(w,base,reader(),0,nullptr,true),mainBefore));
        setFields(w.appearances[3], R"({"screenLoc":"preview:2,1"})");
        const auto expanded=preview.prepare(w,reader(),0,42);
        QCOMPARE(expanded.terrain.source,QRect(0,0,64,32));
        QCOMPARE(expanded.sprites[0].position,QPoint(32,0));
        setFields(w.appearances[3], R"({"screenLoc":"other:1,1"})");
        QVERIFY(!shell::mapPointerCurrent(*hit,w,42)); // key/membership unchanged
        QVERIFY(preview.prepare(w,reader(),0,42).sprites.empty());
        QCOMPARE(other.prepare(w,reader(),0,42).sprites.size(),1);
        w.hudOrder.pop_back(); w.hudAppid.erase(51);
        QVERIFY(other.prepare(w,reader(),0,42).sprites.empty());
        QVERIFY(samePixelsAndMetadata(main.prepare(w,base,reader(),0,nullptr,true),mainBefore));
    }
    void screenMapOrdinaryRouting() {
        auto w=world();
        w.hudOrder={50}; w.hudAppid[50]=2; w.hudIdentity[50]={2,100,18};
        w.appearances[2]=w.appearances[1];
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto frame=terrain.prepare(w,{},0,false);
        auto draw=[&](QString location) {
            QJsonObject fields{{"screenLoc",location}};
            setFields(w.appearances[2], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
            return scene.prepare(w,frame,reader(),0,nullptr,true);
        };
        const auto baseline=draw("1:8,1:4"); QCOMPARE(baseline.size(),2);
        // Stock 516.1688: explicit default-control names are NOT default-map aliases.
        QCOMPARE(draw("map:1:8,1:4").size(),1);
        QCOMPARE(draw("main.map:1:8,1:4").size(),1);
        const auto other=draw("preview:1:8,1:4"); QCOMPARE(other.size(),1);
        QCOMPARE(other[0].kind,shell::MapSprite::Entity);
        QVERIFY(samePixelsAndMetadata(draw("1:8,1:4"),baseline));
        // A world occurrence ignores screen_loc; map routing is HUD-only.
        setFields(w.appearances[1], R"({"screenLoc":"preview:1,1"})");
        QVERIFY(samePixelsAndMetadata(draw("1:8,1:4"),baseline));
        // HUD-attached images follow their owner's map, not their own (empty)
        // screen_loc. No image or picking candidate may leak from a foreign HUD.
        w.hud[50]={};
        w.appearances[3]=w.appearances[1]; setFields(w.appearances[3], "{}");
        w.imageInstances[7]={3,50,1,false,20,18}; w.imageInstances[7].targetHud=true;
        const auto attached=draw("1:8,1:4"); QCOMPARE(attached.size(),3);
        QCOMPARE(draw("preview:1:8,1:4").size(),1);
        shell::NamedMap preview("preview");
        const auto named=preview.prepare(w,reader(),0,42);
        QCOMPARE(named.sprites.size(),2);
        for (const auto& sprite : named.sprites) QCOMPARE(sprite.atom,uint32_t(50));
        QVERIFY(named.sprites.back().imageIdentity);
        QVERIFY(samePixelsAndMetadata(draw("1:8,1:4"),attached));
        QVERIFY(preview.prepare(w,reader(),0,42).sprites.empty());
    }
    void screenLocationWhitespace_data() {
        QTest::addColumn<QString>("spaced"); QTest::addColumn<QString>("canonical");
        const QStringList locations = {
            "1,1", "2:3,2:-5", "1.5+1:7,2.125-0.5:-8", "CENTER",
            "CENTER+5:7,SOUTH:22", "SOUTH:22,CENTER+5:7", "NORTH-1:2,EAST-2:-3",
            "TOP:-70,CENTER:-54", "BOTTOM+1:7,LEFT+2:-9", "RIGHT-1:7,CENTER:+13",
            "SOUTHWEST", "NORTHEAST", "TOPLEFT", "BOTTOMRIGHT",
            "1,1 TO 3,2", "3,2 to 1,1", "WEST,SOUTH TO EAST,NORTH",
            "SOUTH,WEST TO NORTH,EAST", "4:16,2:16 TO 10:16,2:16",
            "SOUTHWEST TO NORTHEAST", "BOTTOMLEFT TO TOPRIGHT"
        };
        const QStringList whitespace = {" ", "  ", "\t", " \r\n\t"};
        for (qsizetype i = 0; i < locations.size(); ++i) {
            for (qsizetype j = 0; j < whitespace.size(); ++j) {
                const auto& ws = whitespace[j];
                auto spaced = locations[i];
                spaced.replace(",", ws + "," + ws);
                spaced.replace(" TO ", ws + " TO " + ws, Qt::CaseInsensitive);
                spaced = ws + spaced + ws;
                QTest::newRow(qPrintable(QString("location-%1-whitespace-%2").arg(i).arg(j)))
                    << spaced << locations[i];
            }
        }
    }
    void screenLocationWhitespace() {
        QFETCH(QString, spaced); QFETCH(QString, canonical);
        const auto reference = shell::ScreenLoc::parse(canonical);
        const auto parsed = shell::ScreenLoc::parse(spaced);
        QVERIFY(reference); QVERIFY2(parsed.has_value(), qPrintable(spaced));
        const auto expected = reference->positions({15,15},32);
        QVERIFY(!expected.empty());
        QCOMPARE(parsed->positions({15,15},32), expected);
        QCOMPARE(parsed->range, reference->range);
        QCOMPARE(parsed->first.x.anchor, reference->first.x.anchor);
        QCOMPARE(parsed->first.y.anchor, reference->first.y.anchor);
        QCOMPARE(parsed->last.x.anchor, reference->last.x.anchor);
        QCOMPARE(parsed->last.y.anchor, reference->last.y.anchor);
    }
    void screenLocationWhitespaceLimits() {
        // Trim token boundaries, not arbitrary internal whitespace or malformed axes.
        for (const auto* text : {" , ", "1, ", " ,1", "1, ,2", "CEN TER,1",
                                "1 2,1", "CENTER +5,1", "1: 7,1", "1,1 TO ",
                                "CENTER, RIGHT"})
            QVERIFY2(!shell::ScreenLoc::parse(text), text);
        QVERIFY(!shell::ScreenLoc::parse(QString(257,' ') + "1,1"));
    }
    void screenHudboxWhitespacePlacement() {
        auto w = world(); w.atoms.clear();
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        auto map = terrain.prepare(w,{},0,false); map.source.setSize({480,480});
        // TG's seven hudboxes share this formatter, including the space after ','.
        // Synthetic icons isolate placement from game assets, planes and preferences.
        for (uint32_t i = 0; i < 7; ++i) {
            w.hudOrder.push_back(42+i); w.hudAppid[42+i] = 10+i;
            w.hudIdentity[42+i] = {2,uint16_t(99+i),17+i};
            w.appearances[10+i] = w.appearances[1];
            setFields(w.appearances[10+i], QString("{\"screenLoc\":\"SOUTH:%1, CENTER+5:%2\"}")
                .arg(6+(i/3)*16).arg(7+(i%3)*15).toStdString());
        }
        const auto spaced = scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(spaced.size(),7);
        for (int i = 0; i < 7; ++i) {
            QCOMPARE(spaced[i].position, map.source.topLeft()+QPoint(384+7+(i%3)*15,448-6-(i/3)*16));
            QVERIFY(spaced[i].screenInteractive); QCOMPARE(spaced[i].key,uint16_t(99+i));
            QCOMPARE(spaced[i].targetIdentity,uint64_t(17+i));
        }
        QCOMPARE(scene.prepare(w,map,reader(),0,&stats,true).constData(),spaced.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.frameDecodes,uint64_t(0));
        QCOMPARE(stats.sceneReuses,uint64_t(1));
        for (uint32_t i = 0; i < 7; ++i)
            setFields(w.appearances[10+i], QString("{\"screenLoc\":\"SOUTH:%1,CENTER+5:%2\"}")
                .arg(6+(i/3)*16).arg(7+(i%3)*15).toStdString());
        const auto compact = scene.prepare(w,map,reader(),0,nullptr,true);
        QVERIFY(samePixelsAndMetadata(spaced,compact));
        QCOMPARE(spaced[0].position,map.source.topLeft()+QPoint(391,442));
    }
    void screenLocationTopAlias() {
        // Stock 516.1688: paired native masks in screen-loc-top-world are identical.
        const std::array<std::pair<const char*, const char*>,5> pairs{{
            {"TOP,CENTER", "NORTH,CENTER"},
            {"TOP:-70,CENTER:-54", "NORTH:-70,CENTER:-54"},
            {"CENTER:-61,TOP", "CENTER:-61,NORTH"},
            {"TOP-1:-8,CENTER:+29", "NORTH-1:-8,CENTER:+29"},
            {"WEST,TOP TO EAST,TOP", "WEST,NORTH TO EAST,NORTH"}}};
        for (const auto& [alias, canonical] : pairs) {
            const auto top = shell::ScreenLoc::parse(alias);
            const auto north = shell::ScreenLoc::parse(canonical);
            QVERIFY2(top.has_value(),alias); QVERIFY(north.has_value());
            QCOMPARE(top->positions({7,5},32),north->positions({7,5},32));
        }
        const auto offset = shell::ScreenLoc::parse("TOP:-70,CENTER:-54");
        QVERIFY(offset.has_value());
        QCOMPARE(offset->positions({7,5},32),(std::vector<QPoint>{{42,58}}));
        for (const auto* invalid : {"TOP", "TOPLEFT,CENTER", "TOP,TOP",
                                  "TOP+1,CENTER", "TOP+1+1,CENTER", "TOP:1048577,CENTER",
                                  "WEST,TOP TO EAST,TOP:-1"}) {
            const auto loc = shell::ScreenLoc::parse(invalid);
            QVERIFY2(!loc || loc->positions({7,5},32).empty(),invalid);
        }
    }
    void screenTopPlacementAndReuse() {
        auto w=world(); w.atoms.clear();
        w.hudOrder={42}; w.hudAppid[42]=1; w.hudIdentity[42]={2,99,17};
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        auto map=terrain.prepare(w,{},0,false); map.source.setSize({224,160});
        setFields(w.appearances[1], R"({"screenLoc":"TOP:-70,CENTER:-54"})");
        const auto top=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(top.size(),1); QCOMPARE(top[0].position,map.source.topLeft()+QPoint(42,70));
        QVERIFY(top[0].screenInteractive); QCOMPARE(top[0].key,uint16_t(99));
        QCOMPARE(top[0].targetIdentity,uint64_t(17));
        QCOMPARE(scene.prepare(w,map,reader(),0,&stats,true).constData(),top.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.frameDecodes,uint64_t(0));
        QCOMPARE(stats.sceneReuses,uint64_t(1));
        setFields(w.appearances[1], R"({"screenLoc":"NORTH:-70,CENTER:-54"})");
        const auto north=scene.prepare(w,map,reader(),0,nullptr,true);
        QVERIFY(samePixelsAndMetadata(top,north));
        setFields(w.appearances[1], R"({"screenLoc":"TOP,CENTER"})");
        const auto moved=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(moved.size(),1); QCOMPARE(moved[0].position,map.source.topLeft()+QPoint(96,0));
        QCOMPARE(top[0].position,map.source.topLeft()+QPoint(42,70)); // immutable publication
        setFields(w.appearances[1], R"({"screenLoc":"TOP,CENTER","renderSource":"*capture"})");
        // A relay of a plane stands there too (stock 516.1689, plane-relay-world locleft, locright).
        const auto relay=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(relay.size(),1); QCOMPARE(relay[0].iconBottom,moved[0].iconBottom);
    }
    void screenLocationEdgeAliases_data() {
        QTest::addColumn<QString>("alias"); QTest::addColumn<QString>("canonical");
        // Exact paired stock masks: screen-loc-alias-world (ordinary + range probes).
        const std::pair<const char*, const char*> pairs[] = {
            {"CENTER,BOTTOM", "CENTER,SOUTH"}, {"BOTTOM,CENTER", "CENTER,SOUTH"},
            {"BOTTOM+1:7,CENTER:+13", "CENTER:+13,SOUTH+1:7"},
            {"LEFT,CENTER", "WEST,CENTER"}, {"LEFT+1:7,CENTER:-9", "WEST+1:7,CENTER:-9"},
            {"RIGHT,CENTER", "EAST,CENTER"}, {"RIGHT-1:-7,CENTER:+9", "EAST-1:-7,CENTER:+9"},
            {"TOP,LEFT", "NORTHWEST"}, {"RIGHT,TOP", "NORTHEAST"},
            {"BOTTOM,LEFT", "SOUTHWEST"}, {"RIGHT,BOTTOM", "SOUTHEAST"},
            {"TOPLEFT", "NORTHWEST"}, {"TOPRIGHT", "NORTHEAST"},
            {"BOTTOMLEFT", "SOUTHWEST"}, {"BOTTOMRIGHT", "SOUTHEAST"},
            {"LEFT,SOUTH TO EAST,SOUTH", "WEST,SOUTH TO EAST,SOUTH"},
            {"WEST,BOTTOM TO EAST,SOUTH", "WEST,SOUTH TO EAST,SOUTH"},
            {"WEST,SOUTH TO EAST,BOTTOM", "WEST,SOUTH TO EAST,SOUTH"},
            {"WEST,BOTTOM TO EAST,BOTTOM", "WEST,SOUTH TO EAST,SOUTH"},
            {"LEFT,SOUTH TO WEST,NORTH", "WEST,SOUTH TO WEST,NORTH"},
            {"WEST,SOUTH TO LEFT,NORTH", "WEST,SOUTH TO WEST,NORTH"},
            {"WEST,NORTH TO EAST,BOTTOM", "WEST,NORTH TO EAST,SOUTH"},
            {"EAST,SOUTH TO LEFT,NORTH", "EAST,SOUTH TO WEST,NORTH"},
            {"BOTTOMLEFT TO NORTHEAST", "WEST,SOUTH TO EAST,NORTH"},
            {"WEST,TOP TO EAST,NORTH", "WEST,NORTH TO EAST,NORTH"},
            {"WEST,NORTH TO EAST,TOP", "WEST,NORTH TO EAST,NORTH"},
            {"TOPLEFT TO NORTHEAST", "WEST,NORTH TO EAST,NORTH"},
            {"LEFT:7,BOTTOM:5 to EAST-1:7,SOUTH+1:5", "WEST:7,SOUTH:5 TO EAST-1:7,SOUTH+1:5"},
            {"LEFT+1:7,BOTTOM+1:5 TO EAST-1:7,NORTH-1:5", "WEST+1:7,SOUTH+1:5 TO EAST-1:7,NORTH-1:5"}};
        for (const auto& [alias, canonical] : pairs)
            QTest::newRow(alias) << QString(alias) << QString(canonical);
    }
    void screenLocationEdgeAliases() {
        QFETCH(QString, alias); QFETCH(QString, canonical);
        const auto loc=shell::ScreenLoc::parse(alias), reference=shell::ScreenLoc::parse(canonical);
        QVERIFY(loc.has_value()); QVERIFY(reference.has_value());
        const auto positions=reference->positions({7,5},32); QVERIFY(!positions.empty());
        const auto visiblePositions=[](const std::vector<QPoint>& input) {
            std::vector<QPoint> output;
            for (const auto p:input) if (QRect(0,0,224,160).intersects(QRect(p,QSize(32,32)))) output.push_back(p);
            return output;
        };
        QCOMPARE(visiblePositions(loc->positions({7,5},32)),visiblePositions(positions));
        auto w=world(); w.atoms.clear();
        w.hudOrder={42}; w.hudAppid[42]=1; w.hudIdentity[42]={2,99,17};
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        auto map=terrain.prepare(w,{},0,false); map.source.setSize({224,160});
        auto fields=[](const QString& text) { return ("{\"screenLoc\":\""+text+"\"}").toStdString(); };
        setFields(w.appearances[1], fields(canonical));
        const auto before=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(before.size(),qsizetype(positions.size()));
        setFields(w.appearances[1], fields(alias));
        const auto after=scene.prepare(w,map,reader(),0,nullptr,true);
        const auto visibleSprites=[&](const shell::MapSprites& input) {
            shell::MapSprites output;
            for (const auto& sprite:input) if (sprite.rect().intersects(map.source)) output.push_back(sprite);
            return output;
        };
        QVERIFY(samePixelsAndMetadata(visibleSprites(before),visibleSprites(after)));
        QVERIFY(after[0].screenInteractive); QCOMPARE(after[0].targetIdentity,uint64_t(17));
        QCOMPARE(scene.prepare(w,map,reader(),0,&stats,true).constData(),after.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.frameDecodes,uint64_t(0));
        QCOMPARE(stats.sceneReuses,uint64_t(1));
        // A relay of a plane stands where the canonical spelling puts it, at every position of a range (stock 516.1689,
        // plane-relay-world locleft, locright, locrange).
        const auto relays=[&](const QString& text) {
            setFields(w.appearances[1], ("{\"screenLoc\":\""+text+"\",\"renderSource\":\"*capture\"}").toStdString());
            std::vector<QPoint> bottoms;
            for (const auto& sprite:scene.prepare(w,map,reader(),0,nullptr,true))
                if (QRect(map.source.left(),map.source.top()+1,map.source.width(),map.source.height()).contains(sprite.iconBottom))
                    bottoms.push_back(sprite.iconBottom);
            std::sort(bottoms.begin(),bottoms.end(),[](QPoint a, QPoint b) { return std::pair(a.x(),a.y())<std::pair(b.x(),b.y()); });
            return bottoms;
        };
        const auto relayed=relays(alias);
        QCOMPARE(relayed.size(),visiblePositions(positions).size()); QCOMPARE(relayed,relays(canonical));
        QVERIFY(samePixelsAndMetadata(visibleSprites(before),visibleSprites(after))); // immutable publications
    }
    void screenRangeAnchoredPublication() {
        auto w=world(); w.atoms.clear();
        w.hudOrder={42}; w.hudAppid[42]=1; w.hudIdentity[42]={2,99,17};
        setFields(w.appearances[1], R"({"screenLoc":"RIGHT,SOUTH TO EAST,NORTH"})");
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        auto map=terrain.prepare(w,{},0,false); map.source.setSize({224,160});
        const auto first=scene.prepare(w,map,reader(),0,nullptr,true);
        const auto placement=shell::ScreenLoc::parse("RIGHT,SOUTH TO EAST,NORTH");
        QVERIFY(placement.has_value()); QCOMPARE(placement->positions({7,5},32).size(),size_t(35));
        QCOMPARE(first.size(),5); // worker publication already culls off-view copies; range budget includes all 35
        int visible=0;
        for (const auto& sprite:first) {
            QVERIFY(sprite.screenInteractive); QCOMPARE(sprite.targetIdentity,uint64_t(17));
            if (sprite.rect().intersects(map.source)) {
                QCOMPARE(sprite.position.x(),map.source.left()+192); ++visible;
            }
        }
        QCOMPARE(visible,5); // stock right column, NOT a full-view rectangle
        QCOMPARE(scene.prepare(w,map,reader(),0,&stats,true).constData(),first.constData());
        QCOMPARE(stats.sceneReuses,uint64_t(1)); QCOMPARE(stats.templateBuilds,uint64_t(0));
        QCOMPARE(stats.frameDecodes,uint64_t(0));
        setFields(w.appearances[1], R"({"screenLoc":"BOTTOMLEFT TO TOPRIGHT"})");
        const auto changed=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(changed.size(),1); QCOMPARE(changed[0].position,map.source.topLeft()+QPoint(0,128));
        QCOMPARE(first[0].position,map.source.topLeft()+QPoint(192,128)); // immutable earlier publication
        setFields(w.appearances[1], R"({"screenLoc":"RIGHT,SOUTH TO EAST,NORTH"})");
        map.source.setSize({17*32,16*32});
        QVERIFY(scene.prepare(w,map,reader(),0,nullptr,true).empty()); // 272 copies, no clipped-prefix admission
    }
    void screenRangeStockCorpus_data() {
        screenRangeDataReady_ = false;
        QTest::addColumn<QString>("text"); QTest::addColumn<QSize>("view");
        QTest::addColumn<QList<QPoint>>("observed");
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/screen-loc-range-world/stock-positions.json";
        QJsonDocument doc; QString error;
        QVERIFY2(testinput::readRecordedJson(path, doc, error), qPrintable(error));
        QVERIFY2(doc.isArray(), qPrintable(path + ": expected an array of screen-range observations"));
        const auto cases = doc.array();
        QVERIFY2(cases.size() == 1904, qPrintable(path + ": expected 1904 retained screen-range observations"));
        const QSet<QString> exclusions{"RIGHT+1:-192,SOUTH TO WEST:-192,SOUTH", "WEST,TOP+1:-128 TO WEST,SOUTH:-128"};
        std::map<std::tuple<QString, QString, QString>, int> occurrences;
        int admitted = 0;
        for (qsizetype i = 0; i < cases.size(); ++i) {
            const auto c = cases[i].toObject();
            const QString context = path + QString(": row %1: ").arg(i + 1);
            QVERIFY2(cases[i].isObject(), qPrintable(context + "expected an observation object"));
            const QString text = c["text"].toString(), view = c["view"].toString(), group = c["group"].toString();
            QVERIFY2(!text.isEmpty() && !group.isEmpty(), qPrintable(context + "text and group must be nonempty strings"));
            QVERIFY2(view == "7x5" || view == "8x6", qPrintable(context + "view must be the retained 7x5 or 8x6 size"));
            // These two generators each retained the same zero-offset control twice.
            const bool repeatedControl = text == "WEST,SOUTH TO WEST,SOUTH" && view == "7x5" &&
                                         (group == "tile-offsets" || group == "endpoint-modes");
            const int count = ++occurrences[{group, view, text}];
            QVERIFY2(count <= (repeatedControl ? 2 : 1), qPrintable(context + "unexpected duplicate group/view/text case"));
            const bool excluded = group == "large-pixels" && view == "7x5" && exclusions.contains(text);
            QVERIFY2(c.contains("excluded") == excluded, qPrintable(context + "only the two retained reversed-pixel cases are excluded"));
            if (excluded) {
                QVERIFY2(c["excluded"].toString() == "reversed pixel-offset range", qPrintable(context + "missing exclusion reason"));
                continue;
            }
            QVERIFY2(c["positions"].isArray(), qPrintable(context + "positions must be an explicit array; an empty observation is valid"));
            const QSize size = view == "7x5" ? QSize(7, 5) : QSize(8, 6);
            for (const auto& position : c["positions"].toArray()) {
                QVERIFY2(testinput::isIntegerTuple(position, 2, -12, 236), qPrintable(context + "position must contain two integer marker origins"));
                const auto xy = position.toArray();
                QVERIFY2(QRect(QPoint(), size * 32).contains(QRect(QPoint(xy[0].toInt() + 12, xy[1].toInt() + 12), QSize(8, 8))),
                         qPrintable(context + "retained marker must be fully inside the view"));
            }
            ++admitted;
        }
        QVERIFY2(occurrences.size() == 1902, qPrintable(path + ": expected 1902 distinct group/view/text cases and two repeated controls"));
        QCOMPARE(admitted,1902);
        for (qsizetype i=0;i<cases.size();++i) {
            const auto c=cases[i].toObject();
            if (c.contains("excluded")) continue; // two retained reversed-pixel observations, NOT parity passes
            const auto dims=c["view"].toString().split('x');
            QList<QPoint> positions;
            for (const auto p:c["positions"].toArray()) {
                const auto xy=p.toArray(); positions.append({xy[0].toInt(),xy[1].toInt()});
            }
            QTest::newRow(qPrintable(QString::number(i+1))) << c["text"].toString()
                << QSize(dims[0].toInt(),dims[1].toInt()) << positions;
        }
        screenRangeDataReady_ = true;
    }
    void screenRangeStockCorpus() {
        QVERIFY2(screenRangeDataReady_, "screen-loc-range-world/stock-positions.json data validation did not complete");
        QFETCH(QString,text); QFETCH(QSize,view); QFETCH(QList<QPoint>,observed);
        const auto loc=shell::ScreenLoc::parse(text); QVERIFY2(loc.has_value(),qPrintable(text));
        QList<QPoint> visible;
        for (const auto p:loc->positions(view,32)) {
            // Native oracle uses a centered 8x8 marker in a transparent 32x32 icon.
            // These cases have fully visible or fully clipped markers, never partial ones.
            if (QRect(QPoint(),view*32).contains(QRect(p+QPoint(12,12),QSize(8,8)))) visible.append(p);
        }
        std::sort(visible.begin(),visible.end(),[](QPoint a,QPoint b) {
            return std::pair(a.x(),a.y())<std::pair(b.x(),b.y());
        });
        QCOMPARE(visible,observed);
    }
    void screenLocationAliasLimits_data() {
        QTest::addColumn<QString>("text");
        // Endpoint modes now reproduce upper-edge ranges; other grammar and
        // resource/expansion limits remain, including known reversed-pixel gaps.
        for (const auto* text : {"RIGHT+1:-192,SOUTH TO WEST:-192,SOUTH",
                "WEST,TOP+1:-128 TO WEST,SOUTH:-128", "RIGHT,SOUTH TO EAST,NORTH+1",
                "CENTER,RIGHT", "CENTER,LEFT", "CENTER,WEST", "CENTER,EAST", "2,RIGHT", "2,LEFT",
                "LEFTTOP", "RIGHTTOP", "LEFTBOTTOM", "RIGHTBOTTOM",
                "BOTTOM", "LEFT", "RIGHT", "TOP_LEFT",
                "LEFT,LEFT", "BOTTOM,BOTTOM", "RIGHT+1,CENTER", "LEFT-1,CENTER",
                "LEFT:1048577,BOTTOM", "LEFT:7,BOTTOM:5 TO EAST:8,NORTH:5",
                "LEFT:7,NORTH:5 TO EAST:7,BOTTOM:5", "LEFT,BOTTOM TO 17,16"})
            QTest::newRow(text) << QString(text);
    }
    void screenLocationAliasLimits() {
        QFETCH(QString, text);
        const auto loc=shell::ScreenLoc::parse(text);
        QVERIFY(!loc || loc->positions({16,16},32).empty());
    }
    void worldRootMouseTransparency() {
        auto w=world(); w.atoms[42].renderIdentity=7;
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map=terrain.prepare(w,{},0,false);
        const auto first=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(first.size(),1); QVERIFY(!first[0].mouseTransparent); QCOMPARE(first[0].targetIdentity,uint64_t(7));
        // Stock world fixture: opacity 0 -> 0x3004, opacity 1 -> 0x0004.
        setFields(w.appearances[1], R"({"appearanceFlags":12292})");
        const auto transparent=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(transparent.size(),1); QVERIFY(transparent[0].mouseTransparent);
        QVERIFY(!first[0].mouseTransparent); // immutable earlier frame
        setFields(w.appearances[1], R"({"appearanceFlags":4})");
        const auto opaque=scene.prepare(w,map,reader(),0,nullptr,true);
        QVERIFY(!opaque[0].mouseTransparent);
        w.atoms[42].renderIdentity=8;
        const auto reused=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(reused[0].targetIdentity,uint64_t(8)); QCOMPARE(opaque[0].targetIdentity,uint64_t(7));
    }
    void screenMembershipIdentity() {
        model::Session session; session.worldMut()=world(); auto& w=session.worldMut();
        auto screen=[&](uint8_t op,uint8_t ref,uint8_t ver,int64_t time){
            session.setIngestClockNs(time);
            const std::array<uint8_t,14> bytes{op,14,0,2,ref,0,0,0,ver,0,1,0,0,0}; session.ingest(0x76,bytes);
        };
        screen(2,9,7,100);
        const auto membership = w.hudIdentity.at(9).membership; QVERIFY(membership);
        screen(2,8,8,110); screen(2,9,7,120); screen(3,9,7,130);
        QCOMPARE(w.hudIdentity.at(9).membership,membership);
        QCOMPARE(w.hudOrder,(std::vector<uint32_t>{9,8})); QCOMPARE(w.hudIconStart.at(9).ns,int64_t(100));
        const std::array<uint8_t,9> remove{1,9,0,0,0,0,0,0,0};session.ingest(0x76,remove);
        QCOMPARE(w.hudOrder,(std::vector<uint32_t>{8}));
        QVERIFY(!w.hudObjectCurrent(9,7));
        screen(2,9,7,140); QCOMPARE(w.hudOrder,(std::vector<uint32_t>{8,9}));
        QCOMPARE(w.hudIconStart.at(9).ns,int64_t(100));
        QVERIFY(w.hudObjectCurrent(9,7));
        QVERIFY(!w.hudObjectCurrent(9,7,membership)); // close/reopen is a new input membership, not a playback restart
        screen(3,9,9,150); QCOMPARE(w.hudIconStart.at(9).ns,int64_t(150));
        QCOMPARE(w.hudIdentity.at(9).key,uint16_t(9));
        QVERIFY(!w.hudObjectCurrent(9,7));QVERIFY(w.hudObjectCurrent(9,9));
        QCOMPARE(session.ingestErrors(),0);
    }
    void screenPlacementIdentityAndReuse() {
        auto w=world(); w.atoms.clear();
        setFields(w.appearances[1], R"({"screenLoc":"EAST-1:3,NORTH:5","hasPlane":true,"plane":5})");
        w.hudOrder={9,8};w.hudAppid={{9,1},{8,1}};w.hudIdentity={{9,{2,17}},{8,{2,18}}};
        shell::TurfMap terrain;shell::EntityMap scene;shell::EntityMap::Stats stats;
        auto map=terrain.prepare(w,{},0,false);
        const auto first=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(first.size(),2); QCOMPARE(first[0].atom,9u);QCOMPARE(first[1].atom,8u);
        QCOMPARE(first[0].position,map.source.topLeft()+QPoint(35,-5));
        QVERIFY(first[0].screenInteractive);QCOMPARE(first[0].key,uint16_t(17));
        QCOMPARE(scene.prepare(w,map,reader(),0,&stats,true).constData(),first.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0));QCOMPARE(stats.sceneReuses,uint64_t(1));
        // A sub-tile camera scroll, or a terrain recenter, cannot move a HUD pixel in the view.
        map.source.translate(7,9);
        auto moved=scene.prepare(w,map,reader(),0,&stats,true);
        QCOMPARE(moved[0].position-map.source.topLeft(),first[0].position-(map.source.topLeft()-QPoint(7,9)));
        QCOMPARE(stats.templateBuilds,uint64_t(0));QCOMPARE(stats.sortedSprites,uint64_t(0));
        map.geometry.region.translate(2,-3);
        moved=scene.prepare(w,map,reader(),0,&stats,true);QCOMPARE(moved[0].position,map.source.topLeft()+QPoint(35,-5));
        map.source.setWidth(160);map.source.setHeight(128);
        moved=scene.prepare(w,map,reader(),0,&stats,true);QCOMPARE(moved[0].position,map.source.topLeft()+QPoint(99,-5));
        QCOMPARE(stats.templateBuilds,uint64_t(0));
        std::swap(w.hudOrder[0],w.hudOrder[1]);
        moved=scene.prepare(w,map,reader(),0,nullptr,true);QCOMPARE(moved[0].atom,8u);QCOMPARE(moved[1].atom,9u);
        w.hudIdentity[9].key=19;
        moved=scene.prepare(w,map,reader(),0,nullptr,true);QCOMPARE(moved[1].key,uint16_t(19));
        QCOMPARE(first[0].key,uint16_t(17));
        w.hudIdentity[9].kind=3;
        QCOMPARE(scene.prepare(w,map,reader(),0,nullptr,true).size(),1);
        w.hudOrder.clear();QVERIFY(scene.prepare(w,map,reader(),0,nullptr,true).isEmpty());
    }
    void planeSourceAppearances() {
        shell::test::PlaneSourceFixture f;
        const QJsonObject cases[] = {
            {}, {{"appearanceFlags",0x04000204}}, {{"appearanceFlags",0x08000204}},
            {{"vars",QJsonArray{QJsonObject{{"bit",8192},{"data","2000"}}}}},
            {{"filters",shell::test::PlaneSourceFixture::blur("00000000")}},
            {{"maptext","<div>R</div>"}}, {{"overlays",QJsonArray{4}}}, {{"underlays",QJsonArray{4}}},
            {{"filters",shell::test::PlaneSourceFixture::blur("0000803f")}},
            {{"filters",shell::test::PlaneSourceFixture::blur("0000803f")},{"overlays",QJsonArray{4}}},
            {{"filters",shell::test::PlaneSourceFixture::blur("0000803f")},{"underlays",QJsonArray{4}}}
        };
        for (const auto& fields : cases) {
            f.relay(fields);
            const bool children=fields.contains("overlays") || fields.contains("underlays");
            QCOMPARE(f.sprites.size(),children ? 4 : 3);
            const auto hit=f.at({69,37});
            QCOMPARE(hit.click.over.ref,201u); QVERIFY(hit.relayHit); QVERIFY(hit.relayHit->plane);
            QCOMPARE(hit.click.iconX,70); QCOMPARE(hit.click.iconY,59);
            QVERIFY(shell::mapPointerCurrent(hit,f.world,17));
            const auto hole=f.at({79,47});
            QCOMPARE(hole.click.over.kind,uint8_t(children ? proto::enc::RefObj : proto::enc::RefTurf));
            if (children) { QCOMPARE(hole.click.over.ref,201u); QCOMPARE(hole.click.iconX,80); QCOMPARE(hole.click.iconY,49); }
            for (const auto& s:std::as_const(f.sprites)) {
                QVERIFY(!s.maptext); // source replaces own text; no raster approximation
                if (fields.contains("filters") && s.atom==201u) {
                    QVERIFY(s.planeEffects && s.planeEffects->relayFilters());
                    QCOMPARE(s.planeEffects->ignoredFilters,size_t(0));
                    QCOMPARE(s.planeEffects->filters.size(),size_t(1));
                    QCOMPARE(s.planeSourceDependency,QString("*relay"));
                }
            }
            const auto retained=f.sprites;
            shell::EntityMap::Stats stats; f.refresh(&stats);
            QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.frameDecodes,uint64_t(0));
            QCOMPARE(f.sprites.constData(),retained.constData());
        }
        // Ordinary flat siblings reuse traversal; this is not a one-child flag whitelist.
        f.relay({{"overlays",QJsonArray{4,4}}}); QCOMPARE(f.sprites.size(),5);
        const QJsonObject refused[] = {
            {{"filters",QJsonArray{QJsonObject{{"type",99},{"flags",1}}}}},
            {{"filters",QJsonArray{QJsonObject{{"type",99},{"flags",1}},shell::test::PlaneSourceFixture::blur("0000803f")[0]}},{"overlays",QJsonArray{4}}},
            {{"filters",QJsonArray{shell::test::PlaneSourceFixture::blur("0000803f")[0],QJsonObject{{"type",99},{"flags",1}}}}},
            {{"filters",QJsonArray{shell::test::PlaneSourceFixture::blur("0000803f")[0],shell::test::PlaneSourceFixture::blur("01008040")[0]}}}, // chain blur >4
            {{"filters",shell::test::PlaneSourceFixture::blur("000080bf")}}, // negative
            {{"filters",shell::test::PlaneSourceFixture::blur("0000807f")}}, // infinity
            {{"filters",shell::test::PlaneSourceFixture::blur("0000")}},
            {{"hasTransform",true}},
            {{"appearanceFlags",0x0c000204}},
            {{"vars",QJsonArray{QJsonObject{{"bit",8192},{"data","0100"}}}}}
        };
        // The relay's own pixel offset moves neither its picture nor its clicks (stock 516.1689, plane-relay-world rpx).
        f.relay({{"pixelX",8},{"pixelY",3}});
        const auto moved=f.at({69,37});
        QCOMPARE(moved.click.over.ref,201u); QCOMPARE(moved.click.iconX,70); QCOMPARE(moved.click.iconY,59);
        for (const auto& fields:refused) {
            f.relay(fields);
            for(const auto& s:f.sprites) QVERIFY(s.atom!=201u);
        }
        setFields(f.world.appearances[4], R"({"overlays":[1]})");
        f.relay({{"overlays",QJsonArray{4}}});
        for(const auto& s:f.sprites) QVERIFY(s.atom!=201u);
    }
    void planeSourceGroupedReplacement() {
        using Fixture = shell::test::PlaneSourceFixture;
        Fixture f;
        const QJsonObject grouped{{"appearanceFlags",0x04000204},{"overlays",QJsonArray{4}}};
        auto noRelay=[&] { return std::none_of(f.sprites.begin(),f.sprites.end(),[](const auto& s){return s.atom==201u;}); };
        for (const char* blur : {"", "00000000", "0000803f"}) for (const char* list : {"overlays", "underlays"}) {
            QJsonObject fields{{"appearanceFlags",0x04000204},{list,QJsonArray{4,4}}};
            if (*blur) fields["filters"]=Fixture::blur(blur);
            f.relay(fields);
            QCOMPARE(f.sprites.size(),3); // source, master, one consumer; no child draw/hit parts
            QCOMPARE(f.at({69,37}).click.over.ref,201u);
            QCOMPARE(f.at({79,47}).click.over.kind,uint8_t(proto::enc::RefTurf));
            QCOMPARE(f.at({65,40}).click.over.kind,uint8_t(proto::enc::RefTurf));
            const auto retained=f.sprites;
            shell::EntityMap::Stats stats; f.refresh(&stats);
            QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.frameDecodes,uint64_t(0));
            QCOMPARE(f.sprites.constData(),retained.constData());
        }
        // Validate suppressed appearance metadata and retain its cache dependency.
        // No suppressed icon decode, playback part or resource requirement is needed.
        f.resources.erase(124);
        f.relay(grouped); QCOMPARE(f.sprites.size(),3);
        const auto child=f.world.appearances[4];
        const char* refusedChildren[]={
            R"({"overlays":[1]})", R"({"underlays":[1]})", R"({"appearanceFlags":67108864})",
            R"({"appearanceFlags":134217728})", R"({"hasPlane":true,"plane":71})",
            R"({"renderSource":"*other"})", R"({"renderTarget":"*other"})", R"({"screenLoc":"1,1"})",
            R"({"maptext":"text"})", R"({"hasTransform":true})",
            R"({"vars":[{"bit":8192,"data":"2000"}]})",
            R"({"filters":[{"type":1,"flags":1,"paramMask":1,"params":"0000803f"}]})",
            R"({"filters":[{"type":99,"flags":1}]})"
        };
        for (const char* fields:refusedChildren) {
            setFields(f.world.appearances[4], fields); f.refresh(); QVERIFY2(noRelay(),fields);
            f.world.appearances[4]=child; f.refresh(); QCOMPARE(f.sprites.size(),3);
        }
        f.world.appearances.erase(4); f.refresh(); QVERIFY(noRelay());
        f.world.appearances[4]=child; f.refresh(); QCOMPARE(f.sprites.size(),3);
        f.world.appearances[3].fields.iconResID=0; f.refresh(); QCOMPARE(f.sprites.size(),3); // base icon replaced too
        setFields(f.world.appearances[4], "{}"); f.refresh(); QCOMPARE(f.sprites.size(),3); // zero child offsets
        QCOMPARE(f.at({8,72}).click.over.kind,uint8_t(proto::enc::RefTurf));
        const QJsonObject refusedRoots[]={
            {{"filters",QJsonArray{QJsonObject{{"type",99},{"flags",1}}}}},
            {{"filters",QJsonArray{Fixture::blur("0000803f")[0],QJsonObject{{"type",99},{"flags",1}}}}},
            {{"hasTransform",true}}, {{"pixelX",8}}, {{"appearanceFlags",0x0c000204}}
        };
        for (const auto& changes:refusedRoots) {
            auto fields=grouped;
            for(auto it=changes.begin();it!=changes.end();++it) fields[it.key()]=it.value();
            f.relay(fields); QVERIFY(noRelay());
        }
        // Suppression must not turn a traversal-limit partial template into an
        // admitted source. The same bound applies before and after replacement.
        QJsonArray tooMany; for(int i=0;i<65536;++i) tooMany.append(4);
        f.relay({{"appearanceFlags",0x04000204},{"overlays",tooMany}}); QVERIFY(noRelay());
        f.relay(grouped); QCOMPARE(f.sprites.size(),3);
        f.world.appearances[4]=child;
        f.relay({{"overlays",QJsonArray{4}},{"underlays",QJsonArray{4}},{"appearanceFlags",0x04000204}});
        QCOMPARE(f.sprites.size(),3); // ordinary siblings are not a one-child whitelist
        // Extended grouping still cannot bind the separate local-target path.
        Fixture local;
        local.relay({{"overlays",QJsonArray{4}},{"appearanceFlags",0x04000204},{"renderTarget","*local"}});
        QVERIFY(std::none_of(local.sprites.begin(),local.sprites.end(),[](const auto& s){return s.atom==201u && !s.renderSource.isEmpty();}));
    }
    void planeSourceMatrix() {
        QJsonArray rows; QString error;
        QVERIFY2(shell::test::PlaneSourceFixture::capturePixelObservations(
            QString(FUGUE_TEST_DATA_DIR)+"/relay-gate-world/matrix.json",rows,error,false),qPrintable(error));
        shell::test::PlaneSourceFixture f;
        for (const auto& value:rows) {
            const auto row=value.toObject(); f.observation(row);
            const bool admitted=row["admitted"].toBool();
            const bool found=std::any_of(f.sprites.cbegin(),f.sprites.cend(),[](const auto& s){return s.atom==201u;});
            QVERIFY2(found==admitted,qPrintable(row["name"].toString()));
            if (!admitted) continue;
            const auto retained=f.sprites;
            shell::EntityMap::Stats stats; f.refresh(&stats);
            QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.frameDecodes,uint64_t(0));
            QCOMPARE(f.sprites.constData(),retained.constData());
            for (const auto& click:row["clicks"].toArray()) {
                const auto c=click.toArray(); const auto hit=f.at({c[0].toInt(),c[1].toInt()});
                QVERIFY2(hit.click.over.kind==uint8_t(c[2].toBool() ? proto::enc::RefObj : proto::enc::RefTurf),qPrintable(row["name"].toString()));
                QCOMPARE(hit.click.iconX,c[3].toInt()); QCOMPARE(hit.click.iconY,c[4].toInt());
                QVERIFY(shell::mapPointerCurrent(hit,f.world,17));
            }
        }
        // Reverse chains allow fractional alpha, but still require simple sources.
        const QJsonObject outline{{"type",2},{"flags",1},{"paramMask",35},{"params","0000004040ff80ff0000"}};
        const QJsonObject fields{{"overlays",QJsonArray{4}}, {"filters",QJsonArray{shell::test::PlaneSourceFixture::blur("0000803f")[0],outline}}};
        setFields(f.world.appearances[4], R"({"pixelX":68,"pixelY":32})");
        f.relay(fields); QCOMPARE(f.sprites.size(),4);
        const auto source=f.world.appearances[1];
        setFields(f.world.appearances[1], R"({"hasPlane":true,"plane":60,"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":128})");
        f.refresh(); QCOMPARE(f.sprites.size(),4);
        setFields(f.world.appearances[1], R"({"hasPlane":true,"plane":60,"vars":[{"bit":256,"data":"02"}]})");
        f.refresh(); QVERIFY(std::none_of(f.sprites.begin(),f.sprites.end(),[](const auto& s){return s.atom==201u;}));
        f.world.appearances[1]=source; f.refresh(); QCOMPARE(f.sprites.size(),4);
        for (bool grouped:{false,true}) {
            QJsonObject root;
            if (grouped) root={{"appearanceFlags",0x04000204},{"overlays",QJsonArray{4}}};
            root["vars"]=QJsonArray{QJsonObject{{"bit",1024},{"data","0000"}}};
            f.relay(root); QVERIFY(std::none_of(f.sprites.cbegin(),f.sprites.cend(),[](const auto& s){return s.atom==201u;}));
            root["vars"]=QJsonArray{QJsonObject{{"bit",256},{"data","05"}}};
            f.relay(root); QVERIFY(std::none_of(f.sprites.cbegin(),f.sprites.cend(),[](const auto& s){return s.atom==201u;}));
            root.remove("vars");
            for (const char* size:{"0000003e","0000c842","0000ca42"}) { // .125, 100, 101 saturates through shared decoder
                root["filters"]=shell::test::PlaneSourceFixture::blur(size); f.relay(root);
                QVERIFY(std::any_of(f.sprites.cbegin(),f.sprites.cend(),[](const auto& s){return s.atom==201u;}));
            }
        }
    }
    void planeSourceCaptureClassification() {
        using Fixture=shell::test::PlaneSourceFixture;
        QJsonArray rows; QString error;
        for (const char* name:{"source-coverage.json","capture-pixels.json","precision-model.json","master-arithmetic.json"}) {
            QJsonArray recorded;
            QVERIFY2(Fixture::capturePixelObservations(
                QString(FUGUE_TEST_DATA_DIR)+"/relay-gate-world/"+name,recorded,error),qPrintable(error));
            for (const auto& row:recorded) rows.append(row);
        }
        Fixture f;
        for (const auto& value:rows) {
            const auto row=value.toObject(); f.sourceObservation(row);
            const auto master=std::find_if(f.sprites.cbegin(),f.sprites.cend(),[](const auto& s){return s.kind==shell::MapSprite::Master;});
            QVERIFY(master!=f.sprites.cend()); QVERIFY(master->planeEffects);
            QVERIFY2(master->planeEffects->sourceCoverage(f.frame.source.size()),qPrintable(row["name"].toString()));
            shell::EntityMap::Stats stats; const auto prior=f.sprites; f.refresh(&stats);
            QCOMPARE(f.sprites.constData(),prior.constData()); // unchanged publication, no coverage raster/scan
        }
        f.sourceObservation(rows.first().toObject());
        const auto base=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(Fixture{}.world.appearances[2]))).object();
        for (const auto filters:{
                QJsonArray{QJsonObject{{"type",99},{"flags",1}}},
                QJsonArray{Fixture::blur("0000")[0]},
                QJsonArray{Fixture::blur("0000803f")[0],QJsonObject{{"type",99},{"flags",1}}}}) {
            auto master=base; master["filters"]=filters;
            setFields(f.world.appearances[2], QJsonDocument(master).toJson(QJsonDocument::Compact).toStdString()); f.relay({});
            const auto m=std::find_if(f.sprites.cbegin(),f.sprites.cend(),[](const auto& s){return s.kind==shell::MapSprite::Master;});
            QVERIFY(m!=f.sprites.cend()); QVERIFY(m->planeEffects->ignoredFilters);
            QVERIFY(!m->planeEffects->sourceCoverage(f.frame.source.size()));
            QVERIFY(!f.at({69,37}).relayHit); // rendering degradation is not a coverage proof
        }
        // Fractional raster alpha survives reverse chains and source coverage.
        auto master=base;
        const QJsonObject outline{{"type",2},{"flags",1},{"paramMask",35},{"params","0000004040ff80ff0000"}};
        master["filters"]=QJsonArray{Fixture::blur("0000803f")[0],outline};
        setFields(f.world.appearances[2], QJsonDocument(master).toJson(QJsonDocument::Compact).toStdString());
        f.world.appearances[1].fields.iconResID=2; f.relay({}); QVERIFY(f.at({69,37}).relayHit);
        f.world.appearances[1].fields.iconResID=0; f.refresh(); QVERIFY(f.at({69,37}).relayHit);
        shell::EntityMap missing;
        f.sprites=missing.prepare(f.world,f.frame,[](uint32_t,QByteArray&){return false;},0,nullptr,true);
        QVERIFY(!f.at({69,37}).relayHit);
        f.refresh(); QVERIFY(f.at({69,37}).relayHit);
    }
    void planeSourceBottomLeftSpellings() {
        auto w=world(); w.atoms.clear();
        w.hudOrder={42}; w.hudAppid[42]=1; w.hudIdentity[42]={2,99};
        shell::TurfMap terrain; shell::EntityMap scene;
        auto map=terrain.prepare(w,{},0,false);
        setFields(w.appearances[1], R"({"screenLoc":"SOUTHWEST","renderSource":"*capture"})");
        const auto southwest=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(southwest.size(),1);
        setFields(w.appearances[1], R"({"screenLoc":"1,1","renderSource":"*capture"})");
        const auto numeric=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(numeric.size(),1);
        QCOMPARE(numeric[0].position,southwest[0].position);
        QCOMPARE(numeric[0].renderSource,QString("*capture"));
        QVERIFY(!numeric[0].screenInteractive);
        // A relay's own pixel offsets move nothing (stock 516.1689, plane-relay-world rpx, rpw-loc23).
        setFields(w.appearances[1], R"({"screenLoc":"1,1","renderSource":"*capture","pixelX":1,"pixelY":-3,"pixelW":5,"pixelZ":7})");
        const auto offset=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(offset.size(),1);
        QCOMPARE(offset[0].position,numeric[0].position); QCOMPARE(offset[0].iconBottom,numeric[0].iconBottom);
        // Stock's CENTER moves the captured plane as far as the centre tile is from 1,1 (tests/data/plane-relay-world
        // loccenter); the relay stands there.
        setFields(w.appearances[1], R"({"screenLoc":"CENTER","renderSource":"*capture"})");
        const auto centre=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(centre.size(),1);
        QVERIFY(centre[0].position.x()>southwest[0].position.x() && centre[0].position.y()<southwest[0].position.y());
        // An edge counts in tiles, as for a one-tile icon (plane-relay-world locright "RIGHT-5,TOP-4" draws as 2,3).
        setFields(w.appearances[1], R"({"screenLoc":"TOP,CENTER","renderSource":"*capture"})");
        const auto top=scene.prepare(w,map,reader(),0,nullptr,true);
        setFields(w.appearances[1], R"({"screenLoc":"CENTER,NORTH","renderSource":"*capture"})");
        const auto north=scene.prepare(w,map,reader(),0,nullptr,true);
        QCOMPARE(top.size(),1); QCOMPARE(north.size(),1);
        QCOMPARE(top[0].iconBottom,north[0].iconBottom); QCOMPARE(top[0].position,north[0].position);
        QVERIFY(top[0].iconBottom!=centre[0].iconBottom);
    }
    void screenRangesLateResourcesAndChildren() {
        auto w=world();w.atoms.clear();w.hudOrder={42};w.hudAppid[42]=1;w.hudIdentity[42]={2,99};
        w.hudIconStart[42]={1,1000000000};
        w.appearances[1].fields.iconState="";
        setFields(w.appearances[1], R"({"screenLoc":"1,1 TO 2,2","overlays":[2],"dir":4})");
        bool ready=false;
        auto read=[&](uint32_t,QByteArray& out){out=iconfixture::animation(1);return ready;};
        shell::TurfMap terrain;shell::EntityMap scene;shell::EntityMap::Stats stats;
        const auto map=terrain.prepare(w,{},0,false);
        QVERIFY(scene.prepare(w,map,read,1100000000,nullptr,true).isEmpty());
        ready=true;const auto first=scene.prepare(w,map,read,1200000000,nullptr,true);
        QCOMPARE(first.size(),4);for(const auto& s:first) QCOMPARE(s.image.pixelColor(16,0),QColor(210,70,20));
        w.appearances[2]=w.appearances[1]; // its own icon: a child with neither icon nor state draws none
        setFields(w.appearances[2], R"({"pixelX":-8,"pixelY":4,"dir":2})");
        const auto children=scene.prepare(w,map,read,1250000000,nullptr,true);QCOMPARE(children.size(),8);
        for(const auto& s:children) QCOMPARE(s.image.pixelColor(16,0),QColor(210,70,20));
        w.hudOrder.clear();scene.prepare(w,map,read,1260000000,nullptr,true);w.hudOrder={42};
        const auto back=scene.prepare(w,map,read,1270000000,nullptr,true);
        QCOMPARE(back[0].image.cacheKey(),children[0].image.cacheKey());
        QCOMPARE(scene.prepare(w,map,read,1280000000,&stats,true).constData(),back.constData());
        w.resources[0]={456,0};ready=false;QVERIFY(scene.prepare(w,map,read,1290000000,nullptr,true).isEmpty());
        ready=true;const auto remap=scene.prepare(w,map,read,1300000000,nullptr,true);
        QCOMPARE(remap.size(),8);QCOMPARE(remap[0].image.pixelColor(16,0),QColor(210,70,20));
        QVERIFY(remap[0].image.cacheKey()!=children[0].image.cacheKey());
        setFields(w.appearances[1], R"({"screenLoc":"1,1 TO 10000,10000"})");
        QVERIFY(scene.prepare(w,map,read,1300000000,nullptr,true).isEmpty());
        QCOMPARE(first.size(),4); // immutable publication survived all mutations
    }
    void flickWireIdentityAndIngest() {
        model::Session session; session.worldMut()=world(); auto& w=session.worldMut();
        w.appearances[2]=w.appearances[1]; w.appearances[2].fields.iconState="green";
        auto feed=[&](uint16_t type,const QByteArray& bytes,int64_t ns) {
            session.setIngestClockNs(ns);
            session.ingest(type,{reinterpret_cast<const uint8_t*>(bytes.constData()),size_t(bytes.size())});
        };
        const auto command=QByteArray::fromHex("022a000000630002000000");
        const auto event=QByteArray::fromHex("0070756c736500022a000000010000006300");
        feed(0xc7,command,100); QCOMPARE(w.atoms[42].appid,2u); QCOMPARE(w.atoms[42].iconStartNs,int64_t(100));
        QVERIFY(w.flicks.empty());
        feed(0x6c,event,110); QCOMPARE(w.flicks.at(42).startNs,int64_t(110));
        QCOMPARE(w.flicks.at(42).iconState,std::string("pulse")); QCOMPARE(w.flicks.at(42).iconResIndex,1u);
        QCOMPARE(w.flicks.at(42).key,uint16_t(99)); QVERIFY(w.flicks.at(42).haveKey);
        feed(0xc7,command,120); QCOMPARE(w.flicks.at(42).startNs,int64_t(110));
        QCOMPARE(w.atoms[42].iconStartNs,int64_t(100));
        feed(0x6c,event,130); QCOMPARE(w.flicks.at(42).startNs,int64_t(130));
        w.atoms[42].hasPos=false; feed(0x6c,event,140); w.atoms[42].hasPos=true;
        QCOMPARE(w.flicks.at(42).startNs,int64_t(140));
        feed(0x6c,QByteArray::fromHex("0102000000022a0000006300"),150);
        QCOMPARE(w.flicks.at(42).iconResIndex,2u); QVERIFY(w.flicks.at(42).iconState.empty());
        feed(0xc7,QByteArray::fromHex("022a000000640001000000"),160); // recycled slot
        QVERIFY(w.flicks.empty()); QCOMPARE(w.atoms[42].key,uint16_t(100));
        feed(0x6c,event,170); QVERIFY(w.flicks.empty()); // stale event
        feed(0xc7,QByteArray::fromHex("022b000000650001000000"),180); // absent record check/bind
        QVERIFY(w.atoms.count(43)); QCOMPARE(w.atoms[43].appid,1u);
        feed(0xc7,QByteArray::fromHex("022b0000006500"),190); QCOMPARE(w.atoms[43].appid,1u);
        QCOMPARE(session.ingestErrors(),0);
        for (auto bad : {event.left(event.size()-1), event+char(0), QByteArray::fromHex("0200000000022a0000006400"),
                         QByteArray::fromHex("0100000000032a0000006400"),
                         QByteArray(1,char(0))+QByteArray(4097,'x')+QByteArray::fromHex("00022a000000010000006400")}) {
            feed(0x6c,bad,200); QVERIFY(w.flicks.empty());
        }
        QCOMPARE(session.ingestErrors(),5);
        feed(0x6c,QByteArray::fromHex("0100000000022a0000006400"),210); QVERIFY(w.flicks.count(42));
        feed(0x6f,QByteArray::fromHex("2a000000"),220); QVERIFY(!w.flicks.count(42));
        // A mob (kind 0 here, 3 in the FlickTarget, which does not make its record) and a held turf by world index
        // (stock 516.1689, tests/data/flick-world).
        const uint32_t mob=model::kRegistryA|7;
        feed(0xc7,QByteArray::fromHex("0307000000630001000000"),230); QVERIFY(!w.atoms.count(mob));
        feed(0x6c,QByteArray::fromHex("0070756c7365000007000000010000006300"),240); QVERIFY(!w.flicks.count(mob));
        w.atoms[mob].key=99; w.atoms[mob].haveKey=true;
        feed(0x6c,QByteArray::fromHex("0070756c7365000007000000010000006300"),250);
        QCOMPARE(w.flicks.at(mob).startNs,int64_t(250));
        w.bounds.maxX=10; w.bounds.maxY=10; w.tiles[{3,2,0}]=1;
        feed(0x6c,QByteArray::fromHex("0070756c736500011800000001000000"),260); QVERIFY(w.turfFlicks.empty());
        feed(0x6c,QByteArray::fromHex("0070756c736500011700000001000000"),270);
        QCOMPARE(w.turfFlicks.at({3,2,0}).iconState,std::string("pulse"));
        QCOMPARE(session.ingestErrors(),5);
    }
    void flickInstancesTransitionsAndRetries() {
        auto w=world(); w.resources={{123,0},{456,0}}; w.appearances[1].fields.iconState="other";
        w.atoms[42].iconStartNs=900000000;
        // West draws last; keep playback index 0 on atom 42.
        w.atoms[43]=w.atoms[42]; w.atoms[43].x--;
        w.flicks[42]={"pulse",1,true,1000000000,99,true};
        w.flicks[43]={"pulse",1,true,1125000000,99,true};
        bool ready=false;
        auto read=[&](uint32_t id,QByteArray& out){out=iconfixture::flickAsset();return id==123 || ready;};
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        auto map=terrain.prepare(w,{},0,false);
        auto waiting=scene.prepare(w,map,read,1150000000); QCOMPARE(waiting.size(),2);
        QCOMPARE(waiting[0].image.pixelColor(16,0).blue(),220);
        ready=true; const auto first=scene.prepare(w,map,read,1150000000);
        QCOMPARE(first[0].image.pixelColor(16,0),QColor(130,30,20));
        QCOMPARE(first[1].image.pixelColor(16,0),QColor(50,30,20));
        QCOMPARE(scene.prepare(w,map,read,1160000000,&stats).constData(),first.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.sortedSprites,uint64_t(0));
        auto next=scene.prepare(w,map,read,1200000000,&stats);
        QCOMPARE(next[0].image.pixelColor(16,0).red(),210); QCOMPARE(stats.sortedSprites,uint64_t(0));
        w.flicks[42].startNs=1200000000; next=scene.prepare(w,map,read,1200000000,&stats);
        QCOMPARE(next[0].image.pixelColor(16,0),QColor(50,30,20));
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.sortedSprites,uint64_t(0));
        w.flicks[42]={"other",1,true,1250000000,99,true};
        next=scene.prepare(w,map,read,1300000000); QCOMPARE(next[0].image.pixelColor(16,0),QColor(130,30,220));
        // Resource remapping, template rebuilding and disappearance do not reset the event.
        w.atoms[42].hasPos=false; scene.prepare(w,map,read,1320000000); w.atoms[42].hasPos=true;
        setFields(w.appearances[1], R"({"pixelX":1})"); w.atoms[42].dir=4;
        w.resources[1]={789,0}; ready=false; scene.prepare(w,map,read,1400000000);
        ready=true; next=scene.prepare(w,map,read,1450000000);
        QCOMPARE(next[0].image.pixelColor(16,0),QColor(210,70,220));
        QCOMPARE(w.flicks[42].startNs,int64_t(1250000000));
        // Completion restores the CURRENT underlying state/ordinary phase, not a saved image.
        w.appearances[1].fields.iconState="pulse"; w.atoms[42].iconStartNs=1400000000;
        const auto completed=scene.prepare(w,map,read,1650000000); QCOMPARE(completed[0].image.pixelColor(16,0),QColor(210,70,20));
        QCOMPARE(scene.prepare(w,map,read,1660000000,&stats).constData(),completed.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.sceneReuses,uint64_t(1));
        QCOMPARE(first[0].image.pixelColor(16,0),QColor(130,30,20));
        // Identity safety even for directly constructed model worlds.
        w.flicks[42].startNs=1700000000; ++w.atoms[42].key;
        next=scene.prepare(w,map,read,1700000000); QCOMPARE(next[0].image.pixelColor(16,0),QColor(210,70,20));
        w.flicks[42]={"other",1,true,2000000000,w.atoms[42].key,true};
        w.resources[1]={999,0}; ready=false; scene.prepare(w,map,read,2050000000);
        ready=true; next=scene.prepare(w,map,read,2600000000);
        QCOMPARE(next[0].image.pixelColor(16,0),QColor(50,70,20)); // arrived after completion: no flash/restart
        scene.prepare(w,map,read,INT64_MAX);
    }
    void flickInheritanceMissingAndBounds() {
        auto w=world(); w.resources={{123,0},{456,0}}; w.appearances[1].fields.iconState="";
        setFields(w.appearances[1], R"({"underlays":[2],"overlays":[3,4]})");
        for (uint32_t id : {2u,3u,4u}) {
            w.appearances[id]=w.appearances[1]; setFields(w.appearances[id], "{}");
            w.appearances[id].fields.iconResID=0xffffu; w.appearances[id].fields.iconState="other"; // captured wire no-icon sentinel
        }
        setFields(w.appearances[3], R"({"dir":1,"appearanceFlags":131072})");
        w.appearances[4].fields.iconState="short";
        w.atoms[42].dir=4; w.flicks[42]={"pulse",1,true,1000000000,99,true};
        auto read=[](uint32_t id,QByteArray& out){out=iconfixture::flickAsset(id==456?64:32);return true;};
        shell::TurfMap terrain; shell::EntityMap scene; auto map=terrain.prepare(w,{},0,false);
        const auto first=scene.prepare(w,map,read,1100000000); QCOMPARE(first.size(),4);
        QCOMPARE(first[0].image.pixelColor(32,0),QColor(130,70,220)); // inherited icon/direction, own state
        QCOMPARE(first[1].image.pixelColor(32,0),QColor(130,70,20));
        QCOMPARE(first[2].image.pixelColor(32,0),QColor(130,50,220)); // fixed child direction
        QCOMPARE(first[3].image.pixelColor(32,0),QColor(130,70,220)); // short child keeps looping
        QCOMPARE(first[1].size,QSize(64,64));
        const auto completed=scene.prepare(w,map,read,1400000000); QCOMPARE(completed[1].size,QSize(32,32));
        QCOMPARE(first[1].position,completed[1].position-QPoint(0,32));
        QCOMPARE(first[1].iconBottom,completed[1].iconBottom);
        w.flicks[42].iconState="missing"; w.flicks[42].startNs=1500000000;
        auto missing=scene.prepare(w,map,read,1500000000); QCOMPARE(missing.size(),3); // no fallback root pixels
        QCOMPARE(scene.prepare(w,map,read,1700000000).size(),4); // default-state duration
        // Late child appearances join elapsed flick time, not frame zero.
        w.flicks[42]={"pulse",1,true,1800000000,99,true};
        auto child=w.appearances[2]; w.appearances.erase(2); scene.prepare(w,map,read,1850000000);
        w.appearances[2]=child; auto late=scene.prepare(w,map,read,2000000000);
        QCOMPARE(late[0].image.pixelColor(32,0),QColor(210,70,220));
        setFields(w.appearances[1], "{}");
        // A wider flick can intersect the view while the ordinary root does not.
        w.atoms[42].x=4; w.atoms[42].y=6;
        QVERIFY(!scene.prepare(w,map,read,2000000000).isEmpty());
        QVERIFY(scene.prepare(w,map,read,2200000000).isEmpty());
    }
    void flickTransitiveDependenciesAndLateRoot() {
        auto w=world(); w.resources={{123,0},{456,12},{789,12},{987,0}};
        w.appearances[1].fields.iconState=""; setFields(w.appearances[1], R"({"overlays":[2]})");
        w.appearances[2]=w.appearances[1]; setFields(w.appearances[2], "{}"); w.appearances[2].fields.iconResID=1;
        w.flicks[42]={"pulse",0,true,1000000000,99,true};
        std::map<uint32_t,QByteArray> data{{123,iconfixture::flickAsset()},
            {456,iconfixture::recipe(iconfixture::select(2,""))},
            {789,iconfixture::recipe(iconfixture::select(3,"base"))}};
        auto read=[&](uint32_t id,QByteArray& out){if(!data.count(id))return false;out=data.at(id);return true;};
        shell::TurfMap terrain; shell::EntityMap scene; const auto map=terrain.prepare(w,{},0,false);
        const auto root=w.appearances[1]; w.appearances.erase(1);
        QVERIFY(scene.prepare(w,map,read,1000000000).isEmpty());
        w.appearances[1]=root; auto late=scene.prepare(w,map,read,1100000000);
        QCOMPARE(late.size(),1); QCOMPARE(late[0].image.pixelColor(16,0),QColor(130,30,20));
        data[987]=iconfixture::sheet(); const auto first=scene.prepare(w,map,read,1200000000);
        QCOMPARE(first.size(),2); QCOMPARE(first[1].image.pixelColor(16,0),QColor(Qt::red));
        w.resources[3]={988,0}; QCOMPARE(scene.prepare(w,map,read,1250000000).size(),1);
        data[988]=iconfixture::sheet(Qt::blue); const auto remapped=scene.prepare(w,map,read,1300000000);
        QCOMPARE(remapped[0].image.pixelColor(16,0),QColor(210,30,20));
        QCOMPARE(remapped[1].image.pixelColor(16,0),QColor(Qt::blue));
        QCOMPARE(first[1].image.pixelColor(16,0),QColor(Qt::red));
        shell::EntityMap::Stats stats;
        QCOMPARE(scene.prepare(w,map,read,1310000000,&stats).constData(),remapped.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.sortedSprites,uint64_t(0));
    }
    void flickGeneratedAndMobTargets() {
        auto w=world(); w.appearances[1].fields.iconState=""; w.resources={{123,0},{456,12}};
        w.flicks[42]={"",1,true,1000000000,99,true};
        auto read=[](uint32_t id,QByteArray& out){out=id==123?iconfixture::flickAsset():iconfixture::recipe(iconfixture::select(0,"pulse"));return true;};
        shell::TurfMap terrain; shell::EntityMap scene; const auto map=terrain.prepare(w,{},0,false);
        auto sprites=scene.prepare(w,map,read,1000000000); QCOMPARE(sprites[0].image.pixelColor(16,0),QColor(180,180,180));
        w.flicks[42]={"pulse",0,true,1000000000,99,true};
        w.atoms[42|model::kRegistryA]=w.atoms[42]; w.atoms.erase(42);
        w.flicks[42|model::kRegistryA]=w.flicks[42];
        sprites=scene.prepare(w,map,read,1000000000); QCOMPARE(sprites[0].image.pixelColor(16,0),QColor(50,30,20)); // a mob flicks
    }
    void animationIngestRestartRules() {
        model::Session session; session.worldMut()=world();
        auto& w=session.worldMut();
        w.appearances[2]=w.appearances[1]; setFields(w.appearances[2], R"({"dir":1,"overlays":[99],"name":"changed"})");
        w.appearances[3]=w.appearances[1]; w.appearances[3].fields.iconState="other";
        w.appearances[4]=w.appearances[3]; w.appearances[4].fields.iconResID=1;
        auto bind=[&](uint8_t app,int64_t ns){
            session.setIngestClockNs(ns);
            const std::array<uint8_t,8> bytes{42,0,0,0,app,0,0,0}; session.ingest(0x0024,bytes);
        };
        bind(1,1000000000); QCOMPARE(w.atoms[42].iconStartNs,int64_t(1000000000));
        bind(2,1100000000); QCOMPARE(w.atoms[42].iconStartNs,int64_t(1000000000));
        w.atoms[42].hasPos=false; bind(2,1200000000); w.atoms[42].hasPos=true;
        QCOMPARE(w.atoms[42].iconStartNs,int64_t(1000000000));
        bind(3,1300000000); QCOMPARE(w.atoms[42].iconStartNs,int64_t(1300000000));
        bind(4,1400000000); QCOMPARE(w.atoms[42].iconStartNs,int64_t(1400000000));
        bind(4,1500000000); QCOMPARE(w.atoms[42].iconStartNs,int64_t(1400000000));
        auto screen=[&](uint8_t app,int64_t ns){
            session.setIngestClockNs(ns);
            const std::array<uint8_t,14> bytes{2,14,0,2,9,0,0,0,0,0,app,0,0,0}; session.ingest(0x0076,bytes);
        };
        screen(1,1000000000); screen(2,1100000000);
        QCOMPARE(w.hudIconStart.at(9).ns,int64_t(1000000000));
        const std::array<uint8_t,9> remove{1,9,0,0,0,0,0,0,0}; session.ingest(0x0076,remove);
        QVERIFY(w.hudOrder.empty()); screen(2,1200000000);
        QCOMPARE(w.hudIconStart.at(9).ns,int64_t(1000000000));
        screen(3,1300000000); QCOMPARE(w.hudIconStart.at(9).ns,int64_t(1300000000));
        QCOMPARE(session.ingestErrors(),0);
    }
    void animationInstancesAndRetries() {
        auto w = world(); w.appearances[1].fields.iconState = "absent-default";
        w.atoms[42].iconStartNs = 1000000000;
        // Nonoverlapping west neighbor: playback assertions keep atom 42 first.
        w.atoms[43] = w.atoms[42]; w.atoms[43].iconStartNs = 1125000000; w.atoms[43].x--;
        bool ready = false;
        auto read = [&](uint32_t,QByteArray& out){out=iconfixture::animation(1);return ready;};
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        auto map=terrain.prepare(w,{},0,false);
        QVERIFY(scene.prepare(w,map,read,1150000000).isEmpty());
        ready=true; const auto first=scene.prepare(w,map,read,1150000000);
        QCOMPARE(first.size(),2); QCOMPARE(first[0].image.pixelColor(16,0),QColor(130,30,20));
        QCOMPARE(first[1].image.pixelColor(16,0),QColor(50,30,20));
        QCOMPARE(scene.prepare(w,map,read,1160000000,&stats).constData(),first.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.sortedSprites,uint64_t(0));
        const auto next=scene.prepare(w,map,read,1200000000,&stats);
        QCOMPARE(next[0].image.pixelColor(16,0),QColor(210,30,20));
        QCOMPARE(next[1].image.pixelColor(16,0),QColor(130,30,20));
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.sortedSprites,uint64_t(0));
        QCOMPARE(first[0].image.pixelColor(16,0),QColor(130,30,20)); // published data is immutable
        // Rebuild/turn/move/re-enter: no resource/template/view-arrival phase.
        setFields(w.appearances[1], R"({"pixelX":1})"); w.atoms[42].dir=4;
        auto turned=scene.prepare(w,map,read,1200000000);
        QCOMPARE(turned[0].image.pixelColor(16,0),QColor(210,70,20));
        w.atoms[42].hasPos=false; scene.prepare(w,map,read,1250000000);
        w.atoms[42].hasPos=true; auto reentered=scene.prepare(w,map,read,1300000000);
        QCOMPARE(reentered[0].image.cacheKey(),turned[0].image.cacheKey());
        // A model icon/state bind is the explicit restart input; an unchanged ID
        // can still restart on record replacement. Different instances remain independent.
        w.atoms[42].iconStartNs=1300000000; auto restarted=scene.prepare(w,map,read,1300000000);
        QCOMPARE(restarted[0].image.pixelColor(16,0),QColor(50,70,20));
        w.resources[0]={456,0}; ready=false; QVERIFY(scene.prepare(w,map,read,1350000000).isEmpty());
        ready=true; auto late=scene.prepare(w,map,read,1500000000);
        QCOMPARE(late[0].image.pixelColor(16,0),QColor(210,70,20));
        QCOMPARE(scene.prepare(w,map,read,INT64_MAX)[0].image.pixelColor(16,0),QColor(210,70,20));
    }
    void animationChildrenTurfsAndHud() {
        auto w = world(); w.appearances[1].fields.iconState="";
        setFields(w.appearances[1], R"({"underlays":[2],"overlays":[3]})");
        for (uint32_t id : {2u,3u}) {w.appearances[id]=w.appearances[1];setFields(w.appearances[id], "{}");}
        setFields(w.appearances[3], R"({"dir":1,"appearanceFlags":131072})");
        w.atoms[42].dir=4; w.atoms[42].iconStartNs=1000000000;
        auto read=[](uint32_t,QByteArray& out){out=iconfixture::animation(1);return true;};
        shell::TurfMap terrain; shell::EntityMap scene;
        auto map=terrain.prepare(w,{},0,false);
        auto first=scene.prepare(w,map,read,1050000000);
        QCOMPARE(first.size(),3);
        QCOMPARE(first[0].image.pixelColor(16,0),QColor(130,70,20));
        QCOMPARE(first[2].image.pixelColor(16,0),QColor(130,50,20));
        // Missing child arrival is not a restart of the enclosing finite sequence.
        w.appearances.erase(2); scene.prepare(w,map,read,1100000000);
        w.appearances[2]=w.appearances[3]; auto late=scene.prepare(w,map,read,1200000000);
        QCOMPARE(late.size(),3); QCOMPARE(late[0].image.pixelColor(16,0),QColor(210,50,20));
        const auto ordinary=fieldsJson(w.appearances[1]);
        for(const auto& m:std::array<std::array<float,6>,3>{{{0,1,0,-1,0,0},{.5f,0,0,0,.5f,0},{1,0,0,0,1,0}}}) {
            setFields(w.appearances[1], affinefixture::fields(m,",\"underlays\":[2],\"overlays\":[3]"));
            const auto posed=scene.prepare(w,map,read,1200000000); QCOMPARE(posed.size(),3);
            for(int i=0;i<3;++i) QCOMPARE(posed[i].image.cacheKey(),late[i].image.cacheKey());
            QCOMPARE(w.atoms[42].iconStartNs,int64_t(1000000000));
        }
        setFields(w.appearances[1], ordinary);
        w.atoms.clear(); w.tiles[{5,5,0}]=1; w.tileIconStart[{5,5,0}]={1,1000000000};
        w.appearances[4]=w.appearances[1]; setFields(w.appearances[4], R"({"screenLoc":"CENTER"})");
        w.hudOrder={9}; w.hudAppid[9]=4; w.hudIconStart[9]={4,1100000000};
        auto combined=scene.prepare(w,map,read,1225000000,nullptr,true);
        QCOMPARE(combined.size(),4);
        for(const auto& sprite:combined) QCOMPARE(sprite.image.pixelColor(16,0).red(),sprite.kind==shell::MapSprite::Screen?130:210);
    }
    void loopingAnimationReuse() {
        auto w=world(); w.appearances[1].fields.iconState="";
        w.atoms[42].iconStartNs=123; w.atoms[43]=w.atoms[42]; w.atoms[43].iconStartNs=987654321;
        auto read=[](uint32_t,QByteArray& out){out=iconfixture::animation();return true;};
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        auto map=terrain.prepare(w,{},0,false);
        auto first=scene.prepare(w,map,read,1); QCOMPARE(first[0].image.cacheKey(),first[1].image.cacheKey());
        scene.prepare(w,map,read,50000000); scene.prepare(w,map,read,200000000);
        const auto again=scene.prepare(w,map,read,400000000,&stats);
        QCOMPARE(again[0].image.cacheKey(),first[0].image.cacheKey()); QCOMPARE(stats.frameDecodes,uint64_t(0));
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.sortedSprites,uint64_t(0));
        QCOMPARE(scene.prepare(w,map,read,410000000,&stats).constData(),again.constData());
        QCOMPARE(stats.sceneReuses,uint64_t(1));
    }
    void generatedAnimationResidentFrames() {
        auto w=world(); w.resources={{100,12},{200,3}};
        w.atoms[42].iconStartNs=1000000000;
        auto read=[](uint32_t id,QByteArray& out) {
            out=id==100 ? iconfixture::recipe(iconfixture::select(1,"",0)+QByteArray::fromHex("4402")) : iconfixture::animation(2,true);
            return true;
        };
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto map=terrain.prepare(w,{},0,false);
        const auto first=scene.prepare(w,map,read,1000000000); QCOMPARE(first.size(),1);
        const auto middle=scene.prepare(w,map,read,1050000000); QCOMPARE(middle.size(),1);
        QVERIFY(first[0].image.cacheKey()!=middle[0].image.cacheKey());
        scene.prepare(w,map,read,1200000000);
        const auto reverse=scene.prepare(w,map,read,1400000000,&stats);
        QCOMPARE(reverse[0].image.cacheKey(),middle[0].image.cacheKey());
        QCOMPARE(stats.frameDecodes,uint64_t(0)); QCOMPARE(stats.templateBuilds,uint64_t(0));
        QCOMPARE(stats.resourceReads,uint64_t(0));
        ++w.atoms[42].pixelX;
        const auto moved=scene.prepare(w,map,read,1450000000,&stats);
        QCOMPARE(moved[0].image.cacheKey(),middle[0].image.cacheKey());
        QCOMPARE(stats.frameDecodes,uint64_t(0)); QCOMPARE(stats.templateBuilds,uint64_t(0));
        QCOMPARE(first[0].image.pixelColor(16,0),QColor(50,30,20));
    }
    void deferredGeneratedPreview() {
        auto w=world(); w.resources.push_back({100,12}); w.resources.push_back({200,3});
        w.hudOrder={50}; w.hud[50]={}; w.hudAppid[50]=2;
        w.hudIdentity[50]={2,100,18}; w.hudIconStart[50]={2,1000000000};
        w.appearances[2]=w.appearances[1]; w.appearances[2].fields.iconResID=1; w.appearances[2].fields.iconState="";
        setFields(w.appearances[2], R"({"screenLoc":"preview:1,1"})");
        QByteArray body=iconfixture::select(2,"",0)+QByteArray::fromHex("4b00010001");
        for(int i=0;i<120;++i) body+=QByteArray::fromHex("4402");
        body+=QByteArray::fromHex("4b20002000");
        const auto program=iconfixture::recipe(body), source=iconfixture::animation(2,true,1), ordinary=iconBytes();
        auto read=[&](uint32_t id,QByteArray& out) {out=id==100?program:id==200?source:ordinary;return true;};
        shell::TurfMap terrain; shell::EntityMap main; shell::NamedMap preview("preview");
        const auto map=terrain.prepare(w,{},0,false);
        const auto baseline=main.prepare(w,map,read,1050000000,nullptr,true);
        shell::NamedMap::Frame frame;
        int turns=0;
        QElapsedTimer timer; timer.start(); qint64 longest=0;
        do {
            const auto start=timer.nsecsElapsed();
            frame=preview.prepare(w,read,1050000000,42);
            longest=std::max(longest,timer.nsecsElapsed()-start);
            // Worker's normal MAP preparation and an input-driven movement update
            // continue between slices. No partial preview or altered main material.
            const auto current=main.prepare(w,map,read,1050000000,nullptr,true);
            QCOMPARE(current[0].image.cacheKey(),baseline[0].image.cacheKey());
            QCOMPARE(current[0].position,baseline[0].position+QPoint(w.atoms[42].pixelX,0));
            w.atoms[42].pixelX=1-w.atoms[42].pixelX;
        } while(frame.sprites.empty() && ++turns<1000);
        QVERIFY(turns>0); QCOMPARE(frame.sprites.size(),1);
        // Completion samples the ingest clock, not a new recipe-completion clock.
        QCOMPARE(frame.sprites[0].image.pixelColor(16,0),QColor(130,30,20));
        const auto retained=frame.sprites;
        shell::EntityMap::Stats stats;
        const auto warm=preview.prepare(w,read,1050000000,42,&stats);
        QCOMPARE(warm.sprites[0].image.cacheKey(),retained[0].image.cacheKey());
        QCOMPARE(stats.resourceReads,uint64_t(0)); QCOMPARE(stats.frameDecodes,uint64_t(0));
        const auto later=preview.prepare(w,read,1200000000,42);
        QCOMPARE(later.sprites[0].image.pixelColor(16,0),QColor(210,30,20));
        QCOMPARE(retained[0].image.pixelColor(16,0),QColor(130,30,20));
        qInfo() << "deferred preview turns/longestNs" << turns << longest;
        // Generous scheduler allowance, not a claimed real-time 2ms deadline.
        QVERIFY(longest<250000000);
    }
    void denseDeferredPreview_data() {
        QTest::addColumn<int>("previews"); QTest::addColumn<bool>("warm");
        QTest::newRow("main-only") << 0 << false;
        QTest::newRow("one-cold") << 1 << false;
        QTest::newRow("two-cold") << 2 << false;
        QTest::newRow("two-warm") << 2 << true;
    }
    void denseDeferredPreview() {
        QFETCH(int, previews); QFETCH(bool, warm);
        auto w=world(); w.bounds={true,128,128,0,71,71,71,71};
        const auto prototype=w.atoms.at(42); w.atoms.clear();
        uint32_t id=1000;
        for(uint16_t y=4;y<75;++y) for(uint16_t x=4;x<75;++x) {
            w.tiles[{x,y,0}]=1;
            for(int copy=0;copy<2;++copy) {
                auto atom=prototype; atom.x=x+1; atom.y=y+1;
                w.atoms[id++]=atom;
            }
        }
        w.resources.push_back({100,12}); w.resources.push_back({200,3});
        for(uint32_t i=0;i<2;++i) {
            const uint32_t ref=50+i, app=2+i;
            w.hudOrder.push_back(ref); w.hud[ref]={}; w.hudAppid[ref]=app;
            w.hudIdentity[ref]={2,100,18+i}; w.hudIconStart[ref]={app,1000000000};
            w.appearances[app]=w.appearances[1]; w.appearances[app].fields.iconResID=1;
            w.appearances[app].fields.iconState="";
            setFields(w.appearances[app], i ? R"({"screenLoc":"preview-b:1,1"})" : R"({"screenLoc":"preview-a:1,1"})");
        }
        QByteArray body=iconfixture::select(2,"",0)+QByteArray::fromHex("4b00010001");
        for(int i=0;i<120;++i) body+=QByteArray::fromHex("4402");
        body+=QByteArray::fromHex("4b20002000");
        const auto program=iconfixture::recipe(body), source=iconfixture::animation(2,true,1), ordinary=iconBytes();
        auto read=[&](uint32_t resource,QByteArray& out) {out=resource==100?program:resource==200?source:ordinary;return true;};
        shell::TurfMap terrain; shell::EntityMap main;
        shell::NamedMap a("preview-a"), b("preview-b");
        const std::array<shell::NamedMap*,2> named{&a,&b};
        std::array<shell::NamedMap::Frame,2> frames;
        if(warm) for(int i=0;i<previews;++i) {
            int attempts=0;
            do { frames[i]=named[i]->prepare(w,read,1050000000,42); }
            while(frames[i].sprites.empty() && ++attempts<1000);
            QCOMPARE(frames[i].sprites.size(),1);
        }
        auto map=terrain.prepare(w,{},0,false);
        const auto retained=main.prepare(w,map,read,1050000000,nullptr,true);
        QCOMPARE(retained.size(),15123); // 71x71 turfs and two ordinary objects/cell
        const auto firstObject=std::find_if(retained.begin(),retained.end(),[](const auto& s){return s.kind==shell::MapSprite::Entity;});
        QVERIFY(firstObject!=retained.end()); const auto oldObject=*firstObject;
        int pendingTurns=0;
        QElapsedTimer phaseTimer; phaseTimer.start();
        for(int turn=0;turn<1000;++turn) {
            // Deterministic dense object movement, not a live eye/LOS benchmark.
            for(auto& [key,atom]:w.atoms) atom.pixelX=turn%2;
            QElapsedTimer timer; timer.start();
            map=terrain.prepare(w,{},0,false);
            shell::EntityMap::Stats stats;
            const auto current=main.prepare(w,map,read,1050000000,&stats,true);
            const double mainMs=double(timer.nsecsElapsed())/1e6;
            const auto namedStart=timer.nsecsElapsed();
            bool pending=false;
            for(int i=0;i<previews;++i) {
                pending=pending || frames[i].sprites.empty();
                frames[i]=named[i]->prepare(w,read,1050000000,42);
            }
            const double namedMs=double(timer.nsecsElapsed()-namedStart)/1e6;
            const double totalMs=double(timer.nsecsElapsed())/1e6;
            pendingTurns+=pending;
            QCOMPARE(current.size(),retained.size());
            QCOMPARE(stats.resourceReads,uint64_t(0)); QCOMPARE(stats.frameDecodes,uint64_t(0));
            const auto object=std::find_if(current.begin(),current.end(),[&](const auto& s){return s.atom==oldObject.atom && s.kind==oldObject.kind;});
            QVERIFY(object!=current.end()); QCOMPARE(object->position,oldObject.position+QPoint(turn%2,0));
            QCOMPARE(object->image.cacheKey(),oldObject.image.cacheKey());
            QCOMPARE(firstObject->position,oldObject.position); // old immutable frame remains untouched
            qInfo().noquote() << QString("[preview-profile] samples=1 sprites=%1 candidates=%2 main=%3ms total=%4ms pending=%5 previews=%6 named=%7ms")
                .arg(current.size()).arg(stats.candidates).arg(mainMs,0,'f',6).arg(totalMs,0,'f',6).arg(int(pending)).arg(previews).arg(namedMs,0,'f',6);
            bool complete=true;
            for(int i=0;i<previews;++i) complete=complete && !frames[i].sprites.empty();
            if(turn>=79 && complete) break;
        }
        qInfo().noquote() << QString("[preview-phase] duration=%1").arg(double(phaseTimer.nsecsElapsed())/1e9,0,'f',9);
        if(previews && !warm) QVERIFY(pendingTurns>1);
        if(warm) QCOMPARE(pendingTurns,0);
        for(int i=0;i<previews;++i) {
            QCOMPARE(frames[i].sprites.size(),1);
            QCOMPARE(frames[i].sprites[0].image.pixelColor(16,0),QColor(130,30,20));
        }
    }
    void generatedIconDependencies() {
        auto w = world(); w.resources = {{100,12}};
        setFields(w.appearances[1], R"({"overlays":[2]})");
        w.appearances[1].fields.iconResID = UINT32_MAX;
        std::map<uint32_t,QByteArray> data{{100,iconfixture::masked(1)}};
        auto read = [&](uint32_t id,QByteArray& out) { if(!data.count(id)) return false; out=data.at(id); return true; };
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto map=terrain.prepare(w,{},0,false);
        QVERIFY(scene.prepare(w,map,read).isEmpty());
        w.appearances[2]=w.appearances[1]; setFields(w.appearances[2], "{}"); w.appearances[2].fields.iconResID=0;
        QVERIFY(scene.prepare(w,map,read).isEmpty());
        w.resources.push_back({200,3}); QVERIFY(scene.prepare(w,map,read).isEmpty());
        QVERIFY(scene.prepare(w,map,read,0,&stats).isEmpty()); QCOMPARE(stats.templateBuilds,uint64_t(0));
        data[200]=iconfixture::sheet(); const auto first=scene.prepare(w,map,read);
        QCOMPARE(first.size(),1); QCOMPARE(first[0].image.pixelColor(20,0),QColor(Qt::red));
        QCOMPARE(scene.prepare(w,map,read,0,&stats).constData(),first.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.resourceReads,uint64_t(0));
        ++w.atoms[42].pixelX;
        const auto moved=scene.prepare(w,map,read); QCOMPARE(moved[0].image.cacheKey(),first[0].image.cacheKey());
        w.resources[1]={300,3}; QVERIFY(scene.prepare(w,map,read).isEmpty());
        data[300]=iconfixture::sheet(Qt::blue); const auto changed=scene.prepare(w,map,read);
        QCOMPARE(changed[0].image.pixelColor(20,0),QColor(Qt::blue));
        QCOMPARE(first[0].image.pixelColor(20,0),QColor(Qt::red));
        shell::EntityMap fresh; QVERIFY(samePixelsAndMetadata(changed,fresh.prepare(w,map,read)));
        // The CPU turf reference uses the same late-dependency and ordinal-remap rules.
        w.tiles[{5,5,0}]=2; shell::TurfMap cpu;
        const auto blue=cpu.compose(w,read); QCOMPARE(blue.pixelColor(52,32),QColor(Qt::blue));
        w.resources[1]={200,3}; const auto red=cpu.compose(w,read);
        QCOMPARE(red.pixelColor(52,32),QColor(Qt::red));
    }
    void decodedSceneSemantics() {
        QCOMPARE(shell::resolvePlane(-32767), 0);
        QCOMPARE(shell::resolvePlane(-32768), -1);
        QCOMPARE(shell::resolvePlane(32766), -3);
        QCOMPARE(shell::resolvePlane(-32763, 12), 16);
        QCOMPARE(shell::resolvePlane(15, 12), 15);
        const shell::SceneAppearance ap(model::appearanceOf(R"({"appearanceFlags":268435456,
            "vars":[{"bit":256,"data":"04"},{"bit":4,"data":"65"},
            {"bit":1024,"data":"0000803f000000000000000000000000000000000000803f000000000000000000000000000000000000803f00000000000000000000000000000000000000000000000000000000000000000000803f"}]})"));
        QVERIFY(ap.master); QCOMPARE(ap.blend, 4); QCOMPARE(ap.invisibility, 101);
        QCOMPARE(ap.color[15], 0.0f); QCOMPARE(ap.color[19], 1.0f);
    }
    void rgbaInheritanceResets_data() {
        QTest::addColumn<uint32_t>("flags");
        QTest::addColumn<bool>("resetColor"); QTest::addColumn<bool>("resetAlpha");
        QTest::newRow("inherit") << uint32_t(4) << false << false;
        QTest::newRow("color") << uint32_t(0x400004) << true << false;
        QTest::newRow("alpha") << uint32_t(0x800004) << false << true;
        QTest::newRow("both") << uint32_t(0xc00004) << true << true;
        // DM numeric constants are NOT the packed flags.
        QTest::newRow("not-DM-constants") << uint32_t(2 | 4) << false << false;
    }
    void rgbaInheritanceResets() {
        QFETCH(uint32_t, flags); QFETCH(bool, resetColor); QFETCH(bool, resetAlpha);
        auto w = world();
        setFields(w.appearances[1], R"({"hasColor":true,"colorR":128,"colorG":64,"colorB":32,"colorA":128,"underlays":[2],"overlays":[3]})");
        const auto child = std::string(R"({"hasColor":true,"colorR":64,"colorG":128,"colorB":255,"colorA":64,"appearanceFlags":)") + std::to_string(flags) + "}";
        for (uint32_t id : {2u,3u}) {
            w.appearances[id] = w.appearances[1]; setFields(w.appearances[id], child);
            w.appearances[id].fields.layerBits = std::bit_cast<uint32_t>(-1.0f);
        }
        const shell::SceneAppearance decoded(model::appearanceOf(child));
        QCOMPARE(decoded.resetColor, resetColor); QCOMPARE(decoded.resetAlpha, resetAlpha);
        shell::TurfMap terrain; shell::EntityMap scene;
        auto map = terrain.prepare(w, {}, 0, false);
        const auto first = scene.prepare(w, map, reader());
        QCOMPARE(first.size(), 3);
        const std::array<float,4> expected{(resetColor ? 1 : 128.f/255) * (64.f/255),
            (resetColor ? 1 : 64.f/255) * (128.f/255), (resetColor ? 1 : 32.f/255),
            (resetAlpha ? 1 : 128.f/255) * (64.f/255)};
        QCOMPARE(first.front().tint, expected); QCOMPARE(first.back().tint, expected);
        QCOMPARE(first[1].tint[0], 128.f/255); // a child's reset cannot mutate its parent/siblings
        shell::EntityMap::Stats stats;
        QCOMPARE(scene.prepare(w, map, {}, 0, &stats).constData(), first.constData());
        QCOMPARE(stats.templateBuilds, uint64_t(0)); QCOMPARE(stats.sceneReuses, uint64_t(1));
        setFields(w.appearances[3], "{}");
        const auto changed = scene.prepare(w, map, {}, 0, &stats);
        QCOMPARE(stats.templateBuilds, uint64_t(1));
        QCOMPARE(changed.back().tint, first[1].tint);
        QCOMPARE(first.back().tint, expected); // previous immutable output remains unchanged
        QCOMPARE(changed.back().image.cacheKey(), first.back().image.cacheKey());
    }
    void resetBranchesAndGroupBoundary() {
        auto w = world();
        setFields(w.appearances[1], R"({"hasColor":true,"colorR":128,"colorG":64,"colorB":32,"colorA":128,"overlays":[2,4]})");
        for (uint32_t id : {2u,3u,4u}) {
            w.appearances[id] = w.appearances[1];
            w.appearances[id].fields.layerBits = std::bit_cast<uint32_t>(-1.f);
        }
        setFields(w.appearances[2], R"({"appearanceFlags":12582912,"overlays":[3]})");
        setFields(w.appearances[3], "{}"); setFields(w.appearances[4], "{}");
        shell::TurfMap terrain; shell::EntityMap scene;
        auto map = terrain.prepare(w, {}, 0, false);
        const auto plain = scene.prepare(w,map,reader());
        QCOMPARE(plain.size(),4);
        const std::array<float,4> white{1,1,1,1};
        QCOMPARE(plain[1].tint,white); QCOMPARE(plain[2].tint,white);
        QCOMPARE(plain[3].tint,plain[0].tint);
        // Resets cannot escape the enclosing GPU material operation.
        setFields(w.appearances[1], fieldsJson(w.appearances[1]).insert(1, "\"appearanceFlags\":67108864,"));
        auto grouped = scene.prepare(w,map,{});
        QCOMPARE(grouped.size(),4);
        QCOMPARE(grouped[1].tint,white); QCOMPARE(grouped[2].tint,white);
        QVERIFY(grouped[1].appearanceGroup);
        QCOMPARE(grouped[1].appearanceGroup->tint,plain[0].tint);
        setFields(w.appearances[2], R"({"appearanceFlags":146800640,"overlays":[3]})"); // KEEP_APART + both resets
        auto apart = scene.prepare(w,map,{});
        QCOMPARE(apart[1].tint,white); QCOMPARE(apart[2].tint,white);
        QCOMPARE(apart[3].tint,white); QVERIFY(!apart[1].appearanceGroup); QVERIFY(apart[3].appearanceGroup);
        QCOMPARE(apart[3].appearanceGroup->tint,plain[0].tint);
        // Late/reset changes must agree with a fresh extraction, including ancestor changes.
        shell::EntityMap fresh;
        QVERIFY(samePixelsAndMetadata(apart,fresh.prepare(w,map,reader())));
    }
    void resetAlphaRevivesLateChild() {
        auto w = world();
        setFields(w.appearances[1], R"({"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":0,"overlays":[2]})");
        shell::TurfMap terrain; shell::EntityMap scene;
        auto map = terrain.prepare(w,{},0,false);
        const auto hidden=scene.prepare(w,map,reader()); QCOMPARE(hidden.size(),1);
        QCOMPARE(hidden[0].tint[3],0.f); // still carries source-alpha input
        w.appearances[2] = w.appearances[1];
        setFields(w.appearances[2], R"({"appearanceFlags":8388608,"pixelX":32})");
        w.appearances[2].fields.iconResID = 1; w.resources.push_back({456,0});
        int attempts = 0;
        auto missing = [&](uint32_t, QByteArray&) { ++attempts; return false; };
        QCOMPARE(scene.prepare(w,map,missing).size(),1);
        QCOMPARE(scene.prepare(w,map,missing).size(),1); QCOMPARE(attempts,2);
        const auto visible = scene.prepare(w,map,reader()); QCOMPARE(visible.size(),2);
        QCOMPARE(visible[1].tint[3],1.f);
        QCOMPARE(visible[1].position-map.source.topLeft(),QPoint(64,32));
        auto hit = shell::mapPointerAt({80,48},{96,96},{3,3},map.source,map.geometry,"map",true,visible);
        QVERIFY(hit); QCOMPARE(hit->click.over.ref,uint32_t(42));
        setFields(w.appearances[2], "{}");
        const auto transparent=scene.prepare(w,map,{}); QCOMPARE(transparent.size(),2);
        for(const auto& part:transparent) QCOMPARE(part.tint[3],0.f);
    }
    void unifiedTerrainOrderingAndLateChildren() {
        auto w = world();
        w.tiles[{5,5,0}] = 2;
        w.appearances[2] = w.appearances[1];
        setFields(w.appearances[2], R"({"hasPlane":true,"plane":-32768,"overlays":[3]})");
        w.appearances[2].fields.layerBits = std::bit_cast<uint32_t>(2.0f);
        shell::TurfMap terrain; shell::EntityMap scene;
        auto map = terrain.prepare(w, {}, 0, false);
        auto first = scene.prepare(w, map, reader(), 0, nullptr, true);
        QCOMPARE(first.size(), 2);
        QCOMPARE(first[0].kind, shell::MapSprite::Turf);
        QCOMPARE(first[0].plane, -1);
        w.appearances[3] = w.appearances[1];
        setFields(w.appearances[3], R"({"hasPlane":true,"plane":-32765})");
        w.appearances[3].fields.iconState = "green";
        const auto next = scene.prepare(w, map, reader(), 0, nullptr, true);
        QCOMPARE(next.size(), 3);
        QCOMPARE(next[1].kind, shell::MapSprite::Entity);
        QCOMPARE(next[2].kind, shell::MapSprite::Turf);
        QCOMPARE(next[2].plane, 1); // parent -1 + relative 2
        auto hit = shell::mapPointerAt({48,48}, {96,96}, {3,3}, map.source, map.geometry, "map", true, next);
        QVERIFY(hit); QCOMPARE(hit->click.over.kind, proto::enc::RefTurf);
        shell::EntityMap::Stats stats;
        auto idle = scene.prepare(w, map, {}, 0, &stats, true);
        QCOMPARE(idle.constData(), next.constData()); QCOMPARE(stats.sceneReuses, uint64_t(1));
        QCOMPARE(stats.templateBuilds, uint64_t(0)); QCOMPARE(stats.sortedSprites, uint64_t(0));
        w.appearances[3].fields.layerBits = std::bit_cast<uint32_t>(7.0f);
        QCOMPARE(scene.prepare(w, map, {}, 0, nullptr, true).back().layer, 7.0f);
        QCOMPARE(first.size(), 2); // immutable previous frame
    }
    void unifiedCachedMatchesFresh() {
        auto w = world();
        w.tiles[{5,5,0}] = 2; w.appearances[2] = w.appearances[1];
        w.hudOrder = {7}; w.hudAppid[7] = 3;
        setFields(w.appearances[3], R"({"appearanceFlags":268435456,"renderTarget":"*plane"})");
        shell::TurfMap terrain; shell::EntityMap cached;
        for (int i = 0; i < 48; ++i) {
            switch (i % 6) {
            case 0: setFields(w.appearances[2], i % 12 ? R"({"overlays":[9]})" : R"({"hasPlane":true,"plane":32766})"); break;
            case 1: w.appearances[9] = w.appearances[1]; w.appearances[9].fields.iconState = "green"; break;
            case 2: w.appearances[2].fields.layerBits = std::bit_cast<uint32_t>(float(i % 5)); break;
            case 3: w.appearances.erase(9); w.atoms[42].x = 5 + i % 3; break;
            case 4: w.seeInvisible = uint8_t(i); setFields(w.appearances[3], R"({"appearanceFlags":268435456,"vars":[{"bit":256,"data":"04"}]})"); break;
            case 5: setFields(w.appearances[3], R"({"appearanceFlags":268435456,"renderTarget":"*new"})"); break;
            }
            const auto map = terrain.prepare(w, {}, 0, false);
            shell::EntityMap fresh;
            QVERIFY(samePixelsAndMetadata(cached.prepare(w,map,reader(),0,nullptr,true),
                                          fresh.prepare(w,map,reader(),0,nullptr,true)));
        }
    }
    void turfColumnLookupArrivalAndMutation() {
        auto w = world(); w.atoms.clear();
        w.bounds.maxX = w.bounds.maxY = 14;
        w.appearances[1].fields.iconState = "";
        w.appearances[2] = w.appearances[1];
        setFields(w.appearances[2], R"({"dir":4})");
        w.appearances[2].fields.layerBits = std::bit_cast<uint32_t>(7.f);
        const auto bytes = iconfixture::animation(1);
        const auto sheet = QImage::fromData(bytes).convertToFormat(QImage::Format_RGBA8888);
        auto read = [&](uint32_t, QByteArray& out) { out = bytes; return true; };
        // A full source rectangle makes bounds clipping observable, not hidden
        // by off-view culling. Later checks independently move/crop this geometry.
        shell::TurfMap::Frame map;
        map.geometry = {QRect(4,4,11,11), QSize(14,14), 32, 0, 0};
        map.image = QImage(352,352,QImage::Format_RGBA8888); map.image.fill(Qt::black);
        map.source = map.image.rect();
        for (int y = 4; y <= 13; ++y) w.tiles[{8,uint16_t(y),0}] = 1;
        for (int y : {4,9,13}) w.tiles[{9,uint16_t(y),0}] = 1; // sparse, not a fallback trigger
        for (int y = 5; y <= 11; ++y) w.tiles[{10,uint16_t(y),0}] = 2;
        // More than four stored keys between successive Y queries forces the
        // fallback in BOTH tables; coordinate gaps alone do not do that.
        for (uint16_t z = 1; z <= 12; ++z) {
            w.tiles[{8,8,z}] = 2;
            w.tileIconStart[{8,8,z}] = {2,2500000000};
        }
        w.tiles[{14,6,0}] = 1; // in source but beyond world maxX
        w.tiles[{7,6,0}] = 4;  // missing appearance, arrives later
        w.tileIconStart[{8,4,0}] = {1,2500000000};
        w.tileIconStart[{8,9,0}] = {1,2500000000};
        w.tileIconStart[{10,10,0}] = {2,2500000000};
        constexpr int64_t now = 2600000000;
        uint64_t cost = 1;
        // Independent oracle for this deliberately simple fixture: conventional
        // tree lookups, known DMI cells, explicit placement/order and visit cost.
        // It uses neither EntityMap nor its column cursor/template/frame caches.
        auto reference = [&] {
            shell::MapSprites expected;
            uint64_t budget = 65536;
            const auto region = map.geometry.region;
            const QPoint translation(-region.x()*32,(region.y()+region.height())*32);
            for (int y = region.top(); y <= region.bottom(); ++y)
                for (int x = region.left(); x <= region.right(); ++x) {
                    if (x < 0 || y < 0 || x >= w.bounds.maxX || y >= w.bounds.maxY || !budget) continue;
                    const std::array<uint16_t,3> key{uint16_t(x),uint16_t(y),uint16_t(map.geometry.z)};
                    const auto tile = w.tiles.find(key);
                    if (tile == w.tiles.end()) continue;
                    const auto ap = w.appearances.find(tile->second);
                    const uint64_t visits = ap == w.appearances.end() ? 1 : cost;
                    if (visits > budget) { budget = 0; continue; }
                    budget -= visits;
                    if (ap == w.appearances.end()) continue;
                    const auto timing = w.tileIconStart.find(key);
                    const int frame = timing != w.tileIconStart.end() && timing->second.ns == 2500000000 ? 1 : 2;
                    const bool east = tile->second == 2;
                    shell::MapSprite s;
                    s.image = sheet.copy(east ? 64 : 0,frame*32,32,32);
                    s.position = QPoint(x*32,-(y+1)*32)+translation;
                    s.iconBottom = s.position+QPoint(0,32); s.cell = {x+1,y+1}; s.sortCell=s.cell;
                    s.atom = (uint32_t(y)<<16)|uint32_t(x);
                    s.appearanceId = tile->second;
                    s.layer = east ? 7.f : 3.f; s.kind = shell::MapSprite::Turf; s.size = {32,32};
                    if (s.rect().intersects(map.source)) expected.push_back(std::move(s));
                }
            std::stable_sort(expected.begin(),expected.end(),[](const auto& a,const auto& b) {
                return std::tuple(a.layer,-a.cell.y(),-a.cell.x()) < std::tuple(b.layer,-b.cell.y(),-b.cell.x());
            });
            return expected;
        };
        shell::EntityMap scene;
        std::vector<std::pair<shell::MapSprites,shell::MapSprites>> retained;
        auto check = [&] {
            const auto expected = reference();
            const auto actual = scene.prepare(w,map,read,now,nullptr,true);
            QVERIFY(samePixelsAndMetadata(actual,expected));
            shell::EntityMap fresh; // NEW cache on every mutation, not a second warm cache
            QVERIFY(samePixelsAndMetadata(actual,fresh.prepare(w,map,read,now,nullptr,true)));
            shell::EntityMap::Stats stats;
            const auto repeated = scene.prepare(w,map,read,now,&stats,true);
            QVERIFY(repeated == actual); QCOMPARE(stats.sceneReuses,uint64_t(1));
            // Oracle images are independent allocations; check all pixels and
            // metadata of every retained publication after every later mutation.
            retained.emplace_back(actual,expected);
            for (const auto& [published,snapshot] : retained) QVERIFY(samePixelsAndMetadata(published,snapshot));
        };
        check();
        w.tiles.erase({8,9,0}); check();
        w.tiles[{8,9,0}] = 2; check(); // changes pixels (direction) AND layer/order
        w.tileIconStart.erase({8,9,0}); w.tileIconStart[{9,9,0}] = {1,2500000000}; check();
        w.appearances[4] = w.appearances[1]; check();
        w.appearances.erase(4); check();
        w.tiles[{6,7,0}] = 1; check();
        map.geometry.z = 5; check();
        w.tiles[{9,9,5}] = 1; check();
        map.geometry.z = 0; map.source = QRect(96,96,160,160); check();
        map.geometry.region.translate(-6,-6); map.source = map.image.rect(); check();
        map.geometry.region.translate(9,9); check();
        // Replace the world while reusing the same renderer and overlapping IDs.
        w = world(); w.atoms.clear(); w.appearances[1].fields.iconState = "";
        w.tiles[{8,8,0}] = 1; check();
        // Budget admission must retain row-major traversal, even though final
        // draw order runs north-to-south. Missing children consume visits but
        // produce no parts: 4096 visits/root admits exactly the first 16 cells.
        map.geometry.region = QRect(4,4,5,5); map.source = QRect(0,0,160,160);
        w.tiles.clear(); w.tileIconStart.clear();
        for (uint16_t y = 4; y < 9; ++y) for (uint16_t x = 4; x < 9; ++x) w.tiles[{x,y,0}] = 1;
        std::string children = "{\"overlays\":[999";
        for (int i = 1; i < 4095; ++i) children += ",999";
        setFields(w.appearances[1], children+"]}"); cost = 4096;
        QCOMPARE(reference().size(),16); check();
        w.tiles.erase({4,4,0}); QCOMPARE(reference().size(),16); check();
    }
    void invisibleAndIconlessMasters() {
        auto w = world();
        setFields(w.appearances[1], R"({"vars":[{"bit":4,"data":"65"}]})");
        shell::TurfMap terrain; shell::EntityMap scene;
        auto map = terrain.prepare(w, {}, 0, false);
        QVERIFY(scene.prepare(w, map, reader(), 0, nullptr, true).isEmpty());
        w.seeInvisible = 101;
        QCOMPARE(scene.prepare(w, map, reader(), 0, nullptr, true).size(), 1);
        w.hudOrder = {7}; w.hudAppid[7] = 2;
        setFields(w.appearances[2], R"({"appearanceFlags":268435456,"screenLoc":"1,1","hasPlane":true,"plane":-32767,
            "renderTarget":"*capture","hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":0,
            "vars":[{"bit":256,"data":"04"}]})");
        auto sprites = scene.prepare(w, map, reader(), 0, nullptr, true);
        QCOMPARE(sprites.size(), 2);
        auto master = std::find_if(sprites.cbegin(), sprites.cend(), [](const auto& s) { return s.kind == shell::MapSprite::Master; });
        QVERIFY(master != sprites.cend()); QVERIFY(master->image.isNull());
        QCOMPARE(master->blend, 4); QCOMPARE(master->tint[3], 0.0f);
        QCOMPARE(master->renderTarget, QString("*capture"));
        auto hit = shell::mapPointerAt({48,48}, {96,96}, {3,3}, map.source, map.geometry, "map", true, sprites);
        // Stock keeps original hits for an unconsumed hidden producer; master
        // alpha is presentation-only, not a reason to fall back to the turf.
        QVERIFY(hit); QCOMPARE(hit->click.over.kind, proto::enc::RefObj);
        QCOMPARE(hit->click.over.ref, uint32_t(42));
    }
    void lifecycleAndLateResources() {
        auto w = world();
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        int reads = 0;
        shell::TurfMap::Read unavailable = [&](uint32_t, QByteArray&) { ++reads; return false; };
        QVERIFY(entities.prepare(w, map, unavailable).isEmpty());
        QVERIFY(entities.prepare(w, map, unavailable).isEmpty());
        QCOMPARE(reads, 2);
        auto sprites = entities.prepare(w, map, reader());
        QCOMPARE(sprites.size(), 1);
        QCOMPARE(sprites[0].position, map.source.topLeft() + QPoint(32, 32));
        QCOMPARE(sprites[0].atom, uint32_t(42));
        QCOMPARE(sprites[0].key, uint16_t(99));
        auto idle = entities.prepare(w, map, {});
        QVERIFY(idle == sprites);
        QCOMPARE(idle[0].image.cacheKey(), sprites[0].image.cacheKey());
        w.atoms[42].x++;
        auto movedMap = terrain.prepare(w, {});
        QCOMPARE(movedMap.image.cacheKey(), map.image.cacheKey());
        auto moved = entities.prepare(w, movedMap, {});
        QCOMPARE(moved[0].position, sprites[0].position + QPoint(32, 0));
        QCOMPARE(moved[0].image.cacheKey(), sprites[0].image.cacheKey());
        w.resync.freed.insert(42);
        QVERIFY(entities.prepare(w, map, {}).isEmpty());
        w.resync.freed.clear();
        w.atoms[42].hasPos = false;
        QVERIFY(entities.prepare(w, map, {}).isEmpty());
        w.atoms[42].hasPos = true;
        w.atoms[42].z = 1;
        QVERIFY(entities.prepare(w, map, {}).isEmpty());
        w.atoms[42].z = 0;
        w.atoms.erase(42);
        QVERIFY(entities.prepare(w, map, {}).isEmpty());
        // Existing mailbox values remain immutable after the worker's state changes.
        QCOMPARE(sprites[0].position, map.source.topLeft() + QPoint(32, 32));
    }
    void overlaysOrderingAndPicking() {
        auto w = world();
        setFields(w.appearances[1], "{\"overlays\":[2]}");
        auto& overlay = w.appearances[2];
        setFields(overlay, "{\"pixelX\":16,\"overlays\":[1]}"); // cycle terminates
        overlay.fields.iconState = "green"; overlay.fields.layerBits = std::bit_cast<uint32_t>(-1.0f);
        w.atoms[model::kRegistryA | 7] = w.atoms[42];
        w.atoms[model::kRegistryA | 7].appid = 3;
        w.appearances[3] = w.appearances[1];
        setFields(w.appearances[3], "{\"hasPlane\":true,\"plane\":-1}");
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        auto sprites = entities.prepare(w, map, reader());
        QCOMPARE(sprites.size(), 3);
        QCOMPARE(sprites[0].atom, model::kRegistryA | 7);
        QCOMPARE(sprites[1].image.pixelColor(1, 1), QColor(Qt::red));
        QCOMPARE(sprites[2].image.pixelColor(1, 1), QColor(Qt::green));
        QCOMPARE(sprites[2].position, sprites[1].position + QPoint(16, 0));
        auto pick = [&](QPointF point) {
            return shell::mapPointerAt(point, {192, 96}, {3, 3}, map.source, map.geometry, "main.map", true, sprites);
        };
        auto hit = pick({48 + 32 + 20, 32 + 5});
        QVERIFY(hit.has_value());
        QCOMPARE(hit->click.over.kind, uint8_t(proto::enc::RefObj));
        QCOMPARE(hit->click.over.ref, uint32_t(42));
        QCOMPARE(hit->click.iconX, 21); // root-relative, not overlay-local
        QCOMPARE(hit->click.iconY, 27);
        QCOMPARE(hit->click.loc.x, uint16_t(5));
        QCOMPARE(hit->atomKey, uint16_t(99));
        QVERIFY(hit->haveAtomKey);
        auto hole = pick({48 + 32, 32});
        QCOMPARE(hole->click.over.kind, uint8_t(proto::enc::RefTurf));
        auto letterbox = pick({1, 40});
        QCOMPARE(letterbox->click.over.kind, uint8_t(proto::enc::RefNull));
        setFields(w.appearances[3], "{\"hasPlane\":true,\"plane\":1}");
        sprites = entities.prepare(w, map, {});
        QCOMPARE(sprites.back().atom, model::kRegistryA | 7);
        hit = pick({48 + 32 + 20, 32 + 5});
        QCOMPARE(hit->click.over.kind, uint8_t(proto::enc::RefMob));
        QCOMPARE(hit->click.over.ref, uint32_t(7));
        // Tint changes draw data, not immutable texture pixels or source-alpha hits.
        auto imageKey = sprites.back().image.cacheKey();
        setFields(w.appearances[3], "{\"hasPlane\":true,\"plane\":1,\"hasColor\":true,\"colorR\":128,\"colorG\":255,\"colorB\":255,\"colorA\":128}");
        sprites = entities.prepare(w, map, {});
        QCOMPARE(sprites.back().image.cacheKey(), imageKey);
        QCOMPARE(sprites.back().tint[3], 128.0f / 255);
        setFields(w.appearances[3], "{\"hasColor\":true,\"colorA\":0}");
        sprites = entities.prepare(w, map, {});
        QCOMPARE(sprites.size(), 3);
        QCOMPARE(pick({48+32+20,32+5})->click.over.kind,uint8_t(proto::enc::RefMob));
    }
    void mobDirectionUpdateToDmi() {
        model::Session session; session.worldMut() = world(); auto& w = session.worldMut();
        const uint32_t mob = model::kRegistryA | 42;
        w.atoms[mob] = w.atoms.at(42); w.atoms.erase(42);
        w.appearances[1].fields.iconState = "missing"; // ordinary default-state fallback
        setFields(w.appearances[1], R"({"dir":2,"overlays":[2]})");
        w.appearances[2] = w.appearances[1]; w.appearances[2].fields.iconResID = 1;
        setFields(w.appearances[2], R"({"dir":2})");
        w.resources.push_back({456,0});
        const auto four = iconfixture::animation(), eight = iconfixture::animation(0,false,8);
        auto read = [&](uint32_t id,QByteArray& out) { out = id == 123 ? four : eight; return true; };
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w,{},0,false);
        const uint8_t directions[] = {2,1,4,8,6,10,5,9};
        const int cells[] = {0,1,2,3,2,3,2,3};
        for (int i = 0; i < 8; ++i) {
            // 0x0007: empty eye/new/pos/bind, one keyed mob direction, no appearance bind.
            const std::array<uint8_t,13> update{0,0,0,0,1,42,0,0,0,99,0,directions[i],0};
            session.ingest(0x0007,update);
            QCOMPARE(session.ingestErrors(),0);
            QCOMPARE(w.atoms.at(mob).dir,int(directions[i]));
            const auto sprites = scene.prepare(w,map,read,w.iconClockNs+200000000); // frames count from the first message
            QCOMPARE(sprites.size(),2);
            QCOMPARE(sprites[0].image.pixelColor(16,0),QColor(210,30+20*cells[i],20));
            // A four-direction root must not discard the diagonal inherited by
            // an eight-direction child, including on later animation frames.
            QCOMPARE(sprites[1].image.pixelColor(16,0),QColor(210,30+20*i,20));
        }
    }
    void overlayDirectionInheritance_data() {
        QTest::addColumn<int>("parentDir");
        QTest::newRow("south") << 2; QTest::newRow("north") << 1;
        QTest::newRow("east") << 4; QTest::newRow("west") << 8;
        QTest::newRow("southeast") << 6; QTest::newRow("southwest") << 10;
        QTest::newRow("northeast") << 5; QTest::newRow("northwest") << 9;
    }
    void overlayDirectionInheritance() {
        QFETCH(int,parentDir);
        auto w = world();
        QImage sheet(128,32,QImage::Format_RGBA8888);
        const QColor colors[] = {Qt::red,Qt::green,Qt::blue,Qt::yellow};
        for (int y=0; y<32; ++y) for (int x=0; x<128; ++x) sheet.setPixelColor(x,y,colors[x/32]);
        sheet.setText("Description","width = 32\nheight = 32\nstate = \"\"\ndirs = 4\nframes = 1\n");
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); QVERIFY(sheet.save(&buffer,"PNG"));
        auto read = [&](uint32_t, QByteArray& out) { out = bytes; return true; };
        w.appearances[1].fields.iconState = "";
        setFields(w.appearances[1], R"({"dir":2,"underlays":[2],"overlays":[3,5]})");
        for (uint32_t id : {2u,3u,4u,5u}) {
            w.appearances[id] = w.appearances[1];
            w.appearances[id].fields.layerBits = std::bit_cast<uint32_t>(-1.f);
        }
        setFields(w.appearances[2], R"({"dir":2})"); // decoded SOUTH, but not fixed
        setFields(w.appearances[3], R"({"dir":1,"appearanceFlags":131588,"overlays":[4]})"); // captured 0x20204
        setFields(w.appearances[4], R"({"dir":8})"); // inherits its immediate fixed-direction parent
        setFields(w.appearances[5], R"({"dir":2})");
        w.atoms[42].dir = parentDir;
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto map = terrain.prepare(w,{},0,false);
        QVERIFY(scene.prepare(w,map,{}).isEmpty()); // late resource retry preserves directional dependencies
        const auto first = scene.prepare(w,map,read);
        QCOMPARE(first.size(),5);
        const auto expected = colors[parentDir==2 ? 0 : parentDir==1 ? 1 : (parentDir & 4) ? 2 : 3];
        for (int i : {0,1,4}) QCOMPARE(first[i].image.pixelColor(16,16),expected);
        for (int i : {2,3}) QCOMPARE(first[i].image.pixelColor(16,16),QColor(Qt::green));
        shell::EntityMap::Stats stats;
        QCOMPARE(scene.prepare(w,map,{},0,&stats).constData(),first.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.sceneReuses,uint64_t(1));
        // Explicit SOUTH is fixed too. Changing only a descendant's flag must
        // invalidate the root template, without changing already-published frames.
        setFields(w.appearances[3], R"({"dir":2,"appearanceFlags":131588,"overlays":[4]})");
        auto fixed = scene.prepare(w,map,{},0,&stats);
        QCOMPARE(stats.templateBuilds,uint64_t(1));
        for (int i : {2,3}) QCOMPARE(fixed[i].image.pixelColor(16,16),QColor(Qt::red));
        setFields(w.appearances[3], R"({"dir":2,"appearanceFlags":516,"overlays":[4]})"); // 0x200 alone is not fixedDir
        auto inherited = scene.prepare(w,map,{},0,&stats);
        QCOMPARE(stats.templateBuilds,uint64_t(1));
        for (const auto& s : inherited) QCOMPARE(s.image.pixelColor(16,16),expected);
        QCOMPARE(first[2].image.pixelColor(16,16),QColor(Qt::green));
        shell::EntityMap fresh;
        QVERIFY(samePixelsAndMetadata(inherited,fresh.prepare(w,map,read)));
        // Root direction changes are part of the template key. Shared appearances
        // on differently facing atoms must not share the wrong expanded template.
        w.atoms[43] = w.atoms[42]; w.atoms[43].dir = 8;
        const auto shared = scene.prepare(w,map,read);
        QCOMPARE(shared.size(),10);
        for (const auto& s : shared) QCOMPARE(s.image.pixelColor(16,16),s.atom==43 ? QColor(Qt::yellow) : expected);
        const shell::SceneAppearance a(model::appearanceOf(R"({"appearanceFlags":131588})"));
        const shell::SceneAppearance b(model::appearanceOf(R"({"appearanceFlags":516})"));
        QVERIFY(a.fixedDir); QVERIFY(!b.fixedDir);
    }
    void gliding_data() {
        QTest::addColumn<int>("dx"); QTest::addColumn<int>("dy");
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) if (x || y)
            QTest::newRow(qPrintable(QString("%1,%2").arg(x).arg(y))) << x << y;
    }
    void gliding() {
        QFETCH(int, dx); QFETCH(int, dy);
        auto w = world();
        w.haveEye = true; w.eyeX = w.eyeY = 6;
        w.atoms[42].gliding = w.eyeGliding = true;
        constexpr int64_t start = 1'000'000'000;
        w.eyeGlide = model::glideMotion(-dx, -dy, 32, 32, 100'000'000, false, start, false);
        w.atoms[42].glide = w.eyeGlide;
        shell::TurfMap terrain;
        shell::EntityMap entities;
        qint64 terrainKey = 0, iconKey = 0;
        for (int step = 0; step <= 4; ++step) {
            auto now = start + w.eyeGlide.durNs * step / 4;
            auto map = terrain.prepare(w, {}, now);
            auto sprites = entities.prepare(w, map, reader(), now);
            QCOMPARE(sprites.size(), 1);
            QCOMPARE(sprites[0].position - map.source.topLeft(), QPoint(32, 32)); // camera and self never drift
            if (!step) { terrainKey = map.image.cacheKey(); iconKey = sprites[0].image.cacheKey(); }
            QCOMPARE(map.image.cacheKey(), terrainKey);
            QCOMPARE(sprites[0].image.cacheKey(), iconKey);
        }
        // Interrupted movement carries the remaining offset; both use the same ingest clock.
        double sx = 0, sy = 0;
        uint8_t dir = w.eyeGlideDir;
        QVERIFY(model::armGlideOffset(w, &w.eyeGlide, start + w.eyeGlide.durNs / 2, -dx, -dy, dir, sx, sy));
        w.eyeGlide = model::glideMotion(sx, sy, 32, 32, 100'000'000, false, start + w.eyeGlide.durNs / 2, false);
        w.atoms[42].glide = w.eyeGlide;
        auto map = terrain.prepare(w, {}, w.eyeGlide.startNs);
        auto sprites = entities.prepare(w, map, {}, w.eyeGlide.startNs);
        QCOMPARE(sprites[0].position - map.source.topLeft(), QPoint(32, 32));
        // NPC movement without camera movement reaches the same destination and settles.
        w.eyeGliding = false;
        map = terrain.prepare(w, {});
        auto settled = entities.prepare(w, map, {}, w.eyeGlide.startNs + w.eyeGlide.durNs);
        QCOMPARE(settled[0].position - map.source.topLeft(), QPoint(32, 32));
        QVERIFY(settled == entities.prepare(w, map, {}, w.eyeGlide.startNs + w.eyeGlide.durNs * 2));
    }
    void layerAndUnderlayOrder() {
        auto w = world();
        setFields(w.appearances[1], "{\"underlays\":[2],\"overlays\":[3]}");
        for (uint32_t id : {2u, 3u}) {
            w.appearances[id] = w.appearances[1];
            setFields(w.appearances[id], "{}");
            w.appearances[id].fields.layerBits = std::bit_cast<uint32_t>(-1.0f);
            w.appearances[id].fields.iconState = "green";
        }
        w.atoms[43] = w.atoms[42]; w.atoms[43].appid = 4;
        w.appearances[4] = w.appearances[1];
        setFields(w.appearances[4], "{}");
        w.appearances[4].fields.layerBits = std::bit_cast<uint32_t>(2.0f);
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        auto sprites = entities.prepare(w, map, reader());
        QCOMPARE(sprites.size(), 4);
        QCOMPARE(sprites[0].atom, uint32_t(43));
        QCOMPARE(sprites[1].image.pixelColor(1, 1), QColor(Qt::green));
        QCOMPARE(sprites[2].image.pixelColor(1, 1), QColor(Qt::red));
        QCOMPARE(sprites[3].image.pixelColor(1, 1), QColor(Qt::green));
        w.appearances[4].fields.layerBits = std::bit_cast<uint32_t>(4.0f);
        QCOMPARE(entities.prepare(w, map, {}).back().atom, uint32_t(43));
        // Non-finite layer input cannot break strict weak ordering.
        w.appearances[4].fields.layerBits = 0x7fc00000u;
        QCOMPARE(entities.prepare(w, map, {}).front().atom, uint32_t(43));
    }
    void directionsOffsetsAndLargeIcons() {
        auto w = world();
        QImage image(64, 64, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        for (int y = 0; y < 64; ++y) for (int x = 32; x < 64; ++x) image.setPixelColor(x, y, Qt::green);
        image.setText("Description", "width = 16\nheight = 64\nstate = \"red\"\ndirs = 4\nframes = 1\n");
        QByteArray bytes;
        QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer, "PNG"));
        auto read = [&](uint32_t, QByteArray& out) { out = bytes; return true; };
        auto& atom = w.atoms[42];
        atom.dir = 4; atom.pixelX = 2; atom.pixelY = 3; atom.stepX = 4; atom.stepY = 5;
        setFields(w.appearances[1], "{\"dir\":2,\"pixelX\":6,\"pixelY\":7}");
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        auto sprites = entities.prepare(w, map, read);
        QCOMPARE(sprites.size(), 1);
        QCOMPARE(sprites[0].image.size(), QSize(16, 64));
        QCOMPARE(sprites[0].image.pixelColor(1, 1), QColor(Qt::green)); // atom direction overrides appearance
        QCOMPARE(sprites[0].position - map.source.topLeft(), QPoint(44, -15));
        QCOMPARE(sprites[0].iconBottom - map.source.topLeft(), QPoint(44, 49));
        // Frame direction changes do not invalidate the terrain cache.
        atom.dir = 1;
        sprites = entities.prepare(w, map, {});
        QCOMPARE(sprites[0].image.pixelColor(1, 1), QColor(Qt::red));
        QCOMPARE(terrain.prepare(w, {}).image.cacheKey(), map.image.cacheKey());
        w.viewZ = 1;
        auto otherLevel = terrain.prepare(w, {});
        QVERIFY(entities.prepare(w, otherLevel, {}).isEmpty());
    }
    void missingAppearanceAndOffsetIcon() {
        auto w = world();
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        w.atoms[42].appid = 100;
        QVERIFY(entities.prepare(w, map, reader()).isEmpty());
        w.appearances[100] = w.appearances[1];
        QCOMPARE(entities.prepare(w, map, reader()).size(), 1);
        w.appearances[100].fields.iconState = "absent";
        QVERIFY(entities.prepare(w, map, {}).isEmpty());
        w.appearances[100].fields.iconState = "red";
        setFields(w.appearances[100], "{\"pixelX\":-64}");
        w.atoms[42].x = 8; // destination outside view; offset pixels are inside
        auto sprites = entities.prepare(w, map, {});
        QCOMPARE(sprites.size(), 1);
        QCOMPARE(sprites[0].position - map.source.topLeft(), QPoint(32, 32));
        QVERIFY(entities.prepare(w, {}, {}).isEmpty());
    }
    void templateInvalidation() {
        auto w = world();
        setFields(w.appearances[1], "{\"overlays\":[2,3]}");
        w.appearances[2] = w.appearances[1];
        setFields(w.appearances[2], "{\"pixelX\":8,\"overlays\":[1]}"); // cyclic dependency is path-local
        w.appearances[2].fields.iconState = "green";
        w.appearances[2].fields.layerBits = std::bit_cast<uint32_t>(-1.0f);
        w.atoms[43] = w.atoms[42]; w.atoms[43].appid = 2;
        w.atoms[44] = w.atoms[42]; w.atoms[44].dir = 4;
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        auto read = reader();
        auto equivalent = [&] {
            auto cached = entities.prepare(w, map, read);
            shell::EntityMap fresh;
            return samePixelsAndMetadata(cached, fresh.prepare(w, map, read));
        };
        QVERIFY(equivalent());
        setFields(w.appearances[2], "{\"pixelX\":-16,\"hasPlane\":true,\"plane\":2}");
        QVERIFY(equivalent());
        w.appearances[2].fields.iconState = "red"; // scalar changes without changing JSON
        QVERIFY(equivalent());
        w.appearances[2].fields.layerBits = std::bit_cast<uint32_t>(7.0f);
        QVERIFY(equivalent());
        w.appearances[3] = w.appearances[2]; // previously unknown descendant arrives
        QVERIFY(equivalent());
        w.appearances.erase(2);
        QVERIFY(equivalent());
        w.appearances[2] = w.appearances[3];
        QVERIFY(equivalent());
        setFields(w.appearances[2], "{}");
        QVERIFY(equivalent());
        w.appearances[1].fields.iconResID = 4; // late resource ordinal, including while nothing can draw
        QVERIFY(equivalent());
        w.resources.resize(5, {123, 0});
        QVERIFY(equivalent());
        w.resources[0].first = 456; // same pixels, different resource mapping
        QVERIFY(equivalent());
        w.atoms[42].key++;
        QVERIFY(equivalent());
        w.atoms[42].pixelX = 1000; // root and children outside the viewport
        QVERIFY(equivalent());
        w.atoms.erase(43);
        QVERIFY(equivalent());
        w.resync.freed.insert(44);
        QVERIFY(equivalent());
        w.resync.freed.clear();
        QVERIFY(equivalent());
        w.atoms[44].z = 1;
        QVERIFY(equivalent());
        w.viewZ = 1; map = terrain.prepare(w, {});
        QVERIFY(equivalent());
        w.stepSizeBits = std::bit_cast<uint32_t>(16.0f); map = terrain.prepare(w, {});
        QVERIFY(equivalent());
    }
    void sharedResourceRetryAndBounds() {
        auto w = world();
        setFields(w.appearances[1], "{\"overlays\":[2]}");
        w.appearances[2] = w.appearances[1]; setFields(w.appearances[2], "{\"pixelX\":64}");
        w.appearances[2].fields.iconResID = 1; w.appearances[2].fields.iconState = "green";
        w.appearances[2].fields.layerBits = std::bit_cast<uint32_t>(-1.0f);
        w.resources.push_back({456, 0});
        // Root is outside view; only its descendant's offset intersects. Its resource is initially absent.
        w.atoms[42].x = 3;
        w.atoms[43] = w.atoms[42];
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        bool available = false;
        int requests = 0;
        auto read = [&](uint32_t resource, QByteArray& bytes) {
            if (resource == 456) { ++requests; if (!available) return false; }
            bytes = iconBytes(); return true;
        };
        shell::EntityMap::Stats stats;
        QVERIFY(entities.prepare(w, map, read, 0, &stats).isEmpty());
        QCOMPARE(requests, 1); QCOMPARE(stats.culled, uint64_t(2));
        QVERIFY(entities.prepare(w, map, read, 0, &stats).isEmpty());
        QCOMPARE(requests, 2); QCOMPARE(stats.templateBuilds, uint64_t(0));
        available = true;
        auto visible = entities.prepare(w, map, read, 0, &stats);
        QCOMPARE(requests, 3); QCOMPARE(visible.size(), 2);
        QCOMPARE(stats.templateBuilds, uint64_t(1));
        QCOMPARE(visible[0].position - map.source.topLeft(), QPoint(0, 32));
        QCOMPARE(visible[0].image.cacheKey(), visible[1].image.cacheKey());
        setFields(w.appearances[2], "{}"); // bounds shrink back outside view
        QVERIFY(entities.prepare(w, map, read).isEmpty());
    }
    void warmSceneAndCameraReuse() {
        auto w = world();
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        shell::EntityMap::Stats stats;
        auto first = entities.prepare(w, map, reader());
        auto warm = entities.prepare(w, map, {}, 0, &stats);
        QCOMPARE(warm.constData(), first.constData());
        QCOMPARE(stats.sceneReuses, uint64_t(1));
        w.atoms[42].pixelX = 1;
        auto moved = entities.prepare(w, map, {}, 0, &stats);
        QCOMPARE(stats.instanceUpdates, uint64_t(1));
        QCOMPARE(stats.sortedSprites, uint64_t(0));
        QCOMPARE(stats.templateBuilds, uint64_t(0));
        QCOMPARE(moved[0].position, first[0].position + QPoint(1, 0));
        QCOMPARE(first[0].position, map.source.topLeft() + QPoint(32, 32)); // old mailbox data is untouched
        map.source.translate(1, 0); // camera-only glide
        entities.prepare(w, map, {}, 0, &stats);
        QCOMPARE(stats.instanceUpdates, uint64_t(0));
        QCOMPARE(stats.sortedSprites, uint64_t(0));
        auto old = entities.prepare(w, map, {});
        map.geometry.region.translate(2, 3); // recentered terrain, same world viewport
        map.source.translate(-64, 96);
        auto recentered = entities.prepare(w, map, {}, 0, &stats);
        QCOMPARE(stats.templateBuilds, uint64_t(0));
        QCOMPARE(stats.instanceUpdates, uint64_t(0));
        QCOMPARE(stats.sortedSprites, uint64_t(0));
        QCOMPARE(recentered[0].position, old[0].position + QPoint(-64, 96));
        QCOMPARE(recentered[0].image.cacheKey(), old[0].image.cacheKey());
    }
    void largeWarmScene_data() {
        QTest::addColumn<bool>("animated"); QTest::addColumn<bool>("flick");
        QTest::newRow("static") << false << false; QTest::newRow("animated") << true << false;
        QTest::newRow("flick") << true << true;
    }
    void largeWarmScene() {
        QFETCH(bool,animated); QFETCH(bool,flick);
        auto w = world();
        if (animated) w.appearances[1].fields.iconState="";
        w.bounds = {true, 100, 100, 0, 71, 71, 71, 71};
        w.resync.originX = w.resync.originY = 0;
        w.atoms.clear();
        model::Atom atom; atom.hasPos = true; atom.appid = 1;
        for (int i = 0; i < 10000; ++i) {
            atom.x = 11 + (i % 50); atom.y = 11 + (i / 50) % 50;
            w.atoms[uint32_t(i)] = atom;
            if (flick) w.flicks[uint32_t(i)]={"",0,true,0,0,false};
        }
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        shell::EntityMap::Stats stats;
        auto read = animated ? shell::TurfMap::Read([](uint32_t,QByteArray& out){out=iconfixture::animation();return true;}) : reader();
        auto first = entities.prepare(w, map, read, 0, &stats);
        QCOMPARE(first.size(), 10000);
        QCOMPARE(stats.templateBuilds, uint64_t(1));
        QCOMPARE(stats.visits, uint64_t(1));
        QCOMPARE(stats.instanceUpdates, uint64_t(10000));
        QElapsedTimer clock; clock.start();
        for (int i = 0; i < 30; ++i) {
            auto warm = entities.prepare(w, map, {}, 0, &stats);
            QCOMPARE(warm.constData(), first.constData());
            QCOMPARE(stats.candidates, uint64_t(10000));
            QCOMPARE(stats.visits, uint64_t(0));
            QCOMPARE(stats.templateBuilds, uint64_t(0));
            QCOMPARE(stats.instanceUpdates, uint64_t(0));
            QCOMPARE(stats.sortedSprites, uint64_t(0));
            QCOMPARE(stats.sceneReuses, uint64_t(1));
        }
        qInfo("10,000 %s entities between transitions: %.3f ms/prepare", flick ? "flicking" : animated ? "animated" : "static", double(clock.nsecsElapsed()) / 30e6);
        if (animated && !flick) {
            for (int64_t ns : {50000000LL,200000000LL,400000000LL}) {
                const auto frame=entities.prepare(w,map,{},ns,&stats);
                QCOMPARE(frame.size(),10000); QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.sortedSprites,uint64_t(0));
            }
            QCOMPARE(entities.prepare(w,map,{},1)[0].image.cacheKey(),first[0].image.cacheKey());
        }
        w.atoms[1].pixelX++;
        auto moved = entities.prepare(w, map, {}, 0, &stats);
        QCOMPARE(moved.size(), 10000);
        QCOMPARE(stats.instanceUpdates, uint64_t(1));
        QCOMPARE(stats.sortedSprites, uint64_t(0));
        QCOMPARE(stats.templateBuilds, uint64_t(0));
    }
    void cachedMatchesFreshAcrossUpdates() {
        auto w = world();
        const auto original = w.appearances[1];
        for (uint32_t id = 2; id <= 8; ++id) {
            w.appearances[id] = original;
            w.appearances[id].fields.iconState = id % 2 ? "red" : "green";
            setFields(w.appearances[id], "{\"overlays\":[" + std::to_string(id - 1) + "]}");
            w.appearances[id].fields.layerBits = std::bit_cast<uint32_t>(-1.0f);
        }
        for (uint32_t key = 1; key <= 40; ++key) {
            w.atoms[key] = w.atoms[42];
            w.atoms[key].appid = key % 8 + 1;
        }
        shell::TurfMap terrain;
        shell::EntityMap entities;
        std::mt19937 random(1234);
        auto read = reader();
        for (int step = 0; step < 150; ++step) {
            uint32_t key = 1 + random() % 40, id = 1 + random() % 8;
            auto& atom = w.atoms[key];
            switch (step % 12) {
            case 0: atom.x = 3 + random() % 6; atom.y = 3 + random() % 6; break;
            case 1: atom.pixelX = int16_t(int(random() % 100) - 50); break;
            case 2: setFields(w.appearances[id], "{\"pixelX\":8,\"overlays\":[" + std::to_string(random() % 8 + 1) + "]}"); break;
            case 3: w.appearances[id].fields.layerBits = std::bit_cast<uint32_t>(float(random() % 5)); break;
            case 4: atom.dir = int(random() % 9); atom.appid = id; break;
            case 5: atom.key++; atom.haveKey = true; break;
            case 6: atom.hasPos = !atom.hasPos; break;
            case 7: w.appearances[id].fields.iconResID = random() % 3; break;
            case 8: w.resources.push_back({uint32_t(200 + step), 0}); break;
            case 9: w.appearances.erase(id); break;
            case 10: w.appearances[id] = original; break;
            case 11: w.resync.originX = uint16_t(random() % 5); break;
            }
            auto map = terrain.prepare(w, {});
            auto cached = entities.prepare(w, map, read);
            shell::EntityMap fresh;
            QVERIFY2(samePixelsAndMetadata(cached, fresh.prepare(w, map, read)), qPrintable(QString("step %1").arg(step)));
            // Unrelated appearances must not invalidate any active template.
            setFields(w.appearances[9999], std::to_string(step));
            shell::EntityMap::Stats stats;
            auto again = entities.prepare(w, map, read, 0, &stats);
            QCOMPARE(again.constData(), cached.constData());
            QCOMPARE(stats.templateBuilds, uint64_t(0));
        }
    }
    void sharedTemplatesStillRespectSceneBudget() {
        auto w = world();
        QStringList children;
        for (int i = 2; i <= 101; ++i) children << QString::number(i); // missing descendants still count as visits
        setFields(w.appearances[1], "{\"overlays\":[" + children.join(',').toStdString() + "]}");
        const auto atom = w.atoms[42]; w.atoms.clear();
        for (uint32_t i = 0; i < 1000; ++i) w.atoms[i] = atom;
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        shell::EntityMap::Stats stats;
        auto first = entities.prepare(w, map, reader(), 0, &stats);
        QCOMPARE(stats.templateBuilds, uint64_t(1));
        QCOMPARE(stats.visits, uint64_t(101));
        QCOMPARE(first.size(), 65536 / 101);
        auto warm = entities.prepare(w, map, {}, 0, &stats);
        QCOMPARE(warm.constData(), first.constData());
        QCOMPARE(stats.visits, uint64_t(0));
        setFields(w.appearances[1], "{}");
        QCOMPARE(entities.prepare(w, map, {}).size(), 1000);
    }
    void diagnostics() {
        auto w = world();
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        shell::EntityMap::Stats stats;
        auto first = entities.prepare(w, map, reader(), 0, &stats);
        QCOMPARE(stats.candidates, uint64_t(1));
        QCOMPARE(stats.visits, uint64_t(1));
        QCOMPARE(stats.appearanceDecodes, uint64_t(1));
        QCOMPARE(stats.frameDecodes, uint64_t(1));
        QCOMPARE(stats.resourceReads, uint64_t(1));
        auto warm = entities.prepare(w, map, reader(), 0, &stats);
        QVERIFY(warm == first);
        QCOMPARE(stats.candidates, uint64_t(1));
        QCOMPARE(stats.visits, uint64_t(0));
        QCOMPARE(stats.templateBuilds, uint64_t(0));
        QCOMPARE(stats.instanceUpdates, uint64_t(0));
        QCOMPARE(stats.sortedSprites, uint64_t(0));
        QCOMPARE(stats.sceneReuses, uint64_t(1));
        QCOMPARE(warm.constData(), first.constData());
        QCOMPARE(stats.appearanceDecodes, uint64_t(0));
        QCOMPARE(stats.frameDecodes, uint64_t(0));
        QCOMPARE(stats.resourceReads, uint64_t(0));
        QVERIFY(stats.traverseMs >= 0 && stats.sortMs >= 0 && stats.pruneMs >= 0);
        QVERIFY(entities.prepare(w, map, reader()) == warm); // tracing does not affect the draw list
        entities.prepare(w, {}, {}, 0, &stats);
        QCOMPARE(stats.candidates, uint64_t(0));
        QCOMPARE(stats.visits, uint64_t(0));
        QCOMPARE(stats.traverseMs, 0.0);
        w.atoms[43] = w.atoms[42];
        auto missing = w.appearances[1];
        missing.fields.iconResID = 1; w.resources.push_back({456, 0});
        w.appearances[2] = missing; w.atoms[43].appid = 2;
        entities.prepare(w, map, {}, 0, &stats);
        QCOMPARE(stats.candidates, uint64_t(2));
        QCOMPARE(stats.appearanceDecodes, uint64_t(1));
        QCOMPARE(stats.resourceReads, uint64_t(1));
        QCOMPARE(stats.frameDecodes, uint64_t(0));
    }
    void mailboxReplayAndClear() {
        auto w = world();
        shell::TurfMap terrain;
        shell::EntityMap entities;
        auto map = terrain.prepare(w, {});
        auto sprites = entities.prepare(w, map, reader());
        shell::MapMailbox mailbox(std::make_shared<shell::MapMetrics>());
        mailbox.publish(map.image, map.source, 0, 0, map.geometry, sprites);
        auto first = mailbox.forRender(0);
        QVERIFY(first && first->sprites == sprites);
        auto replay = mailbox.forRender(0);
        QVERIFY(replay && replay->sprites == sprites);
        mailbox.publish({}, {}, 0, 0);
        QVERIFY(mailbox.forRender(first->sequence)->sprites.isEmpty());
        QCOMPARE(first->sprites.size(), 1);
    }
};
FUGUE_TEST_GUILESS_MAIN(EntityMapTest)
#include "entitymap_test.moc"
