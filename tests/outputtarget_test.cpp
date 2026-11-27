// output() text and the chat: stock DreamSeeker 516 prints text sent to a named control only when that control is an
// OUTPUT. Sent to a BROWSER, LABEL, MAP or window, or to a control that does not exist, it prints nothing (the "Fugue Output Target"
// contrast world, docs/protocol/GAME-CLIENT-SERVER-516.md). vg's voting panel is the case that matters: a winclone'd
// window whose page arrives as output(page, "browser_<ref>.browser"), with the panel's dark stylesheets in its head.
#include <QtTest>
#include "tests/testmain.h"
#include <string>
#include <vector>

#include "model/world.h"
#include "skin/liveskin.h"

using namespace fugue;

namespace {
const char* kSkin = R"(window "mainwindow"
	elem "mainwindow"
		type = MAIN
		pos = 0,0
		size = 640x480
		is-default = true
	elem "output"
		type = OUTPUT
		pos = 0,0
		size = 640x480
	elem "lbl"
		type = LABEL
		pos = 0,0
		size = 64x16
	elem "map"
		type = MAP
		pos = 0,0
		size = 320x320
window "outputwindow"
	elem "outputwindow"
		type = MAIN
		pos = 0,0
		size = 640x480
		is-pane = true
	elem "browseroutput"
		type = BROWSER
		pos = 0,0
		size = 640x480
)";

std::vector<uint8_t> outputFrame(const std::string& value, const std::string& control) {
    std::vector<uint8_t> b(value.begin(), value.end());
    b.push_back(0);
    b.insert(b.end(), control.begin(), control.end());
    b.push_back(0);
    return b;
}
}  // namespace

class OutputTargetTest : public QObject {
    Q_OBJECT
    uint64_t send(model::Session& s, const std::string& value, const std::string& control) {
        uint64_t before = s.world().chatTotal;
        auto b = outputFrame(value, control);
        s.ingest(S2C::Output, ByteSpan(b.data(), b.size()));
        return s.world().chatTotal - before;
    }
private slots:
    void onlyTextControlsPrint() {
        auto live = std::make_shared<skin::LiveSkin>();
        QCOMPARE(QString::fromStdString(live->load(kSkin)), QString());
        model::Session s;
        s.setTextTarget([live](const std::string& c) { return live->printsOutput(c); });
        QCOMPARE(send(s, "plain", ""), uint64_t(1));
        QCOMPARE(send(s, "to+output", "output"), uint64_t(1));
        QCOMPARE(send(s, "to+output", "mainwindow.output"), uint64_t(1));
        QCOMPARE(send(s, "%3cspan%3echat%3c%2fspan%3e", "browseroutput:output"),
                 uint64_t(1));  // the chat page's output()
        const std::string page =
            "%3chtml%3e%3chead%3e%3clink+rel%3d%22stylesheet%22+href%3d%22nanotrasen.css%22+%2f%3e"
            "%3c%2fhead%3e%3cbody%3e%3c%2fbody%3e%3c%2fhtml%3e";
        QCOMPARE(send(s, page, "browser_[0x21000123].browser"), uint64_t(0));  // no such window here
        live->winset("browser_[0x21000123].browser", "parent=mainwindow;type=browser");
        QCOMPARE(send(s, page, "browser_[0x21000123].browser"), uint64_t(0));  // a runtime BROWSER
        QCOMPARE(send(s, page, "browseroutput"), uint64_t(0));                 // the skin's own BROWSER
        QCOMPARE(send(s, "x", "nosuchctl"), uint64_t(0));
        QCOMPARE(send(s, "x", "lbl"), uint64_t(0));
        QCOMPARE(send(s, "x", "map"), uint64_t(0));
        QCOMPARE(send(s, "x", "mainwindow"), uint64_t(0));
    }
    void beforeTheSkinEverythingPrints() {
        auto live = std::make_shared<skin::LiveSkin>();
        model::Session s;
        s.setTextTarget([live](const std::string& c) { return live->printsOutput(c); });
        QCOMPARE(send(s, "early", "nosuchctl"), uint64_t(1));
    }
};

FUGUE_TEST_GUILESS_MAIN(OutputTargetTest)
#include "outputtarget_test.moc"
