#include <gtest/gtest.h>

#include "MsgNode.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

TEST(MsgNodeTests, ConstructorCreatesZeroedNullTerminatedBuffer) {
    MsgNode node(8);

    EXPECT_EQ(node._cur_len, 0);
    EXPECT_EQ(node._total_len, 8);
    EXPECT_TRUE(std::all_of(node._data, node._data + 9, [](char value) { return value == 0; }));
}

TEST(MsgNodeTests, ZeroLengthNodeStillProvidesTerminator) {
    MsgNode node(0);

    EXPECT_EQ(node._total_len, 0);
    EXPECT_EQ(node._data[0], '\0');
}

TEST(MsgNodeTests, ClearResetsProgressAndEntirePayloadBuffer) {
    MsgNode node(6);
    node._cur_len = 4;
    std::memset(node._data, 0x5a, 6);

    node.Clear();

    EXPECT_EQ(node._cur_len, 0);
    EXPECT_TRUE(std::all_of(node._data, node._data + 6, [](char value) { return value == 0; }));
    EXPECT_EQ(node._data[6], '\0');
}

TEST(SendNodeTests, StringConstructorPreservesEmbeddedNullBytes) {
    const std::string body("left\0right", 10);

    SendNode node(body, 42, body.size());

    EXPECT_EQ(node._msg_id, 42);
    EXPECT_EQ(node._total_len, HEAD_TOTAL_LEN + body.size());
    EXPECT_EQ(std::memcmp(node._data + HEAD_TOTAL_LEN, body.data(), body.size()), 0);
}

TEST(SendNodeTests, MaximumApplicationBodyLengthIsCopiedWithoutTruncation) {
    const std::string body(MAX_LENGTH, 'x');

    SendNode node(body, 1024, body.size());

    EXPECT_EQ(node._total_len, MAX_LENGTH + HEAD_TOTAL_LEN);
    EXPECT_EQ(std::memcmp(node._data + HEAD_TOTAL_LEN, body.data(), body.size()), 0);
}

TEST(SendNodeTests, OversizedApplicationBodyIsRejectedBeforeAllocation) {
    const std::string body(MAX_LENGTH + 1, 'x');

    EXPECT_THROW(SendNode(body, 1024, body.size()), std::length_error);
}

TEST(SendNodeTests, DeclaredLengthCannotExceedTheSourceString) {
    const std::string body("short");

    EXPECT_THROW(SendNode(body, 1024, body.size() + 1), std::invalid_argument);
}

TEST(RecvNodeTests, ConstructorRetainsMessageIdentityAndClearReusesBuffer) {
    RecvNode node(MAX_LENGTH, 0xffff);
    node._cur_len = 12;
    std::memset(node._data, 0x3c, MAX_LENGTH);

    node.Clear();

    EXPECT_EQ(node._msg_id, 0xffff);
    EXPECT_EQ(node._total_len, MAX_LENGTH);
    EXPECT_EQ(node._cur_len, 0);
    EXPECT_TRUE(std::all_of(node._data, node._data + MAX_LENGTH, [](char value) { return value == 0; }));
}

} // namespace
