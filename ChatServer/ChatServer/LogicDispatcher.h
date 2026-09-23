#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class CSession;

/** @brief 持有待分发消息及来源会话的强引用，排队期间保持会话存活。 */
struct LogicMessage {
	std::shared_ptr<CSession> session;
	std::int16_t id;
	std::string body;
};

enum class LogicSubmitResult {
	Accepted,
	Full,
	Closed,
};

/** @brief 用有界 FIFO 和单工作线程分发业务消息，生产者通过内部锁提交。 */
class LogicDispatcher {
public:
	using Handler = std::function<bool(const LogicMessage&)>;

    /** @brief 保存有效 handler 并启动工作线程；handler 返回 false 表示无处理器，异常被记录后继续。 */
	explicit LogicDispatcher(Handler handler);
    /** @brief 停止接收并等待已接受消息排空；不能在 handler 所在线程析构。 */
	~LogicDispatcher();

    /** @brief 禁止复制拥有队列和工作线程的分发器。 */
	LogicDispatcher(const LogicDispatcher&) = delete;
    /** @brief 禁止复制赋值，避免混淆任务和工作线程所有权。 */
	LogicDispatcher& operator=(const LogicDispatcher&) = delete;

    /** @brief 同步返回入队结果；Full/Closed 不入队，Accepted 不代表业务处理成功。 */
	LogicSubmitResult Submit(LogicMessage message);
    /** @brief 幂等停止入队并排空队列后 join；必须从工作线程之外调用，不可与自身析构竞争。 */
	void Stop();

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
