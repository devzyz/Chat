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

std::optional<ChatFrameHeader> ChatFrameCodec::DecodeValidatedHeader(
    const void* bytes,
    std::size_t maximum_body_length) {
    const auto* header = static_cast<const std::uint8_t*>(bytes);
    const ChatFrameHeader decoded{
        static_cast<std::uint16_t>((header[0] << 8) | header[1]),
        static_cast<std::uint16_t>((header[2] << 8) | header[3])
    };
    if (decoded.body_length > maximum_body_length) {
        return std::nullopt;
    }
    return decoded;
}
