#pragma once
#include "Singleton.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <atomic>

/** @brief 配置服务滚动文件日志器；关闭时只刷新，注册表保留以供静态析构期间继续记录。 */
class LogMgr : public Singleton<LogMgr>
{
	friend class Singleton<LogMgr>;
public:
	/** @brief 调用幂等刷新并释放自身日志器引用，最终注册表清理由 spdlog 负责。 */
	~LogMgr();

	// 关闭日志单例
	/** @brief 幂等刷新已有日志器，不注销或关闭 spdlog 注册表。 */
	void Close();
	// 初始化日志系统
	/** @brief 按配置建立滚动文件日志器并设置默认入口；创建失败返回 false，配置转换异常可向上传播。 */
	bool InitLogMgr();
private:
	/** @brief 初始化未关闭状态，日志器由 InitLogMgr 显式创建。 */
	LogMgr();
	
	// 将config.ini中的日志等级转换为spdlog中的日志等级枚举
	/** @brief 将配置字符串映射为日志级别，未知值使用默认级别。 */
	spdlog::level::level_enum GetLevel(const std::string& level, spdlog::level::level_enum default_level);

	std::atomic<bool> _b_stop;

	std::shared_ptr<spdlog::logger> _logger;
};

