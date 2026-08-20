#include "ChatFrameCodec.h"

ChatFrameCodec::HeaderBytes ChatFrameCodec::EncodeHeader(
    std::uint16_t message_id,
    std::uint16_t body_length) {
    return {
        static_cast<std::uint8_t>((message_id >> 8) & 0xff),
        static_cast<std::uint8_t>(message_id & 0xff),
        static_cast<std::uint8_t>((body_length >> 8) & 0xff),
        static_cast<std::uint8_t>(body_length & 0xff)
    };
}

ChatFrameHeader ChatFrameCodec::DecodeHeader(const void* bytes) {
    const auto* header = static_cast<const std::uint8_t*>(bytes);
    return {
        static_cast<std::uint16_t>((header[0] << 8) | header[1]),
        static_cast<std::uint16_t>((header[2] << 8) | header[3])
    };
}

bool ChatFrameCodec::IsSupported(const ChatFrameHeader& header) {
    return header.message_id <= MAX_LENGTH && header.body_length <= MAX_LENGTH;
}
