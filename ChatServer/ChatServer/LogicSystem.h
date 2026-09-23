#pragma once
#include "UserSessionDirectory.h"
#include "UserPresenceStore.h"
#include "LogicDispatcher.h"
#include <functional>
#include <map>
#include "Data.h"
#include <json/value.h>

class CSession;
namespace chat_transport { class CServer; }
typedef std::function<void(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data)> FunCallBack;
/** @brief 分发聊天业务请求，协调会话身份、用户查询和消息存储。 */
class LogicSystem : public LogicDispatcher
{
public:
    /** @brief 停止业务分发线程并等待已接受任务结束。 */
    ~LogicSystem();
    /** @brief 接入会话目录与在线状态存储，并注册业务处理器。 */
    LogicSystem(std::shared_ptr<UserSessionDirectory> directory, std::shared_ptr<UserPresenceStore> presence);
private:
    /** @brief 校验会话身份并分发消息；仅未知消息类型返回 false。 */
    bool Dispatch(const LogicMessage& message);
    /** @brief 注册登录、好友、消息、回执和历史查询处理器。 */
    void RegisterCallBacks();

    /** @brief 保留的登录处理声明；当前登录由注册回调处理。 */
    void LoginHandler(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data);

    /** @brief 优先读取 Redis，未命中则查询 MySQL 并回填；查不到用户返回 false。 */
    bool GetUserBaseInfo(std::string baseinfo_key, int uid, std::shared_ptr<UserInfo>& userinfo);
    /** @brief 按 UID 查询用户并写入响应，缓存未命中时回源 MySQL。 */
    void GetUserByUid(std::string uid, Json::Value& value);
    /** @brief 按用户名查询用户并写入响应，缓存未命中时回源 MySQL。 */
    void GetUserByName(std::string name, Json::Value& value);
    /** @brief 查询用户收到的前十条好友申请，失败返回 false。 */
    bool GetApplyFriendList(int uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist);
    /** @brief 查询用户好友列表，失败返回 false。 */
    bool GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friend_list);
    /** @brief 分页查询会话，输出列表、后续页标记和末尾游标；失败返回 false。 */
    bool GetUserChatList(int uid, int current_load_id, int page_size,
        std::vector<std::shared_ptr<ChatInfoBase>>& chat_list, bool& load_more, int& last_load_id);
    /** @brief 校验访问权限并分页查询历史消息，输出后续页标记及游标；失败返回 false。 */
    bool GetChatMessageList(int principal_uid, int chat_id, int current_msg_id, int page_size,
        std::vector<std::shared_ptr<ChatMessage>>& chat_list, bool& load_more, int& last_msg_id);

    /** @brief 判断所有字符是否均为数字；空字符串也返回 true。 */
    bool IsOnlyDigit(std::string& uid_name);
    std::map<short, FunCallBack> _fun_callbacks;

    std::shared_ptr<UserSessionDirectory> _directory;
    std::shared_ptr<UserPresenceStore> _presence;
};
