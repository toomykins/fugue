#include "shell/dmiicon.h"
#include <QBuffer>
#include <QImageReader>
#include <QtEndian>
#include <zlib.h>
#include <algorithm>
#include <cmath>

namespace fugue::shell {
namespace {
constexpr int maxMetadata = 1024 * 1024, maxFrames = 65536;
constexpr int64_t defaultDelay = 100000000;
// Strip text before Qt/libpng sees it: a small compressed text chunk must not
// bypass our metadata budget through libpng's unbounded text expansion.
bool png(const QByteArray& bytes, QByteArray& pixels, QString& description) {
    if (bytes.size() > 128 * 1024 * 1024 || !bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) return false;
    pixels = bytes.first(8);
    qsizetype at = 8;
    int64_t textWork = 0;
    while (bytes.size() - at >= 12) {
        const auto n = qFromBigEndian<quint32>(bytes.constData() + at);
        if (n > quint64(bytes.size() - at - 12)) return false;
        const auto type = bytes.mid(at + 4, 4);
        if (type == "tEXt" || type == "zTXt" || type == "iTXt") {
            textWork += n;
            if (textWork > maxMetadata) return false;
            const auto data = bytes.mid(at + 8, n);
            const auto zero = data.indexOf('\0');
            if (zero < 0) return false;
            if (data.first(zero) == "Description") {
                auto text = data.mid(zero + 1);
                bool compressed = type == "zTXt";
                if (type == "zTXt") {
                    if (text.isEmpty() || text[0] != 0) return false;
                    text.remove(0, 1);
                } else if (type == "iTXt") {
                    if (text.size() < 2 || (text[0] != 0 && text[0] != 1) || text[1] != 0) return false;
                    compressed = text[0] == 1; text.remove(0, 2);
                    for (int i = 0; i < 2; ++i) {
                        const auto end = text.indexOf('\0');
                        if (end < 0) return false;
                        text.remove(0, end + 1);
                    }
                }
                if (compressed) {
                    QByteArray out(maxMetadata, Qt::Uninitialized);
                    uLongf length = uLongf(maxMetadata - textWork);
                    if (uncompress(reinterpret_cast<Bytef*>(out.data()), &length,
                                   reinterpret_cast<const Bytef*>(text.constData()), uLong(text.size())) != Z_OK) return false;
                    textWork += length;
                    out.resize(length); text = std::move(out);
                }
                description = QString::fromUtf8(text);
            }
        } else pixels.append(bytes.constData() + at, n + 12);
        at += n + 12;
        if (type == "IEND") return at == bytes.size();
    }
    return false;
}
}
DmiIcon DmiIcon::decode(const QByteArray& bytes, uint64_t maxPixels, bool* limited) {
    if (limited) *limited = false;
    maxPixels = std::min<uint64_t>(maxPixels, 64 * 1024 * 1024);
    DmiIcon icon;
    if (bytes.size() > 128 * 1024 * 1024) return icon;
    if (!bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) {
        const auto parsed = assets::parseIcon(reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()), maxPixels);
        if (limited) *limited = parsed.errClass == "pixel_limit";
        return parsed.ok() ? fromAsset(*parsed.icon) : DmiIcon{};
    }
    QByteArray pixels; QString description;
    if (!png(bytes, pixels, description)) return icon;
    QBuffer buffer(&pixels); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "png");
    const auto size = reader.size();
    if (size.isEmpty()) return icon;
    const uint64_t area = uint64_t(size.width()) * size.height();
    if (area > 64 * 1024 * 1024) return icon; // permanent per-image refusal
    if (area > maxPixels) { if (limited) *limited = true; return icon; }
    icon.sheet = reader.read();
    if (icon.sheet.isNull()) return icon;
    if (description.isEmpty()) {
        icon.cell = size; icon.states[QString()] = {}; return icon;
    }
    QString name, delays;
    auto metadata = std::make_shared<std::vector<assets::IconState>>();
    assets::IconState raw;
    State current;
    bool haveState = false, movement = false, valid = true;
    int64_t next = 0;
    auto finish = [&] {
        if (!haveState || !valid) return;
        next += int64_t(current.dirs) * current.frames;
        if (next > maxFrames) { valid = false; return; }
        { // Single-frame delays also determine flick duration.
            const auto values = delays.split(',');
            std::vector<int64_t> durations(current.frames, defaultDelay);
            for (int i = 0; i < current.frames && i < values.size(); ++i) {
                bool ok = false; const double value = values[i].toDouble(&ok);
                if (ok && std::isfinite(value) && value > 0 && value <= 864000)
                    durations[i] = std::max<int64_t>(1000000, int64_t(std::llround(value * 100000000)));
            }
            auto ends = std::make_shared<std::vector<int64_t>>();
            int64_t total = 0;
            auto append = [&](int64_t duration) {
                constexpr int64_t maxCycle = 365LL * 86400 * 1000000000;
                if (total > maxCycle - duration) { valid = false; return; }
                ends->push_back(total += duration);
            };
            for (auto duration : durations) append(duration);
            if (current.rewind)
                for (int i = current.frames - 2; i > 0; --i) append(durations[i]);
            if (!valid) return;
            current.ends = std::move(ends);
            raw.delays.clear();
            for (auto duration : durations) raw.delays.push_back(double(duration) / 100000000);
        }
        raw.name = name.toUtf8().toStdString(); raw.firstCell = current.first;
        raw.dirs = current.dirs; raw.frames = current.frames;
        raw.loop = current.loop; raw.rewind = current.rewind; raw.movement = movement;
        raw.hasDelays = !delays.isEmpty();
        metadata->push_back(raw);
        // Movement states are a separate namespace; not an ordinary state override.
        (movement ? icon.moving : icon.states).emplace(name, current);
    };
    for (QString line : description.split('\n')) {
        line = line.trimmed(); const auto eq = line.indexOf('=');
        if (eq < 0) continue;
        const auto key = line.left(eq).trimmed(), value = line.mid(eq + 1).trimmed();
        if (key == "width") icon.cell.setWidth(value.toInt());
        else if (key == "height") icon.cell.setHeight(value.toInt());
        else if (key == "state") {
            finish(); if (!valid) return {};
            name = value;
            if (name.startsWith('"') && name.endsWith('"')) name = name.mid(1, name.size() - 2);
            name.replace("\\\"", "\"").replace("\\\\", "\\");
            current = {}; raw = {}; current.first = int(next); haveState = true; movement = false; delays.clear();
        } else if (key == "dirs") {
            current.dirs = value.toInt();
            if (current.dirs != 1 && current.dirs != 4 && current.dirs != 8) valid = false;
        } else if (key == "frames") {
            current.frames = value.toInt();
            if (current.frames < 1 || current.frames > maxFrames) valid = false;
        } else if (key == "delay") delays = value;
        else if (key == "loop") current.loop = std::clamp(value.toInt(), 0, 65535);
        else if (key == "rewind") current.rewind = value.toInt() == 1;
        else if (key == "movement") movement = value.toInt() != 0;
        else if (key == "hotspot") {
            raw.hasHotspot = true;
            for (const auto& part : value.split(',')) raw.hotspot.push_back(part.trimmed().toLongLong());
        }
    }
    finish();
    if (!valid || icon.cell.isEmpty() || next > int64_t(size.width() / icon.cell.width()) * (size.height() / icon.cell.height())) return {};
    icon.metadata = std::move(metadata);
    return icon;
}
uint64_t DmiIcon::storageBytes() const {
    uint64_t bytes = uint64_t(sheet.sizeInBytes());
    for (const auto& [name, state] : states) {
        bytes += sizeof(State) + 128 + uint64_t(name.capacity()) * sizeof(QChar);
        if (state.ends) bytes += sizeof(*state.ends) + state.ends->capacity() * sizeof(int64_t);
    }
    if (metadata) {
        bytes += sizeof(*metadata) + metadata->capacity() * sizeof(assets::IconState);
        for (const auto& raw : *metadata)
            bytes += raw.name.capacity() + raw.delays.capacity() * sizeof(double) + raw.hotspot.capacity() * sizeof(int64_t);
    }
    return bytes;
}
assets::Icon DmiIcon::toAsset() const {
    assets::Icon out;
    out.width = cell.width(); out.height = cell.height();
    out.sheetCols = sheet.isNull() ? 1 : sheet.width() / cell.width();
    out.sheet.w = sheet.width(); out.sheet.h = sheet.height();
    const auto rgba = sheet.convertToFormat(QImage::Format_RGBA8888);
    out.sheet.px.resize(size_t(rgba.width()) * rgba.height() * 4);
    for (int y = 0; y < rgba.height(); ++y)
        std::copy_n(rgba.constScanLine(y), rgba.width() * 4, out.sheet.px.data() + size_t(y) * rgba.width() * 4);
    if (metadata) out.states = *metadata;
    else if (!sheet.isNull()) out.states.emplace_back(); // plain PNG
    return out;
}
DmiIcon DmiIcon::fromAsset(const assets::Icon& asset) {
    DmiIcon icon;
    if (asset.states.empty() && asset.sheet.px.empty() && asset.sheet.w == 0 && asset.sheet.h == 0) {
        icon.cell = QSize(0,0);
        icon.metadata = std::make_shared<const std::vector<assets::IconState>>();
        return icon; // a valid new /icon, distinct from an unavailable dependency
    }
    if (asset.width <= 0 || asset.height <= 0 || asset.sheet.w <= 0 || asset.sheet.h <= 0 ||
        asset.sheetCols != asset.sheet.w / asset.width ||
        uint64_t(asset.sheet.w) * asset.sheet.h > 64 * 1024 * 1024 ||
        asset.sheet.px.size() != uint64_t(asset.sheet.w) * asset.sheet.h * 4) return {};
    icon.cell = QSize(asset.width, asset.height);
    icon.sheet = QImage(asset.sheet.px.data(), asset.sheet.w, asset.sheet.h, asset.sheet.w * 4, QImage::Format_RGBA8888).copy();
    if (icon.sheet.isNull()) return {};
    int64_t cells = 0;
    for (const auto& raw : asset.states) {
        if ((raw.dirs != 1 && raw.dirs != 4 && raw.dirs != 8) || raw.frames < 1 || raw.frames > maxFrames ||
            raw.firstCell < 0 || raw.firstCell + raw.dirs * raw.frames > int64_t(asset.sheetCols) * (asset.sheet.h / asset.height)) return {};
        cells += raw.dirs * raw.frames;
        if (cells > maxFrames) return {};
        State state;
        state.first = int(raw.firstCell); state.dirs = int(raw.dirs); state.frames = int(raw.frames);
        state.loop = int(std::clamp<int64_t>(raw.loop, 0, 65536)); state.rewind = raw.rewind;
        auto ends = std::make_shared<std::vector<int64_t>>();
        int64_t total = 0;
        auto append = [&](int i) {
            const double value = size_t(i) < raw.delays.size() ? raw.delays[size_t(i)] : 1.;
            const int64_t duration = std::isfinite(value) && value > 0 && value <= 864000
                ? std::max<int64_t>(1000000, int64_t(std::llround(value * 100000000))) : defaultDelay;
            if (total > 365LL * 86400 * 1000000000 - duration) return false;
            ends->push_back(total += duration); return true;
        };
        for (int i = 0; i < state.frames; ++i) if (!append(i)) return {};
        if (state.rewind) for (int i = state.frames - 2; i > 0; --i) if (!append(i)) return {};
        state.ends = std::move(ends);
        (raw.movement ? icon.moving : icon.states).emplace(QString::fromUtf8(raw.name), std::move(state));
    }
    icon.metadata = std::make_shared<const std::vector<assets::IconState>>(asset.states);
    return icon;
}
int DmiIcon::State::frameAt(int64_t nowNs, int64_t startNs, int64_t originNs) const {
    if (!ends || ends->empty() || nowNs <= 0) return 0;
    // Infinite animations share one phase, counted from originNs (model::World::iconClockNs). Finite states use
    // the model bind stamp, never resource arrival, template creation or visibility.
    const uint64_t elapsed = loop == 0 ? uint64_t(nowNs > originNs ? nowNs - originNs : 0) :
        (nowNs > std::max<int64_t>(0, startNs) ? uint64_t(nowNs) - uint64_t(std::max<int64_t>(0, startNs)) : 0);
    const uint64_t cycle = uint64_t(ends->back());
    if (loop > 0 && elapsed / cycle >= uint64_t(loop)) return rewind ? 0 : frames - 1;
    const int step = int(std::upper_bound(ends->begin(), ends->end(), int64_t(elapsed % cycle)) - ends->begin());
    return step < frames ? step : 2 * frames - 2 - step;
}
int DmiIcon::State::flickFrameAt(int64_t nowNs, int64_t startNs) const {
    const uint64_t elapsed = nowNs > std::max<int64_t>(0, startNs)
        ? uint64_t(nowNs) - uint64_t(std::max<int64_t>(0, startNs)) : 0;
    if (!ends || ends->size() < size_t(frames)) return elapsed < defaultDelay ? 0 : -1;
    const auto end = ends->begin() + frames;
    if (elapsed >= uint64_t((*ends)[frames - 1])) return -1;
    return int(std::upper_bound(ends->begin(), end, int64_t(elapsed)) - ends->begin());
}
const DmiIcon::State* DmiIcon::state(const QString& name) const {
    auto it = states.find(name);
    if (it != states.end()) return &it->second;
    // A name that only has a movement state draws that state at rest too, before the unnamed default (stock
    // 516.1689, stock-parity-world run-still, run-overlay).
    if (it = moving.find(name); it != moving.end()) return &it->second;
    it = states.find(QString());
    return it == states.end() ? nullptr : &it->second;
}
int DmiIcon::index(const State& s, int dir, int frame) const {
    const int directions[] = {2, 1, 4, 8, 6, 10, 5, 9};
    // A four-direction state draws east for any dir with the EAST bit, else west, else north, else south: diagonals
    // take their horizontal part, and so do N|S (north), E|W (east), UP|E (stock 516.1689, stock-parity-world
    // d4-*). An eight-direction state draws south for anything that is not one of its eight (d8-dir*).
    // Resolve per state here, not on the atom: children may have eight directions.
    if (s.dirs == 4) dir = dir & 4 ? 4 : dir & 8 ? 8 : dir & 1 ? 1 : 2;
    int direction = 0;
    for (int i = 0; i < s.dirs; ++i) if (directions[i] == dir) direction = i;
    return s.first + direction + std::clamp(frame, 0, s.frames - 1) * s.dirs;
}
QImage DmiIcon::frame(const QString& name, int dir, int frameNumber) const {
    const auto* s = state(name);
    return s ? frame(index(*s, dir, frameNumber)) : QImage();
}
QImage DmiIcon::frame(int index) const {
    if (sheet.isNull() || cell.isEmpty() || index < 0) return {};
    const int columns = sheet.width() / cell.width();
    if (!columns) return {};
    const QRect source((index % columns) * cell.width(), (index / columns) * cell.height(), cell.width(), cell.height());
    return sheet.rect().contains(source) ? sheet.copy(source) : QImage();
}
}
