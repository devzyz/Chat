#pragma once

#include <array>
#include <cstdint>

#include "Const.h"

struct ChatFrameHeader {
    std::uint16_t message_id = 0;
    std::uint16_t body_length = 0;
};

class ChatFrameCodec {
public:
    using HeaderBytes = std::array<std::uint8_t, HEAD_TOTAL_LEN>;

    static HeaderBytes EncodeHeader(std::uint16_t message_id, std::uint16_t body_length);
    static ChatFrameHeader DecodeHeader(const void* bytes);
    static bool IsSupported(const ChatFrameHeader& header);
};
