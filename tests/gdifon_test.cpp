// render/gdifon.h: reading raster strikes out of a .FON and drawing a glyph the way DreamSeeker does. The files are
// built here byte by byte (tests/fonfile.h). The separate host-font entry checks an installed Small Fonts file against
// the layout's advance widths.
#include <QDir>
#include <QFile>
#include <QtTest>
#include "tests/testmain.h"
#include <string>

#include "render/dsmaptext.h"
#include "render/gdifon.h"
#include "tests/fonfile.h"

using namespace fugue::render;

namespace {

// The face the layout gives Small Fonts at `px` (bold/italic as asked).
DsFace smallFonts(int px, const std::string& extra = "") {
    DsLayout L = dsLayoutMaptext(
        "<span style=\"font-family:'Small Fonts';font-size:" + std::to_string(px) + "px;" + extra + "\">A</span>", "",
        0);
    return L.faces.at(0);
}

// A 7pt strike with the layout's own advance widths; glyph c inks (x, y) when (x + y + c) is a multiple of 3.
fontfile::Strike layoutStrike() {
    fontfile::Strike s;
    const DsFace f = smallFonts(7);
    for (unsigned c = s.first; c <= s.last; c++) s.widths.push_back(unsigned(dsBitmapAdvance(f, int(c))));
    s.ink = [](unsigned c, unsigned x, unsigned y) { return (x + y + c) % 3 == 0; };
    return s;
}

std::string rows(const GlyphMask& m) {
    std::string s;
    for (int y = 0; y < m.h; y++) {
        for (int x = 0; x < m.w; x++) s += m.a[size_t(y) * size_t(m.w) + size_t(x)] ? '#' : '.';
        s += '/';
    }
    return s;
}

std::string expected(const fontfile::Strike& s, unsigned c, bool bold) {
    const unsigned w = s.widths[c - s.first];
    std::string o;
    for (unsigned y = 0; y < s.h; y++) {
        for (unsigned x = 0; x < w + (bold ? 1 : 0); x++)
            o += (x < w && s.ink(c, x, y)) || (bold && x > 0 && s.ink(c, x - 1, y)) ? '#' : '.';
        o += '/';
    }
    return o;
}

}  // namespace

class GdiFon : public QObject {
    Q_OBJECT
private slots:
    void parsesTheNinetySixDpiStrike() {
        fontfile::Strike a;
        a.first = 'A';
        a.last = 'B';
        a.defaultChar = 'B';
        a.h = 4;
        a.ascent = 3;
        a.il = 1;
        a.widths = {3, 9};  // 'B' spans two bitmap columns
        a.ink = [](unsigned c, unsigned x, unsigned y) { return c == 'B' ? (y == 0 || x == 8) : x == y; };
        fontfile::Strike a72 = a;
        a72.dpi = 72;
        auto ss = parseFon(fontfile::fon({a72, a}));
        QCOMPARE(int(ss.size()), 1);
        QCOMPARE(ss[0].h, 4);
        QCOMPARE(ss[0].ascent, 3);
        QCOMPARE(ss[0].defaultChar, int('B'));
        QCOMPARE(int(ss[0].glyphs.size()), 2);
        QCOMPARE(ss[0].glyphs[1].w, 9);
        auto bits = [](const FonStrike::Glyph& g) {
            std::string s;
            for (uint8_t b : g.bits) s += b ? '#' : '.';
            return s;
        };
        QCOMPARE(QString::fromStdString(bits(ss[0].glyphs[0])), QString("#.."
                                                                        ".#."
                                                                        "..#"
                                                                        "..."));
        QCOMPARE(QString::fromStdString(bits(ss[0].glyphs[1])), QString("#########"
                                                                        "........#"
                                                                        "........#"
                                                                        "........#"));
        QVERIFY(parseFon("not a font").empty());
        QVERIFY(parseFon(fontfile::fon({a}).substr(0, 0x120)).empty());
    }

    void drawsGlyphs() {
        const fontfile::Strike s = layoutStrike();
        auto ss = parseFon(fontfile::fon({s}));
        QCOMPARE(int(ss.size()), 1);
        GlyphMask m;
        QVERIFY(fonGlyph(ss, smallFonts(7), U'A', m));
        QCOMPARE(QString::fromStdString(rows(m)), QString::fromStdString(expected(s, 'A', false)));
        QCOMPARE(m.left, 0);
        QVERIFY(fonGlyph(ss, smallFonts(7), U'€', m));  // the euro sign is cp1252 byte 0x80
        QCOMPARE(QString::fromStdString(rows(m)), QString::fromStdString(expected(s, 0x80, false)));
        QVERIFY(fonGlyph(ss, smallFonts(7), U'Ā', m));  // outside the code page: the default char
        QCOMPARE(QString::fromStdString(rows(m)), QString::fromStdString(expected(s, s.defaultChar, false)));
        // Bold overstrikes 1 px to the right; italic slants: one pixel right for every two rows above the cell's last.
        QVERIFY(fonGlyph(ss, smallFonts(7, "font-weight:bold"), U'g', m));
        QCOMPARE(QString::fromStdString(rows(m)), QString::fromStdString(expected(s, 'g', true)));
        QVERIFY(fonGlyph(ss, smallFonts(7, "font-style:italic"), U'g', m));
        {
            const int w = int(s.widths['g' - s.first]), top = (int(s.h) - 1) / 2;
            std::string slanted;
            for (int y = 0; y < int(s.h); y++) {
                const int shift = top - y / 2;
                for (int x = 0; x < w + top; x++)
                    slanted += x >= shift && x - shift < w && s.ink('g', unsigned(x - shift), unsigned(y)) ? '#' : '.';
                slanted += '/';
            }
            QCOMPARE(QString::fromStdString(rows(m)), QString::fromStdString(slanted));
        }
        QCOMPARE(m.left, 0);
        // Windows never scales a strike: 14px and 22px are the 7pt strike of its 120-dpi file as it is, 13 px high,
        // which is not in this file.
        for (int px : {14, 22}) {
            const DsFace f = smallFonts(px);
            QCOMPARE(f.height, 13);
            QVERIFY(!fonGlyph(ss, f, U'M', m));
        }
    }

    // The strike a size is drawn from on Windows: GDI's choice with raster scaling off, among the strikes of both
    // resolutions (stock 516.1689). A size is in points.
    void picksTheStrikeGdiPicks() {
        auto height = [](const std::string& family, int pt) {
            return dsLayoutMaptext("<span style=\"font-family:'" + family + "';font-size:" + std::to_string(pt) + "pt\">A</span>", "", 0)
                .faces.at(0)
                .height;
        };
        for (auto [pt, h] : {std::pair{8, 13}, {14, 24}, {16, 25}, {17, 29}, {22, 29}, {23, 36}, {28, 37}, {29, 46}, {79, 46}})
            QCOMPARE(height("MS Sans Serif", pt), h);
        QCOMPARE(height("Small Fonts", 7), 11);
        QCOMPARE(height("Small Fonts", 8), 13);
        QCOMPARE(height("Small Fonts", 58), 13);
        QCOMPARE(height("Courier", 16), 20);
        QCOMPARE(height("Courier", 19), 25);
    }

    // Glyphs are only drawn from a strike whose metrics and every advance width match the strike the layout used.
    void refusesAStrikeTheLayoutDidNotUse() {
        GlyphMask m;
        fontfile::Strike s = layoutStrike();
        s.widths['x' - s.first]++;
        auto ss = parseFon(fontfile::fon({s}));
        QVERIFY(!fonGlyph(ss, smallFonts(7), U'A', m));
        QVERIFY(!fonGlyph(ss, smallFonts(7), U'x', m));
        s = layoutStrike();
        while (s.widths[s.defaultChar - s.first] == s.widths[128 - s.first])
            s.defaultChar++;  // a default char of another width, among the bytes Windows' files leave empty
        QVERIFY(!fonGlyph(parseFon(fontfile::fon({s})), smallFonts(7), U'Ā', m));  // only what draws as it is refused
        s = layoutStrike();
        s.ascent = 8;
        QVERIFY(!fonGlyph(parseFon(fontfile::fon({s})), smallFonts(7), U'A', m));
        QVERIFY(fonGlyph(parseFon(fontfile::fon({layoutStrike()})), smallFonts(7), U'A', m));
    }
};

class GdiFonHost : public QObject {
    Q_OBJECT
private slots:
    void hostSmallFontsMatchesTheLayout() {
        const QString directory = qEnvironmentVariable("FUGUE_FONT_DIR");
        QFile f(QDir(directory.isEmpty() ? QString("/usr/share/wine/fonts") : directory).filePath("smalle.fon"));
        // Without FUGUE_FONT_DIR this checks Wine's copy, which only some hosts have.
        if (directory.isEmpty() && !f.exists()) QSKIP(qPrintable(f.fileName() + " is not installed; set FUGUE_FONT_DIR to check a host font"));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.fileName() + ": " + f.errorString()));
        const QByteArray d = f.readAll();
        QVERIFY2(f.error() == QFileDevice::NoError, qPrintable(f.fileName() + ": " + f.errorString()));
        auto ss = parseFon(std::string_view(d.constData(), size_t(d.size())));
        QVERIFY2(!ss.empty(), qPrintable(f.fileName() + ": no readable 96-DPI font strikes"));
        const DsFace fc = smallFonts(7);
        int checked = 0;
        for (int c = 32; c <= 255; c++) {
            GlyphMask m;
            QVERIFY(fonGlyph(ss, fc, char32_t(c), m));
            QCOMPARE(m.h, fc.height);
            checked++;
        }
        QCOMPARE(checked, 224);
    }
};

FUGUE_TEST_MAIN(GdiFon)

int fugue_run_gdifon_host(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    GdiFonHost test;
    QTest::setMainSourcePath(__FILE__);
    return QTest::qExec(&test, argc, argv);
}

#include "gdifon_test.moc"
