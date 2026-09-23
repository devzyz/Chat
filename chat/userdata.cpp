#include "userdata.h"

ChatDataBase::ChatDataBase(int msg_id, int chat_id, ChatType chat_type,
                           ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time):
    _msg_id(msg_id), _chat_id(chat_id), _chat_type(chat_type),
    _chat_msg_type(chat_msg_type), _content(content), _send_uid(send_uid),
    _sent_at(QDateTime(QDate::currentDate(), send_time)),
    _status(ChatStatus::STATUS_NO_READ)
{

}

ChatDataBase::ChatDataBase(QString client_msg_id, int chat_id, ChatType chat_type,
                           ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time) :
    _client_msg_id(client_msg_id), _msg_id(0), _chat_id(chat_id), _chat_type(chat_type),
    _chat_msg_type(chat_msg_type), _content(content), _send_uid(send_uid),
    _sent_at(QDateTime(QDate::currentDate(), send_time)),
    _status(ChatStatus::STATUS_NO_READ)
{

}

ChatDataBase::ChatDataBase(int msg_id, int chat_id, ChatType chat_type,
                           ChatMessageType chat_msg_type, QString content, int send_uid,
                           QDateTime sent_at) :
    _msg_id(msg_id), _chat_id(chat_id), _chat_type(chat_type),
    _chat_msg_type(chat_msg_type), _content(content), _send_uid(send_uid),
    _sent_at(std::move(sent_at)), _status(ChatStatus::STATUS_NO_READ)
{
}

void ChatDataBase::setClientMessageId(const QString &clientMessageId) {
    _client_msg_id = clientMessageId;
}

int ChatDataBase::getMsgId() {
    return _msg_id;
}

int ChatDataBase::getChatId() {
    return _chat_id;
}

ChatType ChatDataBase::getChatType() {
    return _chat_type;
}

ChatMessageType ChatDataBase::getChatMsgType() {
    return _chat_msg_type;
}

QString ChatDataBase::getContent() {
    return _content;
}

int ChatDataBase::getSendId() {
    return _send_uid;
}

QTime ChatDataBase::getSendTime() {
    return _sent_at.time();
}

QDateTime ChatDataBase::getSentAt() {
    return _sent_at;
}

void ChatDataBase::setMessageId(int msg_id)
{
    _msg_id = msg_id;
}

void ChatDataBase::setStatus(ChatStatus status)
{
    _status = status;
}

ChatStatus ChatDataBase::getStatus()
{
    return _status;
}

QString ChatDataBase::getCacheMsgId()
{
    return _client_msg_id;
}


TextChatData::TextChatData(int msg_id, int chat_id, ChatType chat_type,
                           ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time) :
    ChatDataBase(msg_id, chat_id, chat_type, chat_msg_type, content, send_uid, send_time)
{

}

TextChatData::TextChatData(QString client_msg_id, int chat_id, ChatType chat_type,
                           ChatMessageType chat_msg_type, QString content, int send_uid, QTime send_time) :
    ChatDataBase(client_msg_id, chat_id, chat_type, chat_msg_type, content, send_uid, send_time)
{

}

TextChatData::TextChatData(int msg_id, int chat_id, ChatType chat_type,
                           ChatMessageType chat_msg_type, QString content, int send_uid,
                           QDateTime sent_at) :
    ChatDataBase(msg_id, chat_id, chat_type, chat_msg_type, content, send_uid, std::move(sent_at))
{
}

UserInfo::UserInfo(int uid, QString name, QString description, QString icon, int sex) :
        _uid(uid), _name(name), _description(description), _icon(icon),
        _sex(sex){}

UserInfo::UserInfo(int uid, QString name, QString description, QString icon, int sex, QString backname) :
    _uid(uid), _name(name), _description(description), _icon(icon),
    _sex(sex), _backname(backname) {}

/** @brief 从认证好友资料构造用户展示信息。 */
UserInfo::UserInfo(std::shared_ptr<AuthInfo> auth_info) :
    _uid(auth_info->_auth_uid), _name(auth_info->_auth_name), _description(auth_info->_auth_description)
    , _icon(auth_info->_auth_icon), _sex(auth_info->_auth_sex){}

UserInfo::UserInfo(int uid, QString name, QString icon)
    : _uid(uid), _name(name), _description(""),
    _icon(icon), _sex(0) {}


ChatInfo::ChatInfo(int uid, int chat_id, int last_msg_id) :
    _uid(uid), _last_msg_id(last_msg_id), _chat_id(chat_id),
    _chat_type(ChatType::PRIVATE), _is_can_load_more(true) {}

ChatInfo::ChatInfo(int uid, QString name, QString icon, QString back_name, int chat_id, ChatType chat_type):
    _uid(uid), _name(name), _icon(icon), _back_name(back_name), _last_msg_id(0), _chat_id(chat_id),
    _chat_type(chat_type), _is_can_load_more(true) {}

// 添加一条聊天数据
/** @brief 按服务端消息 ID 保存会话消息。 */
void ChatInfo::addChatData(std::shared_ptr<ChatDataBase> chat_data) {
    _chat_msgs.insert(chat_data->getMsgId(), chat_data);
    _last_msg_id = chat_data->getMsgId();
}

int ChatInfo::getUid() {
    return _uid;
}

int ChatInfo::getLastMsgId() {
    return _last_msg_id;
}

int ChatInfo::getChatId() {
    return _chat_id;
}

ChatType ChatInfo::getChatType() {
    return _chat_type;
}

void ChatInfo::setCanLoadMore(bool flag) {
    _is_can_load_more = flag;
}

void ChatInfo::setLastMsgId(int current_msg_id) {
    _last_msg_id = current_msg_id;
}

std::shared_ptr<ChatDataBase> ChatInfo::getChatDataByMsgId(int msg_id)
{
    auto iter_find = _chat_msgs.find(msg_id);
    if (iter_find == _chat_msgs.end()) {
        return nullptr;
    }
    return iter_find.value();
}

QMap<int, std::shared_ptr<ChatDataBase> > &ChatInfo::getChatMsgs()
{
    return _chat_msgs;
}

// 添加一条聊天缓存数据
/** @brief 按客户端 UUID 保存待确认会话消息。 */
void ChatInfo::addCacheChatData(QString uuid, std::shared_ptr<ChatDataBase> chat_data) {
    _cache_msgs.insert(uuid, chat_data);
}

// 获取缓存聊天记录数据
QMap<QString, std::shared_ptr<ChatDataBase>>& ChatInfo::getCacheChatMsgs() {
    return _cache_msgs;
}

// 获取缓存聊天数据
std::shared_ptr<ChatDataBase> ChatInfo::getCacheChatMessage(QString uuid) {
    auto find_iter = _cache_msgs.find(uuid);
    if (find_iter == _cache_msgs.end()) {
        return nullptr;
    }
    return find_iter.value();
}

// 删除缓存聊天数据
void ChatInfo::eraseCacheChatMessage(QString uuid) {
    auto find_iter = _cache_msgs.find(uuid);
    if (find_iter != _cache_msgs.end()) {
        _cache_msgs.erase(find_iter);
    }
}

bool ChatInfo::canLoadMore()
{
    return _is_can_load_more;
}

bool ChatInfo::isEmpty()
{
    return _chat_msgs.empty();
}
