#pragma once
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>

/** @brief 以共享指针提供一次初始化的进程内实例，具体并发操作由派生类负责。 */
template<typename T>
class Singleton
{
public:
	/** @brief 返回以静态状态持有的共享实例；首次构造的配置和依赖由调用前初始化决定。 */
	static std::shared_ptr<T> GetInstance() {
		static std::shared_ptr<T> _instance = std::shared_ptr<T>(new T());
		return _instance;
	}

	// A singleton can outlive the logging registry during static destruction.
	/** @brief 默认销毁无资源的单例基类，派生对象负责自身资源清理。 */
	~Singleton() = default;
protected:
	/** @brief 仅允许派生类构造单例基类。 */
	Singleton() = default;
	/** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
	Singleton(const Singleton&) = delete;
	/** @brief 禁止复制或赋值，避免重复拥有连接、线程或租约资源。 */
	Singleton& operator = (const Singleton&) = delete;
};

