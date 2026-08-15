#include <gtest/gtest.h>

#include "MsgNode.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace {

short ReadNetworkShort(const char* bytes) {
    unsigned short network_value = 0;
    std::memcpy(&network_value, bytes, sizeof(network_value));
    return static_cast<short>(boost::asio::detail::socket_ops::network_to_host_short(network_value));
}

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

TEST(SendNodeTests, CharBufferEncodesHeaderInNetworkOrderAndCopiesBody) {
    const char body[] = {'A', '\0', 'B', static_cast<char>(0xff)};

    SendNode node(body, 0x1234, static_cast<short>(sizeof(body)));

    EXPECT_EQ(node._msg_id, 0x1234);
    EXPECT_EQ(node._total_len, HEAD_TOTAL_LEN + sizeof(body));
    EXPECT_EQ(ReadNetworkShort(node._data), 0x1234);
    EXPECT_EQ(ReadNetworkShort(node._data + HEAD_ID_LEN), sizeof(body));
    EXPECT_EQ(std::memcmp(node._data + HEAD_TOTAL_LEN, body, sizeof(body)), 0);
}

TEST(SendNodeTests, StringConstructorPreservesEmbeddedNullBytes) {
    const std::string body("left\0right", 10);

    SendNode node(body, 42, static_cast<short>(body.size()));

    EXPECT_EQ(ReadNetworkShort(node._data), 42);
    EXPECT_EQ(ReadNetworkShort(node._data + HEAD_ID_LEN), body.size());
    EXPECT_EQ(std::memcmp(node._data + HEAD_TOTAL_LEN, body.data(), body.size()), 0);
}

TEST(SendNodeTests, EmptyBodyProducesHeaderOnlyPacket) {
    SendNode node(std::string(), 7, 0);

    EXPECT_EQ(node._total_len, HEAD_TOTAL_LEN);
    EXPECT_EQ(ReadNetworkShort(node._data), 7);
    EXPECT_EQ(ReadNetworkShort(node._data + HEAD_ID_LEN), 0);
    EXPECT_EQ(node._data[HEAD_TOTAL_LEN], '\0');
}

TEST(SendNodeTests, MaximumApplicationBodyLengthIsCopiedWithoutTruncation) {
    const std::string body(MAX_LENGTH, 'x');

    SendNode node(body, 1024, static_cast<short>(body.size()));

    EXPECT_EQ(node._total_len, MAX_LENGTH + HEAD_TOTAL_LEN);
    EXPECT_EQ(ReadNetworkShort(node._data + HEAD_ID_LEN), MAX_LENGTH);
    EXPECT_EQ(std::memcmp(node._data + HEAD_TOTAL_LEN, body.data(), body.size()), 0);
}

TEST(RecvNodeTests, ConstructorRetainsMessageIdentityAndClearReusesBuffer) {
    RecvNode node(MAX_LENGTH, 1016);
    node._cur_len = 12;
    std::memset(node._data, 0x3c, MAX_LENGTH);

    node.Clear();

    EXPECT_EQ(node._msg_id, 1016);
    EXPECT_EQ(node._total_len, MAX_LENGTH);
    EXPECT_EQ(node._cur_len, 0);
    EXPECT_TRUE(std::all_of(node._data, node._data + MAX_LENGTH, [](char value) { return value == 0; }));
}

} // namespace
