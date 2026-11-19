// SHA-256 (FIPS 180-4). Qt-free so model/ stays dependency-free.
#pragma once
#include <cstdint>
#include <string>

#include "net/bytes.h"

namespace fugue::model {

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t n);
    // Finishes the hash and returns 64 lowercase hex digits. The object must not be reused.
    std::string hexDigest();

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t fill_ = 0;
    uint64_t total_ = 0;
};

std::string sha256Hex(const void* data, size_t n);

}  // namespace fugue::model
