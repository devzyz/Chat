#pragma once

#include <string>
#include <json/json.h>
#include "Const.h"

inline std::string SerializeHistoryResponse(Json::Value response, int request_cursor) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    auto body = Json::writeString(writer, response);
    // The row limit alone cannot bound encoded UTF-8/escaped message content.
    while (body.size() > MAX_LENGTH && response["msgs"].isArray() && !response["msgs"].empty()) {
        auto& messages = response["msgs"];
        messages.resize(messages.size() - 1);
        response["load_more"] = true;
        response["current_msg_id"] = messages.empty() ? Json::Value(request_cursor) :
            messages[messages.size() - 1]["message_id"];
        body = Json::writeString(writer, response);
    }
    if (body.size() > MAX_LENGTH ||
        (response["error"].asInt() == ErrorCodes::Success && response["load_more"].asBool() &&
         response["msgs"].empty())) {
        // A single unrepresentable legacy row must fail instead of looping at one cursor.
        Json::Value failure;
        failure["error"] = ErrorCodes::Error_Json;
        failure["chat_id"] = response["chat_id"];
        body = Json::writeString(writer, failure);
    }
    return body;
}
