#include "userdata.h"

void FriendInfo::AddTextChatMsg(int from_uid, int to_uid, QJsonArray text_array)
{
    for (const auto &msg: text_array) {
        QJsonObject obj = msg.toObject();
        auto msg_id = obj["msg_id"].toString();
        auto msg_content = obj["msg_content"].toString();

        auto text_msg = std::make_shared<TextChatData> (from_uid, to_uid, msg_id, msg_content);
        _chat_msgs.push_back(text_msg);
    }
}
