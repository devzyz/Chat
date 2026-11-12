#pragma once
#include "UserSessionDirectory.h"
#include "UserPresenceStore.h"
#include "LogicDispatcher.h"
#include <functional>
#include <map>
#include "Data.h"
#include <json/value.h>

class CSession;
namespace chat_transport {
class CServer;
}
typedef std::function<void(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data)> MessageHandler;
/** @brief 分发聊天业务请求，协调会话身份、用户查询和消息存储。 */
class LogicSystem : public LogicDispatcher {
public:
    /** @brief 停止业务分发线程并等待已接受任务结束。 */
    ~LogicSystem();
    /** @brief 接入会话目录与在线状态存储，并注册业务处理器。 */
    LogicSystem(
        std::shared_ptr<UserSessionDirectory> directory,
        std::shared_ptr<UserPresenceStore> presence,
        std::shared_ptr<DependencyExecutor> dependencies);

private:
    /** @brief 校验会话身份并分发消息；仅未知消息类型返回 false。 */
    bool Dispatch(const LogicMessage& message);
    // 在工作线程复核排队请求的绑定凭据后执行数据库操作；满载或异常时关闭发起连接。
    void QueueDatabase(const std::shared_ptr<CSession>& session, ExternalWork work);
    // 受理与连接有序关联的外部步骤，过载或异常时关闭原连接。
    void QueueRequest(
        ExternalTask kind,
        const std::shared_ptr<CSession>& session,
        ExternalWork work,
        std::function<bool()> sql_check = {});
    // Redis 复核完成后重新确认连接归属，再调用原业务处理器。
    void DispatchAuthenticated(const LogicMessage& message, int uid);
    // 登录初验通过后按客户端能力加载资料，并在绑定成功后回复。
    void QueueLoginData(std::shared_ptr<CSession> session, int uid, std::string credential, Json::Value capabilities);
    // 返回事务提交的后续操作；先回包，再独立投递通知，队满不改变已提交结果。
    Completion ReplyAndNotify(
        std::shared_ptr<CSession> session,
        short id,
        const Json::Value& response,
        ExternalWork route);
    // 构造唯一一份处理器表，发布后所有分片只读访问。
    std::map<short, MessageHandler> CreateHandlers();

    /** @brief 按 UID 从 MySQL 查询并写入公开资料，避免缓存遮蔽资料更新。 */
    void GetUserByUid(std::string uid, Json::Value& value);
    /** @brief 按用户名从 MySQL 查询并写入公开资料，不暴露账号凭据。 */
    void GetUserByName(std::string name, Json::Value& value);
    /** @brief 查询用户收到的前十条好友申请，失败返回 false。 */
    bool GetApplyFriendList(int uid, std::vector<std::shared_ptr<ApplyInfo>>& applylist);
    /** @brief 查询用户好友列表，失败返回 false。 */
    bool GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friend_list);
    /** @brief 分页查询会话，输出列表、后续页标记和末尾游标；失败返回 false。 */
    bool GetUserChatList(
        int uid,
        int current_load_id,
        int page_size,
        std::vector<std::shared_ptr<ChatInfoBase>>& chat_list,
        bool& load_more,
        int& last_load_id);
    /** @brief 校验访问权限并分页查询历史消息，输出后续页标记及游标；失败返回 false。 */
    bool GetChatMessageList(
        int principal_uid,
        int chat_id,
        int current_msg_id,
        int page_size,
        std::vector<std::shared_ptr<ChatMessage>>& chat_list,
        bool& load_more,
        int& last_msg_id);

    /** @brief 判断所有字符是否均为数字；空字符串也返回 true。 */
    bool IsOnlyDigit(std::string& uid_name);
    const std::map<short, MessageHandler> _fun_callbacks;

    std::shared_ptr<UserSessionDirectory> _directory;
    std::shared_ptr<UserPresenceStore> _presence;
};
