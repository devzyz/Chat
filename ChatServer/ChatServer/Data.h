#pragma once
#include <string>

// 用户基本信息
/** @brief 保存查询得到的用户资料；仅作为进程内数据对象，不控制会话生命周期。 */
struct UserInfo {
	/** @brief 初始化UserInfo，保存查询得到的用户资料；仅作为进程内数据对象，不控制会话生命周期。 */
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

/** @brief 保存好友申请人的资料、申请内容及其给接收方的备注名。 */
class ApplyInfo {
public:
    /** @brief 创建待填充的好友申请，标量字段由调用方赋值。 */
    ApplyInfo() {};
    /** @brief 使用查询到的用户资料和申请记录构造好友申请。 */
    ApplyInfo(int apply_uid, std::string apply_name, std::string apply_description,
        std::string apply_icon, int apply_sex, int status, int to_uid, std::string description, std::string backname)
        :_apply_uid(apply_uid), _apply_name(apply_name), _apply_description(apply_description),
        _apply_icon(apply_icon), _apply_sex(apply_sex), _status(status), _to_uid(to_uid),
        _description(description), _backname(backname) {
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

/** @brief 保存好友审批人的资料、审批状态及其给对方的备注名。 */
class AuthInfo {
public:
    /** @brief 创建待填充的好友审批信息，标量字段由调用方赋值。 */
    AuthInfo() {};
    /** @brief 使用审批人资料和审批记录构造好友确认信息。 */
    AuthInfo(int auth_uid, std::string auth_name, std::string auth_description,
        std::string auth_icon, int auth_sex, int status, int to_uid, std::string description, std::string backname)
        :_auth_uid(auth_uid), _auth_name(auth_name), _auth_description(auth_description),
        _auth_icon(auth_icon), _auth_sex(auth_sex), _status(status), _to_uid(to_uid),
        _description(description), _backname(backname) {
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

/** @brief 保存消息身份、参与者、内容和时间，供数据库结果与协议响应之间传递。 */
class ChatMessage {
public:
    /** @brief 保存普通消息身份、参与者、正文与状态；创建时间尚未赋值，读取前须由调用方补齐。 */
    ChatMessage(int message_id, int chat_id, int send_id, int recv_id, std::string content, int status) : 
        _message_id(message_id), _chat_id(chat_id), _send_id(send_id), _recv_id(recv_id), _content(content), _status(status) {}
    /** @brief 额外保存客户端 UUID；创建时间尚未赋值，读取前须由调用方补齐。 */
    ChatMessage(int message_id, std::string client_msg_id, int chat_id, int send_id, int recv_id, std::string content, int status) :
        _message_id(message_id), _client_msg_id(client_msg_id), _chat_id(chat_id), _send_id(send_id), 
        _recv_id(recv_id), _content(content), _status(status) {
    }
    /** @brief 保存包含持久化创建时间的消息；客户端 UUID 保持空字符串。 */
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

/** @brief 保存聊天会话共有字段，派生类型补充私聊或群聊资料。 */
class ChatInfoBase {
public:
    /** @brief 创建待填充的会话基类，会话编号读取前须由调用方赋值。 */
    ChatInfoBase(){}
    /** @brief 以类型和会话编号初始化共有字段。 */
    ChatInfoBase(std::string type, int chat_id) : _type(type), _chat_id(chat_id) {}
    /** @brief 提供虚析构，允许经基类指针销毁派生会话资料。 */
    virtual ~ChatInfoBase() = default;
    // 当前会话的类型，私聊或群聊
    std::string _type;
    // 会话的唯一id
    int _chat_id;
};

/** @brief 保存私聊会话及对方用户资料。 */
class PrivateChatInfo : public ChatInfoBase{
public:
    /** @brief 创建待填充的私聊资料，会话及参与者编号读取前须赋值。 */
    PrivateChatInfo() {}
    /** @brief 以会话类型、编号和双方用户编号构造私聊资料。 */
    PrivateChatInfo(std::string type, int chat_id, int user1_id, int user2_id) : 
        ChatInfoBase(type, chat_id), _user1_id(user1_id), _user2_id(user2_id) {}
    int _user1_id;
    int _user2_id;
};

/** @brief 保存群聊会话及群组展示资料。 */
class GroupChatInfo : public ChatInfoBase{
public:
    /** @brief 创建待填充的群聊资料，会话编号读取前须赋值。 */
    GroupChatInfo() {}
    /** @brief 以会话类型、编号及群名称构造群聊资料。 */
    GroupChatInfo(std::string type, int chat_id, std::string group_name) : 
        ChatInfoBase(type, chat_id), _group_name(group_name) {}
    // 群聊的名称
    std::string _group_name; 
};
