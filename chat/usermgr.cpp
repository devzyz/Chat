#include "usermgr.h"
#include "global.h"

UserMgr::UserMgr()
    : _contact_load_count(0), _current_load_chat_id(0), _last_chat_id(0),
      _is_load_chat_finish(false)
{

}

void UserMgr::SetToken(QString token)
{
    _token = token;
}

QString UserMgr::GetToken() const
{
    return _token;
}

void UserMgr::SetInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info = user_info;
}

void UserMgr::resetSession()
{
    _user_info.reset();
    _token.clear();
    _contact_load_count = 0;
    _apply_map.clear();
    _friend_map.clear();
    _friend_list.clear();
    _chat_map.clear();
    _uid_to_chatId.clear();
    _current_load_chat_id = 0;
    _last_chat_id = 0;
    _is_load_chat_finish = false;
}

int UserMgr::GetUid()
{
    return _user_info ? _user_info->_uid : 0;
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
        auto fromuid = value["fromuid"].toInt();
        auto applyname = value["applyname"].toString();
        auto applydescription = value["applydescription"].toString();
        auto applyicon = value["applyicon"].toString();
        auto applysex = value["applysex"].toInt();
        auto status = value["status"].toInt();
        auto description = value["description"].toString();
        auto backname = value["backname"].toString();
        auto touid = value["touid"].toInt();
        auto apply_info = std::make_shared<ApplyInfo> (fromuid, applyname, applydescription, applyicon, applysex,
                                                      status, touid, description, backname);
        _apply_map.insert(fromuid, apply_info);
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
        auto backname = value["backname"].toString();
        auto friend_info = std::make_shared<UserInfo> (uid, name, description, icon, sex, backname);
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
    auto friend_info = std::make_shared<UserInfo> (auth_info);
    _friend_map.insert(auth_info->_auth_uid, friend_info);
    _friend_list.push_back(friend_info);
}

// 获取某个好友的信息
std::shared_ptr<UserInfo> UserMgr::GetFriendById(int uid)
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

// 取一部分联系人
std::vector<std::shared_ptr<UserInfo>> UserMgr::GetSomeContactList() {
    std::vector<std::shared_ptr<UserInfo>> friend_list;
    int l = _contact_load_count;
    int r = _contact_load_count + LOADING_STEP_LENGTH;

    // 已经加载完成
    if (l >= _friend_list.size()) {
        return friend_list;
    }

    // 未加载完成，但本次加载不够LOADING_STEP_LENGTH的长度
    if (r > _friend_list.size()) {
        friend_list = std::vector<std::shared_ptr<UserInfo>> (_friend_list.begin() + l, _friend_list.end());
        return friend_list;
    }

    // 未加载完成，且剩余足够长
    friend_list = std::vector<std::shared_ptr<UserInfo>> (_friend_list.begin() + l, _friend_list.begin() + r);

    return friend_list;
}

// 判断联系人是否加载完成
bool UserMgr::ContactIsLoadFinish() {
    return _contact_load_count >= _friend_list.size();
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

int UserMgr::GetCurrentLoadChatId()
{
    return _current_load_chat_id;
}

void UserMgr::SetCurrentChatId(int current_chat_id)
{
    _current_load_chat_id = current_chat_id;
}

void UserMgr::SetUidToChatId(int other_id, int chat_id)
{
    if (_uid_to_chatId.find(other_id) != _uid_to_chatId.end()) {
        return;
    }
    _uid_to_chatId.insert(other_id, chat_id);
}

int UserMgr::GetUidToChatId(int uid)
{
    auto iter_find = _uid_to_chatId.find(uid);
    if (iter_find == _uid_to_chatId.end()) {
        return -1;
    }
    return iter_find.value();
}

void UserMgr::SetIsLoadFinish(bool is_load_chat_finish)
{
    _is_load_chat_finish = is_load_chat_finish;
}

bool UserMgr::ChatIsLoadFinish()
{
    return _is_load_chat_finish;
}

void UserMgr::AddChatInfo(int chat_id, std::shared_ptr<ChatInfo> chat_info)
{
    if (_chat_map.find(chat_id) != _chat_map.end()) {
        return;
    }
    _chat_map.insert(chat_id, chat_info);
}

std::shared_ptr<ChatInfo> UserMgr::GetChatInfo(int chat_id)
{
    auto find_iter = _chat_map.find(chat_id);
    if (find_iter == _chat_map.end()) {
        return nullptr;
    }
    return find_iter.value();
}
