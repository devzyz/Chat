#ifndef USERMGR_H
#define USERMGR_H

#include <QObject>
#include <memory>
#include "singleton.h"
#include "userdata.h"
#include <QJsonArray>
#include <QPixmap>
#include "localavatar.h"
#include "avatarcache.h"
class QLabel;
class MessageService;

/** @brief 管理当前账号的用户、好友与会话缓存，并持有账号级消息和头像服务。 */
class UserMgr : public QObject, public Singleton<UserMgr>,
                public std::enable_shared_from_this<UserMgr>
{
    Q_OBJECT
    friend class Singleton<UserMgr>;
public:
    /** @brief 释放账号管理对象及其 QObject 子对象。 */
    ~UserMgr();
    /** @brief 保存当前账号的会话令牌。 */
    void setToken(QString token);
    /** @brief 返回当前账号的会话令牌。 */
    QString token() const;
    /** @brief 设置当前用户资料并切换头像账号，清除原远端头像会话。 */
    void setUserInfo(std::shared_ptr<UserInfo> user_info);
    /** @brief 为当前账号建立资源服务会话，已有会话时不重复创建。 */
    void startResourceSession();
    /** @brief 停止消息服务并清空账号缓存，保留应用级配置。 */
    void resetSession();
    /** @brief 返回本对象持有的本地头像服务，调用方不负责释放。 */
    LocalAvatar *localAvatar() const { return _localAvatar; }
    /** @brief 返回当前用户头像，缺失时使用默认头像。 */
    QPixmap selfAvatar() const;
    /** @brief 查询并关注指定用户头像，尚未取得时返回内置回退图。 */
    QPixmap avatarFor(int uid, const QString &fallback = {}) const;
    /** @brief 绑定标签的头像更新，标签销毁时自动断开连接。 */
    void bindAvatar(QLabel *label, int uid, const QString &fallback = {});
    /** @brief 返回按 Gate 地址和当前账号隔离的本地数据目录。 */
    QString storageRoot() const;
    /** @brief 返回本对象持有的消息服务，调用方不负责释放。 */
    MessageService *messages() const { return _messages; }
signals:
    /** @brief 通知指定用户的头像已变化。 */
    void avatarChanged(int uid);
public:
    /** @brief 返回当前账号 UID；会话清空后返回 0。 */
    int uid();

    /** @brief 判断是否已缓存指定用户发来的好友申请。 */
    bool hasFriendApplication(int uid);
    /** @brief 按申请人 UID 保存或替换好友申请。 */
    void addFriendApplication(int uid, std::shared_ptr<ApplyInfo> applyinfo);
    /** @brief 将回包中的好友申请写入账号缓存。 */
    void addFriendApplications(QJsonArray list);
    /** @brief 将好友列表回包写入账号缓存。 */
    void addFriends(QJsonArray list);
    /** @brief 将已缓存的好友申请追加到输出列表。 */
    void appendFriendApplicationsTo(std::vector<std::shared_ptr<ApplyInfo>>& list);
    /** @brief 判断指定 UID 是否已在好友缓存中。 */
    bool isFriend(int uid);
    /** @brief 将通过审批的好友资料加入账号缓存。 */
    void addFriend(std::shared_ptr<AuthInfo>);
    /** @brief 查询缓存中的好友资料，未找到时返回 nullptr。 */
    std::shared_ptr<UserInfo> friendById(int uid);
    /** @brief 返回当前游标对应的一页联系人，不推进游标。 */
    std::vector<std::shared_ptr<UserInfo>> nextContactPage();
    /** @brief 判断联系人分页游标是否已到列表末尾。 */
    bool isContactListFullyLoaded();
    /** @brief 按一页大小推进联系人游标，不越过列表末尾。 */
    void advanceContactPage();
    /** @brief 返回当前用户资料，会话清空后返回 nullptr。 */
    std::shared_ptr<UserInfo> userInfo();
    /** @brief 返回下次加载会话列表使用的游标。 */
    int chatListCursor();
    /** @brief 更新会话列表的分页游标。 */
    void setChatListCursor(int current_chat_id);
    /** @brief 登记对方 UID 到私聊 ID 的映射，已有映射保持不变。 */
    void addPrivateChatMapping(int other_id, int chat_id);
    /** @brief 查询与指定用户的私聊 ID，未登记时返回 -1。 */
    int privateChatIdFor(int uid);
    /** @brief 记录会话列表是否已全部加载。 */
    void setChatListFullyLoaded(bool is_load_chat_finish);
    /** @brief 判断会话列表是否已全部加载。 */
    bool isChatListFullyLoaded();
    /** @brief 向消息服务登记会话，并缓存尚未存在的会话资料。 */
    void addChatInfo(int chat_id, std::shared_ptr<ChatInfo> chat_info);
    /** @brief 按会话 ID 查询缓存资料，未找到时返回 nullptr。 */
    std::shared_ptr<ChatInfo> chatInfo(int chat_id);
private:
    /** @brief 创建消息和头像服务，并在 Qt 退出前排空消息存储。 */
    UserMgr();
    MessageService *_messages;
    LocalAvatar *_localAvatar;
    AvatarCache *_remoteAvatars = nullptr;
    std::shared_ptr<UserInfo> _user_info;
    QString _token;

    int _contact_load_count; // 联系人列表已加载到哪里

    // 申请添加我为好友的列表，key为uid
    QMap<int, std::shared_ptr<ApplyInfo>> _apply_map;
    // 保存所有的好友，key为uid
    QMap<int, std::shared_ptr<UserInfo>> _friend_map;
    // 保存所有的好友
    std::vector<std::shared_ptr<UserInfo>> _friend_list;
    // 保存所有的聊天会话, key = chat_id
    QMap<int, std::shared_ptr<ChatInfo>> _chat_map;
    // 用于的uid与chat_id的映射关系
    QMap<int, int> _uid_to_chatId;
    // 记录接下来需要加载从_current_load_chat_id开始的会话列表
    int _current_load_chat_id;
    // 记录上一次会话的_chat_id
    int _last_chat_id;
    // 记录会话是否已经加载完成
    bool _is_load_chat_finish;
};

#endif // USERMGR_H
