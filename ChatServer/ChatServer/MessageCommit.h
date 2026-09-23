#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace message_commit {

using Deadline = std::chrono::steady_clock::time_point;
// 每项依次为客户端 UUID 与文本正文，结果保持输入顺序。
using Batch = std::vector<std::pair<std::string, std::string>>;

/** @brief 携带已由会话认证的 UID，不能从请求自报的发送者构造。 */
struct AuthenticatedPrincipal { int uid = 0; };

enum class Error {
    NONE, UNAUTHORIZED_SENDER, INVALID_UUID, INVALID_MEMBERSHIP,
    CONFLICT, STORAGE_UNAVAILABLE, DEADLINE_EXCEEDED
};
enum class Disposition { CREATED, EXISTING };

/** @brief 保存单条消息的持久化标识、幂等处理结果及秒级创建时间。 */
struct Item {
    int message_id = 0;
    std::string client_msg_uuid;
    Disposition disposition = Disposition::CREATED;
    std::int64_t created_at = 0;
};
/** @brief 返回整个批次的提交结果；错误时不提供部分成功条目。 */
struct Result {
    Error error = Error::NONE;
    std::vector<Item> items;
    /** @brief 判断批次是否获得成功提交结果，不代表接收者已收到或已读。 */
    bool IsSuccess() const { return error == Error::NONE; }
};

/** @brief 同步原子提交文本批次的存储边界，负责成员关系校验和 UUID 幂等冲突处理。 */
class Store {
public:
    /** @brief 允许经存储接口销毁适配器。 */
    virtual ~Store() = default;
    /** @brief 按输入顺序提交已校验批次；deadline 使用 steady_clock，失败返回错误及空条目。 */
    virtual Result Commit(int sender, int recipient, int chat, const Batch& batch, Deadline deadline) = 0;
};

/** @brief 判断是否为小写十六进制的非全零 8-4-4-4-12 UUID，不校验 UUID 版本。 */
bool IsCanonicalUuid(const std::string& value);
/**
 * @brief 校验认证发送者、目标、1～100 条唯一 UUID 和截止时间后，同步委托存储提交。
 * @note 验证失败不调用 store；空批次或过大批次同样返回 INVALID_UUID。存储异常不在此层捕获。
 */
Result Commit(Store& store, AuthenticatedPrincipal principal, int claimed_sender,
    int recipient, int chat, const Batch& batch, Deadline deadline);

}
