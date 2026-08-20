#include <gtest/gtest.h>

#include "ChatFrameCodec.h"

namespace {

// T02-FRM-01
TEST(ChatFrameCodecTests, HeaderUsesBigEndianMessageIdAndBodyLength) {
    const auto bytes = ChatFrameCodec::EncodeHeader(0x1234, 0x0201);

    EXPECT_EQ(bytes, (ChatFrameCodec::HeaderBytes{0x12, 0x34, 0x02, 0x01}));
    const auto decoded = ChatFrameCodec::DecodeHeader(bytes.data());
    EXPECT_EQ(decoded.message_id, 0x1234);
    EXPECT_EQ(decoded.body_length, 0x0201);
}

// T02-FRM-02
TEST(ChatFrameCodecTests, ZeroLengthAndMaximumLengthHeadersAreSupported) {
    const auto empty_bytes = ChatFrameCodec::EncodeHeader(7, 0);
    const auto maximum_bytes = ChatFrameCodec::EncodeHeader(MAX_LENGTH, MAX_LENGTH);
    const auto empty = ChatFrameCodec::DecodeHeader(empty_bytes.data());
    const auto maximum = ChatFrameCodec::DecodeHeader(maximum_bytes.data());

    EXPECT_TRUE(ChatFrameCodec::IsSupported(empty));
    EXPECT_EQ(empty.body_length, 0);
    EXPECT_TRUE(ChatFrameCodec::IsSupported(maximum));
}

// T02-FRM-03
TEST(ChatFrameCodecTests, ValuesAboveTheCurrentProtocolMaximumAreRejected) {
    const auto invalid_id_bytes = ChatFrameCodec::EncodeHeader(MAX_LENGTH + 1, 1);
    const auto invalid_length_bytes = ChatFrameCodec::EncodeHeader(1, MAX_LENGTH + 1);
    const auto invalid_id = ChatFrameCodec::DecodeHeader(invalid_id_bytes.data());
    const auto invalid_length = ChatFrameCodec::DecodeHeader(invalid_length_bytes.data());

    EXPECT_FALSE(ChatFrameCodec::IsSupported(invalid_id));
    EXPECT_FALSE(ChatFrameCodec::IsSupported(invalid_length));
}

} // namespace
