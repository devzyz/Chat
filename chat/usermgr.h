#ifndef USERMGR_H
#define USERMGR_H

#include <QObject>
#include <memory>
#include "singleton.h"
#include "userdata.h"
#include <QJsonArray>

class UserMgr : public QObject, public Singleton<UserMgr>,
                public std::enable_shared_from_this<UserMgr>
{
    Q_OBJECT
    friend class Singleton<UserMgr>;
public:
    ~UserMgr();
    void SetToken(QString token);
    QString GetToken() const;
    void SetInfo(std::shared_ptr<UserInfo> user_info);
    void resetSession();
    int GetUid();

    // 判断是否已经申请过添加我为好友了
    bool AlreadyApplyAddFriend(int uid);
    // 添加好友申请
    void AddApply(int uid, std::shared_ptr<ApplyInfo> applyinfo);
    // 添加好友申请列表
    void AddApplyList(QJsonArray list);
    // 添加好友列表
    void AddFriendList(QJsonArray list);
    // 获取到添加好友列表
    void GetApplyList(std::vector<std::shared_ptr<ApplyInfo>>& list);
    // 判断是否已经是我的好友了
    bool CheckIsFriendById(int uid);
    // 添加某个好友的信息
    void AddFriend(std::shared_ptr<AuthInfo>);
    // 获取某个好友的信息
    std::shared_ptr<UserInfo> GetFriendById(int uid);
    // 取一部分联系人
    std::vector<std::shared_ptr<UserInfo>> GetSomeContactList();
    // 判断联系人是否加载完成
    bool ContactIsLoadFinish();
    // 添加成功后，更新已添加的数量
    void UpdateContactLoadedCount();
    // 获取当前客户端的信息
    std::shared_ptr<UserInfo> GetUserInfo();
    // 获取接下来将要加载的chat_id信息
    int GetCurrentLoadChatId();
    // 设置当前已经获取的chat_id
    void SetCurrentChatId(int current_chat_id);
    // 将uid与chat_id的映射添加上
    void SetUidToChatId(int other_id, int chat_id);
    // 获取uid与chat_id的映射
    int GetUidToChatId(int uid);
    // 设置会话列表是否还能够再加载
    void SetIsLoadFinish(bool is_load_chat_finish);
    // 判断会话是否还能再进行加载
    bool ChatIsLoadFinish();
    // 插入一条会话
    void AddChatInfo(int chat_id, std::shared_ptr<ChatInfo> chat_info);
    // 根据chat_id获取到会话信息
    std::shared_ptr<ChatInfo> GetChatInfo(int chat_id);
private:
    UserMgr();
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
