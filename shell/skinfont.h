// skinfont: the font of a skin control (BUTTON, LABEL, INPUT, INFO) from its font-family, font-size and font-style,
// and button and label text laid out the way stock DreamSeeker lays it out.
//
// Stock creates a GDI font per control: font-size is points at 96 dpi (unset or 0 is 9pt); no family or
// `sans-serif` is the bitmap "MS Sans Serif", `monospace` the bitmap "Courier"; a family list takes the first that exists.
// A bitmap face is drawn in a TrueType stand-in at the character height of the strike GDI picks, with text placed at the
// strike's own advance widths so it is as wide as stock's whatever the stand-in.
#pragma once
#include <string>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QString>

#include "render/dsmaptext.h"

class QPainter;

namespace fugue::shell {

struct SkinFont {
    QFont font;
    render::DsFace bitmap;
};

SkinFont skinControlFont(const std::string& family, const std::string& size, const std::string& style);

// One line of text in a control font: its width; the longest head that fits `width` with "..." after it; drawn in `r`.
int skinTextWidth(const SkinFont& f, const QString& text);
QString skinEllipsized(const SkinFont& f, const QString& text, int width);
void drawSkinText(QPainter& p, const QRect& r, const SkinFont& f, const QString& text, bool ellipsis,
                  Qt::Alignment align = Qt::AlignCenter);

// A LABEL's `align` value; stock centres when unset.
Qt::Alignment skinLabelAlign(const std::string& v);

// The menu bar's and status bar's font: Tahoma 8pt (11 px).
QFont skinMenuFont();

class SkinButton : public QPushButton {
public:
    using QPushButton::QPushButton;
    void setSkinFont(const SkinFont& f) {
        sf_ = f;
        setFont(f.font);
        update();
    }
    const SkinFont& skinFont() const { return sf_; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    SkinFont sf_ = skinControlFont("", "", "");
};

class SkinLabel : public QLabel {
public:
    using QLabel::QLabel;
    void setSkinFont(const SkinFont& f) {
        sf_ = f;
        setFont(f.font);
        update();
    }
    const SkinFont& skinFont() const { return sf_; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    SkinFont sf_ = skinControlFont("", "", "");
};

}  // namespace fugue::shell
