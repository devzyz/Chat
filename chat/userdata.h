#ifndef USERDATA_H
#define USERDATA_H

#include <QString>
#include <vector>
#include <QJsonArray>
#include <memory>
#include <QJsonObject>

/**
 * @brief The SearchInfo class
 * 搜索好友的信息
 */
struct SearchInfo {
    SearchInfo(int uid, QString name, QString description, QString icon, int sex) :
        _uid(uid), _name(name), _description(description), _sex(sex), _icon(icon) {}
    int _uid;
    QString _name;
    QString _description;
    QString _icon;
    int _sex;
};

/**
 * @brief The ApplyInfo class
 * 这是ApplyFriendPage显示的新朋友申请列表的item的信息
 */
struct ApplyInfo {
    ApplyInfo(int uid, QString name, QString description,
              QString icon, int sex, int status)
        :_uid(uid),_name(name),_description(description),
        _icon(icon),_sex(sex),_status(status){}

    int _uid; // 用户id
    QString _name; // 用户名
    QString _description; // 用户请求描述信息
    QString _icon; // 头像
    int _sex; // 性别
    int _status; // 状态，是否添加
};

/**
 * @brief The AuthInfo class
 * 认证好友的信息
 */
struct AuthInfo {
    AuthInfo(int uid, QString name, QString description, QString icon, int sex):
        _uid(uid), _name(name), _description(description), _icon(icon),
        _sex(sex){}
    int _uid;
    QString _name;
    QString _description;
    QString _icon;
    int _sex;
};

// 一条消息
struct TextChatData {
    TextChatData (int from_uid, int to_uid, QString msg_id, QString msg_content) :
        _msg_id(msg_id), _msg_content(msg_content), _from_uid(from_uid), _to_uid(to_uid) {}

    QString _msg_id;
    QString _msg_content;
    int _from_uid;
    int _to_uid;
};

// 多条消息
struct TextChatMsg {
    TextChatMsg(int from_uid, int to_uid, QJsonArray array) :
        _from_uid(from_uid), _to_uid(to_uid) {
        // 取出每一条信息，放到vector里面
        for (auto data : array) {
            auto obj = data.toObject();
            auto msg_id = obj["msg_id"].toString();
            auto msg_content = obj["msg_content"].toString();
            auto msg_ptr = std::make_shared<TextChatData> (from_uid, to_uid, msg_id, msg_content);
            _chat_msgs.push_back(msg_ptr);
        }
    }

    int _from_uid;
    int _to_uid;
    std::vector<std::shared_ptr<TextChatData>> _chat_msgs;
};

/**
 * @brief The FriendInfo class
 * 好友信息，包括好友的基本信息，以及聊天信息
 */
struct FriendInfo {
    // 这个是为新的朋友item开放的接口
    FriendInfo(int uid, QString name, QString icon) : _uid(uid), _name(name), _description(""),
        _icon(icon), _sex(0), _back_name(""), _last_msg("") {}

    FriendInfo(int uid, QString name, QString description,
               QString icon, QString back_name, int sex, QString last_msg="") :
        _uid(uid), _name(name), _description(description), _icon(icon), _back_name(back_name),
        _sex(sex), _last_msg(last_msg){}

    FriendInfo(int uid, QString name, QString description,
               QString icon, int sex, QString last_msg="") :
        _uid(uid), _name(name), _description(description), _icon(icon), _back_name(name),
        _sex(sex), _last_msg(last_msg){}

    FriendInfo(std::shared_ptr<AuthInfo> auth_info) : _uid(auth_info->_uid),
        _name(auth_info->_name), _description(auth_info->_description), _icon(auth_info->_icon),
        _sex(auth_info->_sex), _back_name(auth_info->_name), _last_msg(""){}

    FriendInfo(std::shared_ptr<SearchInfo> search_info) : _uid(search_info->_uid),
        _name(search_info->_name), _description(search_info->_description), _icon(search_info->_icon),
        _sex(search_info->_sex), _back_name(""), _last_msg("") {}

    void AddTextChatMsg(int from_uid, int to_uid, QJsonArray text_array);

    int _uid;
    QString _name;
    QString _description;
    QString _icon;
    int _sex;
    QString _back_name;
    QString _last_msg;
    std::vector<std::shared_ptr<TextChatData>> _chat_msgs;
};

struct UserInfo {
    UserInfo(int uid, QString name, QString description,
               QString icon, int sex) :
        _uid(uid), _name(name), _description(description), _icon(icon),
        _sex(sex){}
    int _uid;
    QString _name;
    QString _description;
    QString _icon;
    int _sex;
};

#endif // USERDATA_H
