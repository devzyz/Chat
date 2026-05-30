#include "usermgr.h"
#include "global.h"

UserMgr::UserMgr() : _chat_load_count(0), _contact_load_count(0)
{

}

void UserMgr::SetToken(QString token)
{
    _token = token;
}

void UserMgr::SetInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info = user_info;
}

int UserMgr::GetUid()
{
    return _user_info->_uid;
}

bool UserMgr::AlreadyApplyAddFriend(int uid)
{
    auto iter_find = _apply_map.find(uid);
    return iter_find != _apply_map.end();
}

void UserMgr::AddApply(int uid, std::shared_ptr<ApplyInfo> applyinfo)
{
    _apply_map[uid] = applyinfo;
}

// 添加申请列表数据
void UserMgr::AddApplyList(QJsonArray list) {
    // 遍历数据，添加数据
    for (const QJsonValue& value : list) {
        auto uid = value["uid"].toInt();
        auto name = value["name"].toString();
        auto description = value["description"].toString();
        auto icon = value["icon"].toString();
        auto sex = value["sex"].toInt();
        auto status = value["status"].toInt();
        auto apply_info = std::make_shared<ApplyInfo> (uid, name, description, icon, sex, status);
        _apply_map.insert(uid, apply_info);
    }
}

// 添加好友列表数据
void UserMgr::AddFriendList(QJsonArray list)
{
    // 遍历数据，添加数据
    for (const QJsonValue& value : list) {
        auto uid = value["uid"].toInt();
        auto name = value["name"].toString();
        auto description = value["description"].toString();
        auto icon = value["icon"].toString();
        auto sex = value["sex"].toInt();
        auto friend_info = std::make_shared<FriendInfo> (uid, name, description, icon, sex);
        _friend_map.insert(uid, friend_info);
        _friend_list.push_back(friend_info);
    }
}

// 获取申请列表
void UserMgr::GetApplyList(std::vector<std::shared_ptr<ApplyInfo>> &list)
{
    for(auto &apply : _apply_map) {
        list.push_back(apply);
    }
}

// 判断是否已经是我的好友了
bool UserMgr::CheckIsFriendById(int uid)
{
    auto iter_find = _friend_map.find(uid);
    if (iter_find == _friend_map.end()) {
        return false;
    }
    return true;
}

// 添加某个好友
void UserMgr::AddFriend(std::shared_ptr<AuthInfo> auth_info)
{
    auto friend_info = std::make_shared<FriendInfo> (auth_info);;
    _friend_map.insert(auth_info->_uid, friend_info);
    _friend_list.push_back(friend_info);
}

// 获取某个好友的信息
std::shared_ptr<FriendInfo> UserMgr::GetFriendById(int uid)
{
    auto iter_find = _friend_map.find(uid);
    if (iter_find == _friend_map.end()) {
        return nullptr;
    }
    return *iter_find;
}

UserMgr::~UserMgr()
{

}

// 取与一部分用户的聊天记录，因为要满足动态加载，不是一次性加载完所有的
std::vector<std::shared_ptr<FriendInfo>> UserMgr::GetSomeChatList() {
    std::vector<std::shared_ptr<FriendInfo>> friend_list;
    int l = _chat_load_count;
    int r = _chat_load_count + LOADING_STEP_LENGTH;

    // 已经加载完成
    if (l >= _friend_list.size()) {
        return friend_list;
    }

    // 未加载完成，但本次加载不够LOADING_STEP_LENGTH的长度
    if (r > _friend_list.size()) {
        _friend_list = std::vector<std::shared_ptr<FriendInfo>> (_friend_list.begin() + l, _friend_list.end());
        return _friend_list;
    }

    // 未加载完成，且剩余足够长
    friend_list = std::vector<std::shared_ptr<FriendInfo>> (_friend_list.begin() + l, _friend_list.begin() + r);

    return friend_list;
}

// 取一部分联系人
std::vector<std::shared_ptr<FriendInfo>> UserMgr::GetSomeContactList() {
    std::vector<std::shared_ptr<FriendInfo>> friend_list;
    int l = _contact_load_count;
    int r = _contact_load_count + LOADING_STEP_LENGTH;

    // 已经加载完成
    if (l >= _friend_list.size()) {
        return friend_list;
    }

    // 未加载完成，但本次加载不够LOADING_STEP_LENGTH的长度
    if (r > _friend_list.size()) {
        _friend_list = std::vector<std::shared_ptr<FriendInfo>> (_friend_list.begin() + l, _friend_list.end());
        return _friend_list;
    }

    // 未加载完成，且剩余足够长
    friend_list = std::vector<std::shared_ptr<FriendInfo>> (_friend_list.begin() + l, _friend_list.begin() + r);

    return friend_list;
}

// 判断聊天列表是否加载完成
bool UserMgr::ChatIsLoadFinish() {
    return _chat_load_count >= _friend_list.size();
}

// 判断联系人是否加载完成
bool UserMgr::ContactIsLoadFinish() {
    return _contact_load_count >= _friend_list.size();
}

// 在添加成功后，更新已添加的数量
void UserMgr::UpdateChatLoadedCount() {
    int l = _chat_load_count;
    int r = _chat_load_count + LOADING_STEP_LENGTH;

    // 已经加载完成
    if (l >= _friend_list.size()) {
        return;
    }

    // 未加载完成，但本次加载不够LOADING_STEP_LENGTH的长度
    if (r > _friend_list.size()) {
        _chat_load_count = _friend_list.size();
        return;
    }

    // 未加载完成，且剩余足够长
    _chat_load_count = r;
}

// 添加成功后，更新已添加的数量
void UserMgr::UpdateContactLoadedCount() {
    int l = _contact_load_count;
    int r = _contact_load_count + LOADING_STEP_LENGTH;

    // 已经加载完成
    if (l >= _friend_list.size()) {
        return;
    }

    // 未加载完成，但本次加载不够LOADING_STEP_LENGTH的长度
    if (r > _friend_list.size()) {
        _contact_load_count = _friend_list.size();
        return;
    }

    // 未加载完成，且剩余足够长
    _contact_load_count = r;
}

std::shared_ptr<UserInfo> UserMgr::GetUserInfo()
{
    return _user_info;
}

// 添加来自from_uid的数据
void UserMgr::AddTextChatMsg(int from_uid, int to_uid, QJsonArray text_array)
{
    auto iter_find = _friend_map.find(from_uid);
    if (iter_find == _friend_map.end()) {
        return ;
    }

    iter_find.value()->AddTextChatMsg(from_uid, to_uid, text_array);
}
