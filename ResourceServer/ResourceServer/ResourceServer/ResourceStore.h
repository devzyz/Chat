#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace resource {
/** @brief 携带 HTTP 状态码的资源业务错误；文件系统及解析错误也可能以原生异常传播。 */
struct Error : std::runtime_error {
    /** @brief 保存状态码及错误说明，供 HTTP 层转换响应。 */
    Error(int status, const std::string& message) : std::runtime_error(message), status(status) {}
    int status;
};
/** @brief 保存资源元数据快照；size 为声明长度，offset 为已落盘长度，ready 表示已完成发布文件。 */
struct Metadata {
    std::string id;
    int owner = 0;
    std::string name;
    std::string media_type;
    std::uint64_t size = 0;
    std::uint64_t offset = 0;
    bool ready = false;
    std::string sha256;
};
/**
 * @brief 持有资源根目录配置并同步读写上传文件；调用方必须在单一存储执行器串行调用。
 * @note 本类不加锁，不验证下载权限；业务失败抛 Error，文件系统和 JSON 异常可直接传播。
 */
class ResourceStore {
public:
    /** @brief 固定绝对根路径并创建目录；max_size 单位为字节且必须大于零，失败抛异常。 */
    ResourceStore(std::filesystem::path root, std::uint64_t max_size,
        std::uint64_t total_limit = 64ull * 1024 * 1024 * 1024,
        std::uint64_t owner_limit = 16ull * 1024 * 1024 * 1024);
    /** @brief 校验元数据、创建分块文件后原子发布描述；中断创建由下次 Create 回收。 */
    Metadata Create(int owner, const std::string& name, const std::string& type,
                    std::uint64_t size, const std::string& sha256);
    /** @brief 读取元数据及实际文件长度，不检查调用者权限；不存在抛 404，非法 id 抛 400。 */
    Metadata Inspect(const std::string& id) const;
    /** @brief 读取快照并检查 owner，所有者不符抛 403；未完成任务续租七天保留期。 */
    Metadata Owned(const std::string& id, int owner) const;
    /**
     * @brief 从当前字节偏移追加至多 BUFFER_SIZE 字节，data 在同步调用期间必须有效。
     * @note 已完成或偏移不符抛 409，超声明长度抛 413；写失败可能已有部分落盘，重试前须查询偏移。
     */
    Metadata Append(const std::string& id, int owner, std::uint64_t offset,
                    const char* data, std::size_t size);
    /**
     * @brief 校验完整长度、摘要及媒体签名后将临时文件重命名为完成文件；已完成时幂等返回。
     * @note 未传完抛 409，摘要不符抛 422，签名不符抛 415；不负责数据库发布或消息通知。
     */
    Metadata Complete(const std::string& id, int owner);
    /**
     * @brief 从 offset 读取至多 min(limit, BUFFER_SIZE) 字节，可读上传中的临时文件。
     * @note 超出已落盘长度抛 416，恰好等于长度返回空；下载授权必须由上层完成。
     */
    std::vector<char> Read(const std::string& id, std::uint64_t offset, std::size_t limit) const;
    /** @brief 分块计算文件的小写 SHA-256；文件不存在抛 404，读取或摘要操作失败抛 500。 */
    static std::string Digest(const std::filesystem::path& path);
    static constexpr std::size_t BUFFER_SIZE = 64 * 1024;
private:
    /** @brief 回收中断创建和七天未活动上传；完成文件保留，删除标记支持中断恢复。 */
    void CollectExpiredUploads();
    /** @brief 完成已标记上传的清理；遇到完成文件、符号链接或非普通文件时拒绝删除。 */
    void FinishDiscard(const std::string& id);
    /** @brief 校验 id 的长度及字符后拼接内部 suffix 路径，不进行完整 UUID 语义验证。 */
    std::filesystem::path Path(const std::string& id, const char* suffix) const;
    /** @brief 按已声明大小保留全局和用户额度，限制未完成上传数并保留磁盘余量。 */
    void CheckQuota(int owner, std::uint64_t requested) const;
    std::filesystem::path _root;
    std::uint64_t _max_size;
    std::uint64_t _total_limit;
    std::uint64_t _owner_limit;
};
}
