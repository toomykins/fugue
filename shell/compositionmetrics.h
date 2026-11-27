// Latest model-free composition workload; caps are warning thresholds, not admission limits.
#pragma once
#include <QString>
#include <array>
#include <cstdint>

namespace fugue::shell {
struct CompositionMetrics {
    static constexpr uint64_t generatedCap = 4096, passCap = 2048, surfaceCap = 64;
    static constexpr uint64_t pixelCap = 1024ull*1024*1024;
    bool available = false;
    uint64_t generated = 0, passes = 0, surfaces = 0, workPixels = 0, storagePixels = 0;
    struct Row { const char* label; uint64_t value, cap; };
    std::array<Row,5> rows() const {
        return {{{"Generated passes",generated,generatedCap}, {"Live passes",passes,passCap},
                 {"Surfaces",surfaces,surfaceCap}, {"Work pixels",workPixels,pixelCap},
                 {"Storage pixels",storagePixels,pixelCap}}};
    }
    QString html() const {
        QString result = "COMPOSITION";
        for (const auto& row : rows()) {
            const bool warning = available && row.value > row.cap;
            const auto line = QString("%1 %2 / %3%4").arg(row.label,-16)
                .arg(available ? QString::number(row.value) : "--").arg(row.cap).arg(warning ? " !" : "");
            result += "\n<span style=\"color:" + QString(warning ? "#ffbf69" : "#e8edf2") +
                "\">" + line.toHtmlEscaped() + "</span>";
        }
        return result;
    }
};
}
