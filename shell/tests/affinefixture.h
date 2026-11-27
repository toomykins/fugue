#pragma once
#include <QString>
#include <array>
#include <bit>
#include <string>
namespace affinefixture {
inline std::string fields(std::array<float,6> matrix, const std::string& extra = {}) {
    QString out = "{\"hasTransform\":true,\"transform\":[";
    for (int i = 0; i < 6; ++i) {
        if (i) out += ',';
        out += QString("{\"f32\":\"0x%1\"}").arg(std::bit_cast<uint32_t>(matrix[i]),8,16,QChar('0'));
    }
    return out.toStdString() + "]" + extra + "}";
}
}
