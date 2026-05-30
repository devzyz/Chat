#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <memory>
#include <iostream>
#include "Singleton.h"
#include <map>
#include <functional>
#include <unordered_map>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cassert>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

enum ErrorCodes {
	Success = 0,
	Error_Json = 1001, // json解析错误
	RPCFailed = 1002, // rpc请求错误
	VarifyExpired = 1003, // 验证码过期
	VarifyCodeErr = 1004, // 验证码错误
	UserExist = 1005, // 用户已存在
	PasswdErr = 1006, // 密码错误
	EmailNotMatch = 1007, // 邮箱不匹配
	PasswdUpFailed = 1008, // 更新密码失败
	PasswdInvalid = 1009, // 密码更新失败
	UidInvalid = 1010, // 用户uid无效
	TokenInvalid = 1011, // 用户登录的token无效
};

// 用于实现在defer类析构时，自动执行构造传递的lambda或者function函数
class Defer {
public :
	Defer(std::function<void()> func) : _func(func) {}
	~Defer() {
		_func();
	}
private:
	std::function<void()> _func;
};

#define USER_IP_PREFIX "uip_"
#define USER_TOKEN_PREFIX "utoken_"
#define IP_COUNT_PREFIX "ipcount_"
#define USER_BASE_INFO "ubaseinfo_" // 用户基本信息的uid前缀，ubaseinfo_1，即1号用户的基本信息
#define LOGIN_COUNT "logincount" // 用来查询某个chatserver服务器，登录的客户端tcp连接数