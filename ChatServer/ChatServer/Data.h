#pragma once
#include <string>

// 用户基本信息
struct UserInfo {
	UserInfo() : _uid(0), _name(""), _password(""), _email(""), _description(""), _icon(""), _sex(0) {}
	int _uid; // id
	std::string _name; // 昵称
	std::string _password; // 密码
	std::string _email; // 邮箱
	std::string _description; // 个性签名
	std::string _icon; // 头像
	int _sex; // 性别
    std::string _backname; // 某个客户端给的当前uid的备注名
};

// 申请好友的信息
class ApplyInfo {
public:
    ApplyInfo() {};
    ApplyInfo(int apply_uid, std::string apply_name, std::string apply_description,
        std::string apply_icon, int apply_sex, int status, int to_uid, std::string description, std::string backanme)
        :_apply_uid(apply_uid), _apply_name(apply_name), _apply_description(apply_description),
        _apply_icon(apply_icon), _apply_sex(apply_sex), _status(status), _to_uid(to_uid),
        _description(description), _backname(backanme) {
    }

    int _apply_uid; // 申请人id
    std::string _apply_name; // 申请人用户名
    std::string _apply_description; // 申请人用户请求描述信息
    std::string _apply_icon; // 申请人头像
    int _apply_sex; // 申请人性别
    int _status; // 状态，是否添加
    int _to_uid; // 被申请人id
    std::string _description; // 申请人发送的描述信息
    std::string _backname; // 申请人给被申请人的备注名
};

// 认证好友的信息
class AuthInfo {
public:
    AuthInfo() {};
    AuthInfo(int auth_uid, std::string auth_name, std::string auth_description,
        std::string auth_icon, int auth_sex, int status, int to_uid, std::string description, std::string backanme)
        :_auth_uid(auth_uid), _auth_name(auth_name), _auth_description(auth_description),
        _auth_icon(auth_icon), _auth_sex(auth_sex), _status(status), _to_uid(to_uid),
        _description(description), _backname(backanme) {
    }

    int _auth_uid; // 认证人id
    std::string _auth_name; // 认证人用户名
    std::string _auth_description; // 认证人用户请求描述信息
    std::string _auth_icon; // 认证人头像
    int _auth_sex; // 认证人性别
    int _status; // 状态，是否添加
    int _to_uid; // 被认证人id
    std::string _description; // 认证人发送的描述信息
    std::string _backname; // 认证人给被认证人的备注名
};

class ChatMessage {
public:
    ChatMessage(int message_id, int chat_id, int send_id, int recv_id, std::string content, int status) : 
        _message_id(message_id), _chat_id(chat_id), _send_id(send_id), _recv_id(recv_id), _content(content), _status(status) {}
    ChatMessage(int message_id, std::string client_msg_id, int chat_id, int send_id, int recv_id, std::string content, int status) :
        _message_id(message_id), _client_msg_id(client_msg_id), _chat_id(chat_id), _send_id(send_id), 
        _recv_id(recv_id), _content(content), _status(status) {
    }
    ChatMessage(int message_id, int chat_id, int send_id, int recv_id, std::string content, int status, int64_t created_at) :
        _message_id(message_id), _chat_id(chat_id), _send_id(send_id), _recv_id(recv_id), _content(content), _status(status), _created_at(created_at){
    }
    int _message_id;
    std::string _client_msg_id;
    int _chat_id;
    int _send_id;
    int _recv_id;
    std::string _content;
    int _status;
    int64_t _created_at;
};

class ChatInfoBase {
public:
    ChatInfoBase(){}
    ChatInfoBase(std::string type, int chat_id) : _type(type), _chat_id(chat_id) {}
    virtual ~ChatInfoBase() = default;
    // 当前会话的类型，私聊或群聊
    std::string _type;
    // 会话的唯一id
    int _chat_id;
};

class PrivateChatInfo : public ChatInfoBase{
public:
    PrivateChatInfo() {}
    PrivateChatInfo(std::string type, int chat_id, int user1_id, int user2_id) : 
        ChatInfoBase(type, chat_id), _user1_id(user1_id), _user2_id(user2_id) {}
    int _user1_id;
    int _user2_id;
};

class GroupChatInfo : public ChatInfoBase{
public:
    GroupChatInfo() {}
    GroupChatInfo(std::string type, int chat_id, std::string group_name) : 
        ChatInfoBase(type, chat_id), _group_name(group_name) {}
    // 群聊的名称
    std::string _group_name; 
};