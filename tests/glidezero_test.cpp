// A glide_size of 0 is the automatic glide, not "one tile per tick". Stock moves such an atom, and the view when it is
// the eye, 4 px per tick (stock falls back to 4); in LEGACY movement mode stock also raises the rate by
// 2 px for every move that lands while the sprite is still gliding the way it moves (cap icon/2).
// Data: tests/data/glide-zero/measured.txt, camera shifts measured on stock DreamSeeker (header there).
#include <QtTest>
#include "tests/testmain.h"
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include "model/world.h"

using namespace fugue;
using namespace fugue::model;

namespace {
struct Row {
    std::string mode, kind;
    std::vector<double> v;
};

bool loadRows(std::vector<Row>& out, QString& error) {
    out.clear();
    error.clear();
    const QString path = QStringLiteral(FUGUE_TEST_DATA_DIR "/glide-zero/measured.txt");
    qsizetype lineNumber = 0;
    auto fail = [&](const QString& reason) {
        error = QString("%1:%2: %3").arg(path).arg(lineNumber).arg(reason);
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail("Cannot read measurements: " + file.errorString());
    std::vector<Row> rows;
    QSet<QString> seen;
    const QRegularExpression whitespace("\\s+");
    while (!file.atEnd()) {
        ++lineNumber;
        const auto line = QString::fromUtf8(file.readLine()).trimmed();
        if (file.error() != QFileDevice::NoError) return fail("Cannot read measurements: " + file.errorString());
        if (line.isEmpty() || line.startsWith('#')) continue;
        const auto fields = line.split(whitespace, Qt::SkipEmptyParts);
        if (fields.size() < 2 || (fields[0] != "LEGACY" && fields[0] != "PIXEL") ||
            (fields[1] != "iso" && fields[1] != "held"))
            return fail("Expected LEGACY or PIXEL, then iso or held, followed by measured values");
        const auto name = fields[0] + ' ' + fields[1];
        if (seen.contains(name)) return fail("Duplicate measurement row: " + name);
        const int samples = fields[1] == "iso" ? 8 : fields[0] == "LEGACY" ? 6 : 7;
        if (fields.size() != samples + 2)
            return fail(QString("%1: expected %2 measured values, found %3").arg(name).arg(samples).arg(fields.size() - 2));
        Row row{fields[0].toStdString(), fields[1].toStdString(), {}};
        for (qsizetype i = 2; i < fields.size(); ++i) {
            bool valid = false;
            const double value = fields[i].toDouble(&valid);
            if (!valid || !std::isfinite(value))
                return fail(QString("%1 sample %2: expected a finite number, got '%3'").arg(name).arg(i - 2).arg(fields[i]));
            row.v.push_back(value);
        }
        seen.insert(name);
        rows.push_back(std::move(row));
    }
    if (seen.size() != 4) {
        if (!lineNumber) lineNumber = 1;
        return fail("Expected all four LEGACY/PIXEL iso/held measurement rows");
    }
    out = std::move(rows);
    return true;
}

// The view shift (tiles) the model draws at the end of each tick when the eye steps east `moves` times, one per tick.
std::vector<double> drive(const Row& r, int moves, int ticks) {
    constexpr int64_t tick = 100'000'000, base = 1'000'000'000'000;
    World w;
    w.movementMode = r.mode == "PIXEL" ? 8 : 0;
    float autoPx = 0;
    int64_t done = 0;
    GlideMotion g;
    bool gliding = false;
    uint8_t dir = 0;
    std::vector<double> out;
    for (int n = 0; n < ticks; n++) {
        int64_t now = base + n * tick;
        if (n < moves) {
            double sx, sy;
            if (armGlideOffset(w, gliding ? &g : nullptr, now, -1, 0, dir, sx, sy)) {
                autoPx = autoGlideRate(w, autoPx, done, gliding ? &g : nullptr, now, -1, 0);
                g = glideMotion(sx, sy, autoPx, 32, tick, false, now, true);
                done = g.startNs + g.durNs;
                gliding = true;
            }
        }
        out.push_back(std::min(n + 1, moves) - std::fabs(g.sx) * glideLeft(g, now + tick - 1));
    }
    return out;
}
}  // namespace

class GlideZeroTest : public QObject {
    Q_OBJECT
private slots:
    void stockRateIsFourPixelsPerTick() {
        QCOMPARE(float(kAutoGlidePx), 4.f);
        QCOMPARE(glideMotion(-1, 0, 0, 32, 100'000'000, false, 1, true).durNs, int64_t(800'000'000));
        QCOMPARE(glideDurationNs(1, 0, 0, 32, 100'000'000, false), int64_t(800'000'000));
    }

    void matchesMeasuredStock() {
        std::vector<Row> rows; QString error;
        QVERIFY2(loadRows(rows, error), qPrintable(error));
        QCOMPARE(int(rows.size()), 4);
        for (const Row& r : rows) {
            bool held = r.kind == "held";
            auto got = drive(r, held ? 14 : 1, int(r.v.size()));
            for (size_t i = 0; i < r.v.size(); i++) {
                double tol = held ? 0.06 : 0.02;
                QVERIFY2(std::fabs(got[i] - r.v[i]) <= tol, qPrintable(QString("%1 %2 tick %3: model %4, stock %5")
                                                                           .arg(r.mode.c_str(), r.kind.c_str())
                                                                           .arg(i)
                                                                           .arg(got[i])
                                                                           .arg(r.v[i])));
            }
        }
    }

    // LEGACY: a move at rest derives its rate from the previous one and the ticks since that glide played out.
    void legacyRestFormula() {
        World w;
        QCOMPARE(autoGlideRate(w, 0, 0, nullptr, 1'000'000'000, -1, 0), 4.f);  // never glided: 4
        // previous 16 px, played out 5 ticks ago: 32 / (32/16 + 5) = 4.571
        QVERIFY(std::fabs(autoGlideRate(w, 16, 1'000'000'000, nullptr, 1'500'000'000, -1, 0) - 32.f / 7) < 1e-4);
        // previous 16 px, played out one tick ago: 32 / 3 = 10.67, whose integer part 10 is even
        QVERIFY(std::fabs(autoGlideRate(w, 16, 1'000'000'000, nullptr, 1'100'000'000, -1, 0) - 32.f / 3) < 1e-4);
        w.movementMode = 8;
        QCOMPARE(autoGlideRate(w, 16, 1'000'000'000, nullptr, 1'000'000'000, -1, 0), 4.f);  // PIXEL: always 4
    }
};

FUGUE_TEST_GUILESS_MAIN(GlideZeroTest)
#include "glidezero_test.moc"
