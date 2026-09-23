#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace integration {

using RunDeadline = std::chrono::steady_clock::time_point;

/** 保存本次运行预约的数值回环地址及端口。 */
struct LoopbackPort {
	std::string address;
	std::uint16_t port = 0;
};

/** 以 PID 和创建时间共同标识进程，防止 PID 复用误操作。 */
struct ProcessIdentity {
	std::uint32_t pid = 0;
	std::uint64_t creation_time = 0;
};

/** 比较 PID 与创建时间是否共同一致。 */
bool operator==(const ProcessIdentity& left, const ProcessIdentity& right) noexcept;

/** 表示启动前由所属运行预约的进程所有权槽。 */
struct ProcessSlot {
	std::uint64_t value = 0;
};

/** 保存单项清理是否完成及安全诊断。 */
class CleanupStatus {
public:
	/** 构造成功清理结果及可选说明。 */
	static CleanupStatus Success(std::string detail = {});
	/** 构造失败清理结果并保存说明。 */
	static CleanupStatus Failure(std::string detail);

	/** 查询该项清理是否完成。 */
	bool Complete() const noexcept;
	/** 借用清理说明，对象修改或销毁后引用失效。 */
	const std::string& Detail() const noexcept;

private:
	/** 保存清理完成标记及说明文本。 */
	CleanupStatus(bool complete, std::string detail);

	bool complete_ = false;
	std::string detail_;
};

/** 汇总首次主失败与全部清理失败，清理失败不得掩盖主失败。 */
struct RunOutcome {
	bool complete = false;
	std::optional<std::string> primary_failure;
	std::vector<std::string> cleanup_failures;
};

using CleanupAction = std::function<CleanupStatus()>;

/** 拥有单次测试的端口、临时目录与进程账本，逆序幂等清理；须比借用它的进程夹具活得更久。 */
class RunContext {
public:
	/** 校验有限运行期限并创建独占临时根及清理账本，失败抛异常。 */
	static std::unique_ptr<RunContext> Create(RunDeadline deadline);

	/** 执行已登记资源清理，结果查询须在析构前完成。 */
	~RunContext();
	/** 禁止复制，避免重复拥有运行资源。 */
	RunContext(const RunContext&) = delete;
	/** 禁止复制赋值，避免重复拥有运行资源。 */
	RunContext& operator=(const RunContext&) = delete;
	/** 禁止移动，保持借用者和清理回调中的地址稳定。 */
	RunContext(RunContext&&) = delete;
	/** 禁止移动赋值，保持借用者和清理回调中的地址稳定。 */
	RunContext& operator=(RunContext&&) = delete;

	/** 借用当前运行的唯一标识，对象存活期间有效。 */
	const std::string& RunId() const noexcept;
	/** 返回本次运行的硬期限。 */
	RunDeadline Deadline() const noexcept;
	/** 用运行身份和合法逻辑名生成隔离测试身份，非法名称抛异常。 */
	std::string SyntheticIdentity(const std::string& logical_name) const;
	/** 借用本次运行拥有的临时根路径。 */
	const std::filesystem::path& TempRoot() const noexcept;

	/** 按唯一合法名称独占预约随机回环端口并登记清理；无效或重复名称抛异常。 */
	LoopbackPort ReserveLoopbackPort(const std::string& name);
	/** 执行指定端口的未执行清理项，未找到或已执行时返回假。 */
	bool ReleaseLoopbackPort(const std::string& name);
	/** 仅在所属根内创建不存在的相对目录并登记清理，拒绝越界和接管已有目录。 */
	std::filesystem::path CreateOwnedDirectory(const std::filesystem::path& relative_path);
	/** 仅清理已登记且尚未清理的所属路径，其他路径返回假。 */
	bool CleanupOwnedPath(const std::filesystem::path& path);

	/** 在进程启动前预约唯一合法逻辑槽，结束后的运行不能再登记。 */
	ProcessSlot ReserveProcessSlot(const std::string& name);
	/** 把预约槽提交为 PID、创建时间和清理动作，拒绝缺失、重复或非所属槽。 */
	void CommitProcess(ProcessSlot slot, ProcessIdentity identity, CleanupAction cleanup);
	/** 查询完整进程身份是否仍在所有权账本中。 */
	bool IsOwnedProcess(ProcessIdentity identity) const;
	/** 按完整身份执行一次所属清理，成功后移除进程记录。 */
	bool CleanupProcess(ProcessIdentity identity);

	/** 仅记录首次主失败并脱敏，后续失败不能覆盖它。 */
	void RecordPrimaryFailure(std::string failure);
	/** 逆序执行未完成清理并汇总结果，重复调用返回已保存结果。 */
	RunOutcome Teardown() noexcept;

private:
	class Impl;
	/** 接管已初始化的运行实现及资源账本。 */
	explicit RunContext(std::unique_ptr<Impl> impl);

	std::unique_ptr<Impl> impl_;
};

} // namespace integration
