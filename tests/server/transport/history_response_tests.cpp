#include <iostream>
#include <stdexcept>
#include "HistoryResponse.h"
#include "ChatFrameCodec.h"

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// E03-RECOVER-CONTRACT-01
int main() {
    try {
        Json::Value page;
        page["error"] = 0;
        page["chat_id"] = 7;
        page["load_more"] = false;
        page["current_msg_id"] = 10;
        for (int index = 1; index <= 10; ++index) {
            Json::Value row;
            row["message_id"] = index;
            row["msg_uuid"] = "12345678-1234-4234-8234-123456789012";
            row["send_id"] = 41;
            row["recv_id"] = 42;
            row["content"] = std::string(180, 'x');
            row["status"] = 1;
            row["created_at"] = Json::Int64(1789300000);
            page["msgs"].append(row);
        }
        const auto body = SerializeHistoryResponse(page, 0);
        std::cout << "history response bytes=" << body.size() << '\n';
        Require(body.size() <= MAX_LENGTH, "history response exceeds production frame limit");
        const auto header = ChatFrameCodec::EncodeHeader(1028, static_cast<std::uint16_t>(body.size()));
        Require(ChatFrameCodec::DecodeValidatedHeader(header.data(), MAX_LENGTH).has_value(), "frame rejected");
        Json::Value decoded;
        Json::Reader reader;
        Require(reader.parse(body, decoded), "invalid response JSON");
        const auto count = decoded["msgs"].size();
        Require(count > 0 && count < 10, "oversized page must return a nonempty prefix");
        Require(decoded["load_more"].asBool(), "truncated page must retain more state");
        Require(decoded["current_msg_id"].asUInt() == count, "cursor must identify last returned row");
        for (Json::ArrayIndex index = 0; index < count; ++index)
            Require(decoded["msgs"][index] == page["msgs"][index], "row changed or skipped");
        // Traversal must return every row once, including the shortened final page.
        unsigned cursor = 0;
        do {
            Json::Value remainder = page;
            remainder["msgs"] = Json::Value(Json::arrayValue);
            for (unsigned index = cursor; index < 10; ++index) remainder["msgs"].append(page["msgs"][index]);
            const auto next = SerializeHistoryResponse(remainder, static_cast<int>(cursor));
            Require(next.size() <= MAX_LENGTH && reader.parse(next, decoded), "invalid bounded page");
            Require(decoded["msgs"].size() > 0, "scan stopped before final row");
            for (const auto& row : decoded["msgs"]) {
                Require(row == page["msgs"][cursor], "scan skipped or duplicated row");
                ++cursor;
            }
            Require(decoded["current_msg_id"].asUInt() == cursor, "scan cursor mismatch");
            Require(decoded["load_more"].asBool() == (cursor < 10), "scan termination mismatch");
        } while (cursor < 10);
        page["msgs"] = Json::Value(Json::arrayValue);
        Require(reader.parse(SerializeHistoryResponse(page, 10), decoded), "empty page parse");
        Require(decoded["current_msg_id"].asInt() == 10 && !decoded["load_more"].asBool(), "empty page cursor");
        Json::Value boundary = page;
        Json::Value boundary_row;
        boundary_row["message_id"] = 11;
        boundary_row["content"] = "";
        boundary["msgs"].append(boundary_row);
        boundary["current_msg_id"] = 11;
        const auto overhead = SerializeHistoryResponse(boundary, 10).size();
        boundary["msgs"][0]["content"] = std::string(MAX_LENGTH - overhead, 'x');
        const auto exact = SerializeHistoryResponse(boundary, 10);
        Require(exact.size() == MAX_LENGTH && reader.parse(exact, decoded) &&
            decoded["msgs"].size() == 1, "exact frame boundary rejected");
        boundary["msgs"][0]["content"] = std::string(MAX_LENGTH - overhead + 1, 'x');
        Require(reader.parse(SerializeHistoryResponse(boundary, 10), decoded) &&
            decoded["error"].asInt() == ErrorCodes::Error_Json, "one byte overflow accepted");
        Json::Value giant;
        giant["message_id"] = 11;
        giant["content"] = std::string(2048, '\n');
        page["msgs"].append(giant);
        page["current_msg_id"] = 11;
        Require(reader.parse(SerializeHistoryResponse(page, 10), decoded), "oversized row parse");
        Require(decoded["error"].asInt() == ErrorCodes::Error_Json && !decoded.isMember("msgs"),
            "unrepresentable row must fail without a looping cursor");
        page["error"] = ErrorCodes::UidInvalid;
        page.removeMember("msgs");
        Require(reader.parse(SerializeHistoryResponse(page, 10), decoded) &&
            decoded["error"].asInt() == ErrorCodes::UidInvalid, "membership error changed");
        std::cout << "history response contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
