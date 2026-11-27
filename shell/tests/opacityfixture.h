#pragma once
#include "shell/tests/iconfixture.h"
#include <bit>
namespace opacityfixture {
// Captured BYOND 516.1688: icon(file), MapColors(identity RGB, alpha multiplier).
inline QByteArray recipe(uint32_t ordinal, float factor = 10000) {
    QByteArray program = QByteArray::fromHex("0100000000084a14");
    for (int i=0;i<4;++i) program[1+i] = char(ordinal >> (8*i));
    for (int i=0;i<20;++i) {
        const uint32_t bits = std::bit_cast<uint32_t>(i==15 ? factor : i==0 || i==5 || i==10 ? 1.f : 0.f);
        for (int j=0;j<4;++j) program += char(bits >> (8*j));
    }
    return iconfixture::recipe(program);
}
}
