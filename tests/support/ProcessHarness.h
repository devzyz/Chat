#pragma once

#include "RunContext.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace integration {

/** 描述待启动程序、参数、所属工作目录及输出证据上限。 */
struct ProcessSpec {
	std::filesystem::path executable;
	std::vector<std::wstring> arguments;
	std::filesystem::path working_directory;
	std::size_t evidence_limit = 64 * 1024;
};

/** 保存进程身份、脱敏输出和就绪、停机、升级终止与管道关闭证据。 */
struct ProcessEvidence {
	ProcessIdentity identity;
	std::optional<std::uint32_t> exit_code;
	std::string stdout_text;
	std::string stderr_text;
	bool ready_probe_attempted = false;
	bool ready_probe_succeeded = false;
	bool graceful_stop_attempted = false;
	bool escalated = false;
	bool identity_mismatch_refused = false;
	bool pipe_readers_closed = false;
};

using ReadyProbe = std::function<bool()>;

/** 组合平台进程适配器与运行所有权账本，执行有界就绪和停机；所属 RunContext 必须仍存活。 */
class ProcessHarness {
public:
	/** 验证所属目录及运行期限后启动并登记进程，登记失败则清理并抛异常。 */
	static std::unique_ptr<ProcessHarness> Start(RunContext& context, ProcessSpec spec);

	/** 停止所属进程并从运行账本执行对应清理。 */
	~ProcessHarness();
	/** 禁止复制进程所有权。 */
	ProcessHarness(const ProcessHarness&) = delete;
	/** 禁止复制赋值进程所有权。 */
	ProcessHarness& operator=(const ProcessHarness&) = delete;
	/** 禁止移动以保持夹具身份及生命周期稳定。 */
	ProcessHarness(ProcessHarness&&) = delete;
	/** 禁止移动赋值以保持夹具身份及生命周期稳定。 */
	ProcessHarness& operator=(ProcessHarness&&) = delete;

	/** 返回所启动进程的 PID 与创建时间。 */
	ProcessIdentity Identity() const;
	/** 在不超过运行硬期限的期限内轮询就绪探针；进程退出或期限到达返回假。 */
	bool WaitReady(const ReadyProbe& probe, RunDeadline deadline);
	/** 幂等请求正常退出，宽限期后按完整身份升级终止并清理管道，返回清理结果。 */
	CleanupStatus Stop(RunDeadline graceful_deadline);
	/** 取得当前进程和生命周期证据快照，捕获输出经脱敏处理。 */
	ProcessEvidence CollectEvidence() const;

private:
	class Impl;
	/** 共享持有已登记到运行账本的进程实现。 */
	explicit ProcessHarness(std::shared_ptr<Impl> impl);

	std::shared_ptr<Impl> impl_;
};

} // namespace integration
