#include <gtest/gtest.h>

#include "ChatFrameCodec.h"

namespace {

// T02-FRM-01
TEST(ChatFrameCodecTests, ValidatedHeaderPreservesHighBitMessageIdInNetworkOrder) {
    const auto bytes = ChatFrameCodec::EncodeHeader(0x9234, 0x0201);

    EXPECT_EQ(bytes, (ChatFrameCodec::HeaderBytes{0x92, 0x34, 0x02, 0x01}));
    const auto decoded = ChatFrameCodec::DecodeValidatedHeader(bytes.data(), MAX_LENGTH);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->message_id, 0x9234);
    EXPECT_EQ(decoded->body_length, 0x0201);
}

// T02-FRM-02
TEST(ChatFrameCodecTests, MaximumBodyLengthAcceptsAnUnknownMessageId) {
    const auto bytes = ChatFrameCodec::EncodeHeader(0xffff, MAX_LENGTH);

    const auto decoded = ChatFrameCodec::DecodeValidatedHeader(bytes.data(), MAX_LENGTH);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->message_id, 0xffff);
    EXPECT_EQ(decoded->body_length, MAX_LENGTH);
}

// T02-FRM-03
TEST(ChatFrameCodecTests, BodyLengthAboveTheReceiveBufferLimitIsRejected) {
    const auto bytes = ChatFrameCodec::EncodeHeader(1, 0x8000);

    EXPECT_FALSE(ChatFrameCodec::DecodeValidatedHeader(bytes.data(), MAX_LENGTH).has_value());
}

} // namespace
