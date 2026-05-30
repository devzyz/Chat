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
    void SetInfo(std::shared_ptr<UserInfo> user_info);
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
    std::shared_ptr<FriendInfo> GetFriendById(int uid);
    // 取与一部分用户的聊天记录，因为要满足动态加载，不是一次性加载完所有的
    std::vector<std::shared_ptr<FriendInfo>> GetSomeChatList();
    // 取一部分联系人
    std::vector<std::shared_ptr<FriendInfo>> GetSomeContactList();
    // 判断聊天列表是否加载完成
    bool ChatIsLoadFinish();
    // 判断联系人是否加载完成
    bool ContactIsLoadFinish();
    // 在添加成功后，更新已添加的数量
    void UpdateChatLoadedCount();
    // 添加成功后，更新已添加的数量
    void UpdateContactLoadedCount();
    // 获取当前客户端的信息
    std::shared_ptr<UserInfo> GetUserInfo();
    // 添加来自from_uid的数据
    void AddTextChatMsg(int from_uid, int to_uid, QJsonArray text_array);
private:
    UserMgr();
    std::shared_ptr<UserInfo> _user_info;
    QString _token;

    int _chat_load_count; // 聊天列表已加载到哪里
    int _contact_load_count; // 联系人列表已加载到哪里

    // 申请添加我为好友的列表，key为uid
    QMap<int, std::shared_ptr<ApplyInfo>> _apply_map;
    // 保存所有的好友，key为uid
    QMap<int, std::shared_ptr<FriendInfo>> _friend_map;
    // 保存所有的好友
    std::vector<std::shared_ptr<FriendInfo>> _friend_list;
};

#endif // USERMGR_H
