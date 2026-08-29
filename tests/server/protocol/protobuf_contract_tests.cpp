#include <gtest/gtest.h>

#include "chat.pb.h"
#include "varify.pb.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

TEST(ProtobufContractTests, TextChatRequestRoundTripsAllRoutingAndMessageFields) {
    message::TextChatMsgReq request;
    request.set_fromuid(101);
    request.set_touid(202);
    request.set_chatid(303);

    auto* first = request.add_textmsgs();
    first->set_uuid("message-1");
    first->set_msgcontent(std::string("left\0right", 10));
    first->set_msgid(1001);

    auto* second = request.add_textmsgs();
    second->set_uuid("message-2");
    second->set_msgcontent(u8"你好，ChatServer");
    second->set_msgid(1002);

    std::string wire_payload;
    ASSERT_TRUE(request.SerializeToString(&wire_payload));
    ASSERT_FALSE(wire_payload.empty());

    message::TextChatMsgReq parsed;
    ASSERT_TRUE(parsed.ParseFromString(wire_payload));
    EXPECT_EQ(parsed.fromuid(), 101);
    EXPECT_EQ(parsed.touid(), 202);
    EXPECT_EQ(parsed.chatid(), 303);
    ASSERT_EQ(parsed.textmsgs_size(), 2);
    EXPECT_EQ(parsed.textmsgs(0).uuid(), "message-1");
    EXPECT_EQ(parsed.textmsgs(0).msgcontent(), std::string("left\0right", 10));
    EXPECT_EQ(parsed.textmsgs(0).msgid(), 1001);
    EXPECT_EQ(parsed.textmsgs(1).uuid(), "message-2");
    EXPECT_EQ(parsed.textmsgs(1).msgcontent(), u8"你好，ChatServer");
    EXPECT_EQ(parsed.textmsgs(1).msgid(), 1002);
}

TEST(ProtobufContractTests, VerifyResponsePreservesErrorEmailAndCode) {
    message::GetVarifyRsp response;
    response.set_error(2);
    response.set_email("person@example.com");
    response.set_code("042731");

    const std::string wire_payload = response.SerializeAsString();
    message::GetVarifyRsp parsed;

    ASSERT_TRUE(parsed.ParseFromString(wire_payload));
    EXPECT_EQ(parsed.error(), 2);
    EXPECT_EQ(parsed.email(), "person@example.com");
    EXPECT_EQ(parsed.code(), "042731");
}

TEST(ProtobufContractTests, CurrentCppConsumerParsesInitialWireFixtureWithUnknownFields) {
    const auto fixture_root = std::filesystem::path(__FILE__).parent_path() / "fixtures";
    for (const char* fixture_name : {"varify-request-v1.bin", "varify-request-v1-unknown.bin"}) {
        const auto fixture_path = fixture_root / fixture_name;
        std::ifstream input(fixture_path, std::ios::binary);
        ASSERT_TRUE(input.is_open()) << fixture_path.string();
        const std::string payload(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );

        message::GetVarifyReq request;
        ASSERT_TRUE(request.ParseFromString(payload)) << fixture_name;
        EXPECT_EQ(request.email(), "compat-user@example.test") << fixture_name;
    }
}

} // namespace
