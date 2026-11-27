// Qt fonts for skin controls, selected from font-family, font-size and font-style.
#pragma once
#include <string>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QString>

class QPainter;

namespace fugue::shell {

struct SkinFont {
    QFont font;
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
