#ifndef USERDATA_H
#define USERDATA_H

#include <QString>
#include <vector>
#include <QJsonArray>
#include <memory>
#include <QJsonObject>
#include "global.h"
#include <QDateTime>
#include <QTime>

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
    ApplyInfo(int apply_uid, QString apply_name, QString apply_description,
              QString apply_icon, int apply_sex, int status, int to_uid, QString description, QString backanme)
        :_apply_uid(apply_uid),_apply_name(apply_name),_apply_description(apply_description),
        _apply_icon(apply_icon),_apply_sex(apply_sex),_status(status), _to_uid(to_uid),
        _description(description), _backname(backanme) {}

    int _apply_uid; // 申请人id
    QString _apply_name; // 申请人用户名
    QString _apply_description; // 申请人个人描述
    QString _apply_icon; // 申请人头像
    int _apply_sex; // 申请人性别
    int _status; // 状态，是否添加
    int _to_uid; // 被申请人id
    QString _description; // 申请人发送的描述信息
    QString _backname; // 申请人给被申请人的备注名
};

/**
 * @brief The AuthInfo class
 * 认证好友的信息
 */
struct AuthInfo {
    AuthInfo(int auth_uid, QString auth_name, QString auth_description,
              QString auth_icon, int auth_sex, QString backanme)
        :_auth_uid(auth_uid),_auth_name(auth_name),_auth_description(auth_description),
        _auth_icon(auth_icon),_auth_sex(auth_sex), _backname(backanme) {}

    int _auth_uid; // 对方的uid
    QString _auth_name; // 对方的用户名
    QString _auth_description; // 对方的个人描述
    QString _auth_icon; // 对方的头像
    int _auth_sex; // 对方的性别
    QString _backname; // 我给对方的备注名
};

// 聊天消息的基类
/**
 * @brief The ChatDataBase class
 * 发送消息流程
 * 本地生成_client_msg_id -> 服务器 -> 插入MySQL -> _msg_id -> 返回客户端 -> 根据_client_msg_id设置_msg_id
 *
 * 同时服务器 -> 通知另一个客户端接收消息
 */
class ChatDataBase {
public:
    ChatDataBase(int msg_id, int chat_id, ChatType chat_type,
                 ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time);

    ChatDataBase(QString client_msg_id, int chat_id, ChatType chat_type,
                 ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time);

    ChatDataBase(int msg_id, int chat_id, ChatType chat_type,
                 ChatMessageType chat_msg_type, QString content, int send_uid, QDateTime sent_at);

    int GetMsgId();
    int GetChatId();
    ChatType GetChatTpe();
    ChatMessageType GetChatMsgType();
    QString GetContent();
    int GetSendId();
    QTime GetSendTime();
    QDateTime GetSentAt();
    void SetMessageId(int msg_id);
    void SetStatus(ChatStatus status);
    ChatStatus GetStatus();
    QString GetCacheMsgId();
    void SetClientMessageId(const QString &clientMessageId);
private:
    // 客户端本地保存的id
    QString _client_msg_id;
    // 聊天消息的唯一id
    int _msg_id;
    // 会话的唯一id
    int _chat_id;
    // 会话的类型
    ChatType _chat_type;
    // 消息的类型
    ChatMessageType _chat_msg_type;
    // 消息的内容
    QString _content;
    // 发送者uid
    int _send_uid;
    // 消息的发送时间
    QDateTime _sent_at;
    // 消息的状态，-1无需设置，0未读，1发送失败，2已读
    ChatStatus _status;
};

// 一条文本消息
class TextChatData : public ChatDataBase{
public:
    TextChatData (int msg_id, int chat_id, ChatType chat_type,
                 ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time);

    TextChatData (QString client_msg_id, int chat_id, ChatType chat_type,
                 ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time);

    TextChatData (int msg_id, int chat_id, ChatType chat_type,
                  ChatMessageType chat_msg_type, QString content, int send_uid, QDateTime sent_at);
};

/**
 * @brief The FriendInfo class
 * 好友信息，包括好友的基本信息，以及聊天信息
 *
 * search -> chat
 * auth -> chat
 * friendinfo -> chat
 * 未聊天过的从服务器发消息 -> chat
 *
 * 如果userMgr中有，则直接显示
 * 如果没有，则创建
 */
class ChatInfo{
public:
    ChatInfo(int uid, int chat_id, int last_msg_id);
    ChatInfo(int uid, QString name, QString icon, QString back_name, int chat_id,
             ChatType chat_type);

    // 添加一条聊天数据
    void AddChatData(std::shared_ptr<ChatDataBase>);
    // 添加一条聊天缓存数据
    void AddCacheChatData(QString uuid, std::shared_ptr<ChatDataBase>);

    // 获取当前聊天对方的uid
    int GetUid();
    int GetLastMsgId();
    int GetChatId();
    void SetIsCanLoadMore(bool flag);
    void SetLastMsgId(int current_msg_id);
    ChatType GetChatType();
    // 根据msg_id从_chat_msgs中获取编号为msg_id所发送的详细信息
    std::shared_ptr<ChatDataBase> GetChatDataByMsgId(int msg_id);
    // 获取聊天记录数据
    QMap<int, std::shared_ptr<ChatDataBase>>& GetChatMsgs();
    // 获取缓存聊天记录数据
    QMap<QString, std::shared_ptr<ChatDataBase>>& GetCacheChatMsgs();
    // 添加缓存聊天数据
    void AddCacheChatMessage(QString uuid, std::shared_ptr<ChatDataBase> _cache_text_msg);
    // 获取聊天数据
    std::shared_ptr<ChatDataBase> GetCacheChatMessage(QString uuid);
    // 删除聊天数据
    void EraseCacheChatMessage(QString uuid);
    // 获取是否能够加载更多
    bool GetIsCanLoadMore();
    // 聊天记录是否是空的
    bool IsEmpty();
private:
    // private: 对方的uid, group: 0
    int _uid;
    // private: 对方的name, group: 群聊的name
    QString _name;
    // private: 对方的头像, group: 群聊的头像
    QString _icon;
    // private: 给对方的备注名, group: 群聊的备注名
    QString _back_name;
    // 上一次发送的消息的_msg_id
    int _last_msg_id;
    // 当前会话的唯一id
    int _chat_id;
    // 当前会话的类型，private, groups
    ChatType _chat_type;
    // 消息列表map, key = _msg_id, value = ChatDataBase
    QMap<int, std::shared_ptr<ChatDataBase>> _chat_msgs;
    // 当客户端本地发送消息时，还没有入库，没有messageid，因此本地先用uuid作为主键缓存一下等服务器回包之后，再插入聊天记录
    QMap<QString, std::shared_ptr<ChatDataBase>> _cache_msgs;
    // 群聊的所有成员, 保存的uid
    std::vector<int> _group_members;
    // 是否能够加载更多
    bool _is_can_load_more;
};

/**
 * @brief The UserInfo class
 * 有两种作用
 * 第一，表示当前客户端登录的用户
 * 第二，表示当前客户端的好友，在这种使用方式下，_backname有意义
 */
struct UserInfo {
    UserInfo(int uid, QString name, QString description,
             QString icon, int sex);

    UserInfo(int uid, QString name, QString description,
             QString icon, int sex, QString backname);

    UserInfo(std::shared_ptr<AuthInfo>);

    // 这个是为"新的朋友item"开放的接口
    UserInfo(int uid, QString name, QString icon);

    int _uid;
    QString _name;
    QString _description;
    QString _icon;
    int _sex;
    // 给好友的备注名
    QString _backname;
};

#endif // USERDATA_H
