#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "Const.h"

struct ChatFrameHeader {
    std::uint16_t message_id = 0;
    std::uint16_t body_length = 0;
};

class ChatFrameCodec {
public:
    using HeaderBytes = std::array<std::uint8_t, HEAD_TOTAL_LEN>;

    static HeaderBytes EncodeHeader(std::uint16_t message_id, std::uint16_t body_length);
    static std::optional<ChatFrameHeader> DecodeValidatedHeader(
        const void* bytes,
        std::size_t maximum_body_length);
};
