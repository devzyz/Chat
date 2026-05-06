#pragma once
#include "const.h"

class HttpConnection;
typedef std::function<void(std::shared_ptr<HttpConnection>)>HttpHandler;
/**
 * @brief 处理系统，客户端的请求信息在被进行预处理后，通过调用处理系统来实现对数据的处理
 */
class LogicSystem : public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;
public:
	~LogicSystem() = default;

	/**
	 * @brief 
	 * @param  
	 * @param  
	 * @return 
	 * 
	 * 实际的处理位置，外部通过调用这里，进行对应的处理
	 */
	bool HandleGet(std::string, std::shared_ptr<HttpConnection>);
	bool HandlePost(std::string, std::shared_ptr<HttpConnection>);

	/**
	 * @brief 
	 * @param  
	 * @param  
	 * 
	 * 注册get请求到_get_handlers内
	 * 注册post请求到_post_handlers内
	 */
	void RegGet(std::string, HttpHandler);
	void RegPost(std::string, HttpHandler);
private:
	/**
	 * @brief 
	 * 
	 * 构造函数，同时将对应的处理逻辑进行注册
	 */
	LogicSystem();
	std::map<std::string, HttpHandler> _post_handlers;
	std::map<std::string, HttpHandler> _get_handlers;
};

