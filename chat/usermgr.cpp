#include "usermgr.h"
#include "global.h"
#include "avatarcrop.h"
#include <QStandardPaths>
#include "userstoragepaths.h"
#include "messageservice.h"
#include <QCoreApplication>
#include <QLabel>

UserMgr::UserMgr()
    : _messages(new MessageService(this)),
      _localAvatar(new LocalAvatar(UserStoragePaths::dataRoot(), this,
          QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))),
      _contact_load_count(0), _current_load_chat_id(0), _last_chat_id(0),
      _is_load_chat_finish(false)
{
    // 在 Qt 事件投递停止前排空存储，避免工作线程无法收到退出任务。
    qAddPostRoutine(
        /** @brief 应用退出时释放消息服务，兼容单例已释放的关闭顺序。 */
        [] {
        const auto user = UserMgr::instance();
        if (!user) return; // Normal application exit may already release the singleton.
        delete user->_messages;
        user->_messages = nullptr;
    });
    _localAvatar->setUploadEnabled(true);
    // 将本地头像上传请求转交当前资源会话。
    connect(_localAvatar, &LocalAvatar::uploadRequested, this,
        /** @brief 有远端头像服务时上传，否则结束本地等待并报告错误。 */
        [this](const QString &path) {
        if (_remoteAvatars) _remoteAvatars->upload(path);
        else _localAvatar->finishUpload(tr("尚未建立资源服务会话"));
    });
    // 将当前账号的本地头像变化通知界面。
    connect(_localAvatar, &LocalAvatar::imageChanged, this,
        /** @brief 本人头像变化时通知引用该 UID 的界面。 */
        [this] {
        if (_user_info) emit avatarChanged(_user_info->_uid);
    });
}

void UserMgr::setToken(QString token)
{
    _token = token;
}

QString UserMgr::token() const
{
    return _token;
}

void UserMgr::setUserInfo(std::shared_ptr<UserInfo> user_info)
{
    delete _remoteAvatars;
    _remoteAvatars = nullptr;
    _user_info = user_info;
    _localAvatar->setAccount(gate_url_prefix, user_info->_uid);
}

void UserMgr::startResourceSession()
{
    const auto user_info = _user_info;
    if (!user_info || _remoteAvatars) return;
    if (!_token.isEmpty() && !gate_url_prefix.isEmpty()) {
        QSettings settings(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
        _remoteAvatars = new AvatarCache(QUrl(settings.value("ResourceServer/Url", "http://127.0.0.1:8090").toString()),
            user_info->_uid, _token, storageRoot(), this);
        // 远端发布完成后结束本地上传状态。
        connect(_remoteAvatars, &AvatarCache::published, this,
            /** @brief 远端发布成功后完成本地头像保存流程。 */
            [this] { _localAvatar->finishUpload(); });
        connect(_remoteAvatars, &AvatarCache::uploadFailed, _localAvatar, &LocalAvatar::finishUpload);
        // 更新本人头像或通知其他用户头像变化。
        connect(_remoteAvatars, &AvatarCache::changed, this,
            /** @brief 将本人远端头像写回本地模型，其他用户仅通知刷新。 */
            [this](int uid, const QImage &image) {
            if (_user_info && uid == _user_info->_uid) _localAvatar->setRemoteImage(image);
            else emit avatarChanged(uid);
        });
        _remoteAvatars->watch(user_info->_uid);
    }
}

QString UserMgr::storageRoot() const
{
    return UserStoragePaths::accountRoot(UserStoragePaths::dataRoot(), gate_url_prefix,
        _user_info ? _user_info->_uid : 0);
}

QPixmap UserMgr::avatarFor(int uid, const QString &fallback) const
{
    if (_user_info && uid == _user_info->_uid) return selfAvatar();
    if (_remoteAvatars && uid > 0) {
        _remoteAvatars->watch(uid);
        const auto image = _remoteAvatars->image(uid);
        if (!image.isNull()) return QPixmap::fromImage(AvatarCrop::circularPreview(image));
    }
    // Only bundled resources are accepted as legacy server paths.
    QPixmap result(fallback.startsWith(":/") ? fallback : ":/res/head_1.jpg");
    return result;
}

void UserMgr::bindAvatar(QLabel *label, int uid, const QString &fallback)
{
    label->setProperty("avatarUid", uid);
    label->setProperty("avatarFallback", fallback);
    // 按标签当前绑定的用户和尺寸刷新头像。
    const auto update =
        /** @brief 按标签绑定的 UID 和默认资源刷新头像。 */
        [this, label] {
        label->setPixmap(avatarFor(label->property("avatarUid").toInt(),
            label->property("avatarFallback").toString()).scaled(label->size(),
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };
    if (!label->property("avatarBound").toBool()) {
        label->setProperty("avatarBound", true);
        // 仅刷新与变更 UID 匹配的标签。
        connect(this, &UserMgr::avatarChanged, label,
            /** @brief 只更新引用发生变化 UID 的头像标签。 */
            [label, update](int changed) {
            if (label->property("avatarUid").toInt() == changed) update();
        });
    }
    update();
}

QPixmap UserMgr::selfAvatar() const
{
    if (!_localAvatar->image().isNull()) {
        return QPixmap::fromImage(AvatarCrop::circularPreview(_localAvatar->image()));
    }
    const QPixmap serverAvatar(_user_info ? _user_info->_icon : QString());
    return serverAvatar.isNull() ? QPixmap(":/res/head_1.jpg") : serverAvatar;
}

void UserMgr::resetSession()
{
    _messages->stop();
    delete _remoteAvatars;
    _remoteAvatars = nullptr;
    _user_info.reset();
    _localAvatar->reset();
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

int UserMgr::uid()
{
    return _user_info ? _user_info->_uid : 0;
}

bool UserMgr::hasFriendApplication(int uid)
{
    auto iter_find = _apply_map.find(uid);
    return iter_find != _apply_map.end();
}

void UserMgr::addFriendApplication(int uid, std::shared_ptr<ApplyInfo> applyinfo)
{
    _apply_map[uid] = applyinfo;
}

// 添加申请列表数据
void UserMgr::addFriendApplications(QJsonArray list) {
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
void UserMgr::addFriends(QJsonArray list)
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
void UserMgr::appendFriendApplicationsTo(std::vector<std::shared_ptr<ApplyInfo>> &list)
{
    for(auto &apply : _apply_map) {
        list.push_back(apply);
    }
}

// 判断是否已经是我的好友了
bool UserMgr::isFriend(int uid)
{
    auto iter_find = _friend_map.find(uid);
    if (iter_find == _friend_map.end()) {
        return false;
    }
    return true;
}

// 添加某个好友
/** @brief 保存好友资料并更新联系人状态。 */
void UserMgr::addFriend(std::shared_ptr<AuthInfo> auth_info)
{
    auto friend_info = std::make_shared<UserInfo> (auth_info);
    _friend_map.insert(auth_info->_auth_uid, friend_info);
    _friend_list.push_back(friend_info);
}

// 获取某个好友的信息
std::shared_ptr<UserInfo> UserMgr::friendById(int uid)
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
std::vector<std::shared_ptr<UserInfo>> UserMgr::nextContactPage() {
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
bool UserMgr::isContactListFullyLoaded() {
    return _contact_load_count >= _friend_list.size();
}

// 添加成功后，更新已添加的数量
void UserMgr::advanceContactPage() {
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

std::shared_ptr<UserInfo> UserMgr::userInfo()
{
    return _user_info;
}

int UserMgr::chatListCursor()
{
    return _current_load_chat_id;
}

void UserMgr::setChatListCursor(int current_chat_id)
{
    _current_load_chat_id = current_chat_id;
}

void UserMgr::addPrivateChatMapping(int other_id, int chat_id)
{
    if (_uid_to_chatId.find(other_id) != _uid_to_chatId.end()) {
        return;
    }
    _uid_to_chatId.insert(other_id, chat_id);
}

int UserMgr::privateChatIdFor(int uid)
{
    auto iter_find = _uid_to_chatId.find(uid);
    if (iter_find == _uid_to_chatId.end()) {
        return -1;
    }
    return iter_find.value();
}

void UserMgr::setChatListFullyLoaded(bool is_load_chat_finish)
{
    _is_load_chat_finish = is_load_chat_finish;
}

bool UserMgr::isChatListFullyLoaded()
{
    return _is_load_chat_finish;
}

void UserMgr::addChatInfo(int chat_id, std::shared_ptr<ChatInfo> chat_info)
{
    _messages->registerChat(chat_id);
    if (_chat_map.find(chat_id) != _chat_map.end()) {
        return;
    }
    _chat_map.insert(chat_id, chat_info);
}

std::shared_ptr<ChatInfo> UserMgr::chatInfo(int chat_id)
{
    auto find_iter = _chat_map.find(chat_id);
    if (find_iter == _chat_map.end()) {
        return nullptr;
    }
    return find_iter.value();
}
