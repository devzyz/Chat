#pragma once

#include "ResourceStore.h"
#include <zlib.h>
#include <cstring>

namespace resource {
// The editor emits non-interlaced 8-bit RGB/RGBA PNG. Bound decompression before
// accepting a public profile image, rather than trusting its extension or IHDR.
inline void ValidateAvatarPng(const std::vector<char>& bytes)
{
    auto reject = [] { throw Error(415, "avatar must be a valid 256x256 RGB/RGBA PNG"); };
    if (bytes.size() < 57 || bytes.size() > 1024 * 1024 ||
        std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) != 0) reject();
    auto number = [&bytes](std::size_t at) {
        std::uint32_t result = 0;
        for (int i = 0; i < 4; ++i) result = (result << 8) | static_cast<unsigned char>(bytes[at + i]);
        return result;
    };
    std::vector<unsigned char> compressed;
    int channels = 0;
    bool ended = false, data_ended = false;
    for (std::size_t at = 8; at < bytes.size();) {
        if (bytes.size() - at < 12) reject();
        const auto length = number(at);
        if (length > bytes.size() - at - 12) reject();
        const std::string type(bytes.data() + at + 4, 4);
        const auto crc = crc32(0, reinterpret_cast<const Bytef*>(bytes.data() + at + 4), length + 4);
        if (crc != number(at + 8 + length)) reject();
        if (at == 8) {
            if (type != "IHDR" || length != 13 || number(at + 8) != 256 || number(at + 12) != 256 ||
                bytes[at + 16] != 8 || (bytes[at + 17] != 2 && bytes[at + 17] != 6) ||
                bytes[at + 18] != 0 || bytes[at + 19] != 0 || bytes[at + 20] != 0) reject();
            channels = bytes[at + 17] == 6 ? 4 : 3;
        } else if (type == "IDAT") {
            if (data_ended) reject();
            compressed.insert(compressed.end(), bytes.begin() + at + 8, bytes.begin() + at + 8 + length);
        } else if (type == "IEND") {
            if (length != 0 || at + 12 != bytes.size()) reject();
            ended = true;
        } else {
            if (!compressed.empty()) data_ended = true;
            // No other critical chunks are needed by the normalized editor output.
            if ((static_cast<unsigned char>(type[0]) & 32) == 0) reject();
        }
        at += length + 12;
    }
    if (!ended || compressed.empty()) reject();
    const auto stride = 1 + 256 * channels;
    std::vector<unsigned char> decoded(stride * 256);
    uLongf size = static_cast<uLongf>(decoded.size());
    uLong source_size = static_cast<uLong>(compressed.size());
    if (uncompress2(decoded.data(), &size, compressed.data(), &source_size) != Z_OK ||
        size != decoded.size() || source_size != compressed.size()) reject();
    for (int row = 0; row < 256; ++row) if (decoded[row * stride] > 4) reject();
}
}
