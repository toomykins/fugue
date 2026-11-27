#pragma once
#include <QBuffer>
#include <QImage>
namespace iconfixture {
inline QByteArray png(QImage image) {
    QByteArray out; QBuffer buffer(&out); buffer.open(QIODevice::WriteOnly); image.save(&buffer,"PNG"); return out;
}
inline QByteArray animation(int loops = 0, bool rewind = false, int dirs = 4, QString delays = "0.5,1.5,2") {
    QImage image(32 * dirs, 96, QImage::Format_RGBA8888);
    for (int f = 0; f < 3; ++f) for (int d = 0; d < dirs; ++d)
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)
            image.setPixelColor(d * 32 + x, f * 32 + y,
                f == 1 && x < 8 ? QColor(Qt::transparent) : QColor(50 + f * 80, 30 + d * 20, 20));
    image.setText("Description", QString("width = 32\nheight = 32\nstate = \"\"\ndirs = %1\nframes = 3\ndelay = %2\nloop = %3\nrewind = %4\n")
        .arg(dirs).arg(delays).arg(loops).arg(rewind ? 1 : 0));
    return png(image);
}
inline QByteArray flickAsset(int size = 32) {
    QImage image(size * 4, size * 10, QImage::Format_RGBA8888);
    for (int f = 0; f < 10; ++f) for (int d = 0; d < 4; ++d)
        for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
            const int phase = (f - 1) % 3;
            image.setPixelColor(d * size + x, f * size + y, f == 0 ? QColor(180,180,180) :
                phase == 1 && x < size / 4 ? QColor(Qt::transparent) : QColor(50 + phase * 80,30 + d * 20,f >= 4 ? 220 : 20));
        }
    image.setText("Description", QString("width = %1\nheight = %1\nstate = \"\"\ndirs = 4\nframes = 1\ndelay = 2\n"
        "state = \"pulse\"\ndirs = 4\nframes = 3\ndelay = 0.5,1.5,2\nloop = 2\nrewind = 1\n"
        "state = \"other\"\ndirs = 4\nframes = 3\ndelay = 0.5,1.5,2\n"
        "state = \"short\"\ndirs = 4\nframes = 3\ndelay = 0.1,0.1,0.1\n").arg(size));
    return png(image);
}
inline QByteArray select(uint32_t ordinal, const QByteArray& state, int index = 0) {
    QByteArray out(1, char(1));
    for (int i=0;i<4;++i) out += char(ordinal>>(i*8));
    out += QByteArray::fromHex("0805"); out += state; out += char(0);
    out += char(0x40); out += char(index); out += QByteArray::fromHex("ffff"); return out;
}
inline QByteArray recipe(const QByteArray& program) {
    QByteArray out("DDMI");
    for (int i=0;i<4;++i) out += char(uint32_t(program.size())>>(i*8));
    return out+program;
}
inline QByteArray masked(uint32_t ordinal) {
    // Both selects ask for direction index 0 because sheet() below has one direction per state. In stock, asking
    // a one-direction state for another direction gives an empty icon (ddmiops-world case f_onedir).
    return recipe(select(ordinal,"base",0)+select(ordinal,"mask",0)+QByteArray::fromHex("42070300ff00ff03000000004801"));
}
inline QByteArray sheet(QColor color = Qt::red) {
    QImage image(64,32,QImage::Format_RGBA8888); image.fill(color);
    for (int y=0;y<32;++y) for(int x=32;x<64;++x) image.setPixelColor(x,y,x<48 ? QColor(Qt::green) : QColor(Qt::transparent));
    image.setText("Description","width = 32\nheight = 32\nstate = \"base\"\ndirs = 1\nframes = 1\nstate = \"mask\"\ndirs = 1\nframes = 1\n");
    return png(image);
}
}
