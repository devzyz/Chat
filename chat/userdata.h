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
    /** @brief 创建保存用户搜索结果的身份及展示资料。 */
    SearchInfo(int uid, QString name, QString description, QString icon, int sex) :
        _uid(uid), _name(name), _description(description), _sex(sex), _icon(icon) {}
    int _uid;
    QString _name;
    QString _description;
    QString _icon;
    int _sex;
};

/** @brief 保存新朋友申请列表展示所需的申请人资料和申请内容。 */
struct ApplyInfo {
    /** @brief 使用好友申请回包构造列表项，backname 为申请人给接收方的备注名。 */
    ApplyInfo(int apply_uid, QString apply_name, QString apply_description,
              QString apply_icon, int apply_sex, int status, int to_uid, QString description, QString backname)
        :_apply_uid(apply_uid),_apply_name(apply_name),_apply_description(apply_description),
        _apply_icon(apply_icon),_apply_sex(apply_sex),_status(status), _to_uid(to_uid),
        _description(description), _backname(backname) {}

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

/** @brief 保存好友审批后展示的对方资料和本账号设置的备注名。 */
struct AuthInfo {
    /** @brief 使用好友确认回包构造联系人资料。 */
    AuthInfo(int auth_uid, QString auth_name, QString auth_description,
              QString auth_icon, int auth_sex, QString backname)
        :_auth_uid(auth_uid),_auth_name(auth_name),_auth_description(auth_description),
        _auth_icon(auth_icon),_auth_sex(auth_sex), _backname(backname) {}

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
    /** @brief 以服务器消息 ID 和日内时间构造消息，日期按当前日期补齐。 */
    ChatDataBase(int msg_id, int chat_id, ChatType chat_type,
                 ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time);

    /** @brief 以客户端 UUID 构造待确认消息，日内时间按当前日期补齐。 */
    ChatDataBase(QString client_msg_id, int chat_id, ChatType chat_type,
                 ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time);

    /** @brief 以服务器消息 ID 和完整日期时间构造已确认消息。 */
    ChatDataBase(int msg_id, int chat_id, ChatType chat_type,
                 ChatMessageType chat_msg_type, QString content, int send_uid, QDateTime sent_at);

    /** @brief 返回服务器分配的消息 ID，本地待发消息可能尚无有效 ID。 */
    int GetMsgId();
    /** @brief 返回消息或资料所属会话 ID。 */
    int GetChatId();
    /** @brief 返回消息所属会话类型；保留旧接口拼写供后续兼容改名。 */
    ChatType GetChatTpe();
    /** @brief 返回消息内容类型。 */
    ChatMessageType GetChatMsgType();
    /** @brief 返回消息正文副本。 */
    QString GetContent();
    /** @brief 返回发送者 UID。 */
    int GetSendId();
    /** @brief 返回消息时间的日内部分。 */
    QTime GetSendTime();
    /** @brief 返回包含日期的完整发送时间。 */
    QDateTime GetSentAt();
    /** @brief 保存服务器确认的消息 ID。 */
    void SetMessageId(int msg_id);
    /** @brief 更新旧界面消息状态值。 */
    void SetStatus(ChatStatus status);
    /** @brief 返回旧界面消息状态值。 */
    ChatStatus GetStatus();
    /** @brief 返回客户端生成的消息 UUID，用于 ACK 和重试关联。 */
    QString GetCacheMsgId();
    /** @brief 设置客户端消息 UUID，保持与本地发送身份一致。 */
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
/** @brief 保存文本消息的旧界面数据，支持本地 UUID 与服务器消息 ID 两种身份。 */
class TextChatData : public ChatDataBase{
public:
    /** @brief 以服务器消息 ID 和日内时间构造消息，日期按当前日期补齐。 */
    TextChatData (int msg_id, int chat_id, ChatType chat_type,
                 ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time);

    /** @brief 以客户端 UUID 构造待确认消息，日内时间按当前日期补齐。 */
    TextChatData (QString client_msg_id, int chat_id, ChatType chat_type,
                 ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time);

    /** @brief 以服务器消息 ID 和完整日期时间构造已确认消息。 */
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
    /** @brief 以对方 UID、会话 ID 和末尾消息 ID 构造会话索引。 */
    ChatInfo(int uid, int chat_id, int last_msg_id);
    /** @brief 以完整展示资料及会话类型构造会话信息。 */
    ChatInfo(int uid, QString name, QString icon, QString back_name, int chat_id,
             ChatType chat_type);

    // 添加一条聊天数据
    /** @brief 添加一条聊天数据。 */
    void AddChatData(std::shared_ptr<ChatDataBase>);
    // 添加一条聊天缓存数据
    /** @brief 添加一条聊天缓存数据。 */
    void AddCacheChatData(QString uuid, std::shared_ptr<ChatDataBase>);

    // 获取当前聊天对方的uid
    /** @brief 获取当前聊天对方的uid。 */
    int GetUid();
    /** @brief 返回当前记录的最后服务器消息 ID。 */
    int GetLastMsgId();
    /** @brief 返回消息或资料所属会话 ID。 */
    int GetChatId();
    /** @brief 更新此会话是否还允许请求更多历史。 */
    void SetIsCanLoadMore(bool flag);
    /** @brief 记录最近历史页或消息中的末尾服务器 ID。 */
    void SetLastMsgId(int current_msg_id);
    /** @brief 返回私聊或群聊会话类型。 */
    ChatType GetChatType();
    // 根据msg_id从_chat_msgs中获取编号为msg_id所发送的详细信息
    /** @brief 根据msg_id从_chat_msgs中获取编号为msg_id所发送的详细信息。 */
    std::shared_ptr<ChatDataBase> GetChatDataByMsgId(int msg_id);
    // 获取聊天记录数据
    /** @brief 获取聊天记录数据。 */
    QMap<int, std::shared_ptr<ChatDataBase>>& GetChatMsgs();
    // 获取缓存聊天记录数据
    /** @brief 获取缓存聊天记录数据。 */
    QMap<QString, std::shared_ptr<ChatDataBase>>& GetCacheChatMsgs();
    // 添加缓存聊天数据
    /** @brief 添加缓存聊天数据。 */
    void AddCacheChatMessage(QString uuid, std::shared_ptr<ChatDataBase> _cache_text_msg);
    // 获取聊天数据
    /** @brief 获取聊天数据。 */
    std::shared_ptr<ChatDataBase> GetCacheChatMessage(QString uuid);
    // 删除聊天数据
    /** @brief 删除聊天数据。 */
    void EraseCacheChatMessage(QString uuid);
    // 获取是否能够加载更多
    /** @brief 获取是否能够加载更多。 */
    bool GetIsCanLoadMore();
    // 聊天记录是否是空的
    /** @brief 聊天记录是否是空的。 */
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
    /** @brief 从完整账号资料构造用户信息，不设置好友备注。 */
    UserInfo(int uid, QString name, QString description,
             QString icon, int sex);

    /** @brief 从完整用户资料及本账号备注名构造好友信息。 */
    UserInfo(int uid, QString name, QString description,
             QString icon, int sex, QString backname);

    /** @brief 从好友审批资料复制用户信息。 */
    UserInfo(std::shared_ptr<AuthInfo>);

    // 这个是为"新的朋友item"开放的接口
    /** @brief 从 UID、名称和头像构造列表展示用简要资料。 */
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
