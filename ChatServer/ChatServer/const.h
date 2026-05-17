#pragma once
#include <functional>

#define MAX_LENGTH 1024 * 2
#define MAX_RECVQUE 10000
#define MAX_SENDQUE 1000
#define MAX_DEALQUE 1000

// 头部总长度
#define HEAD_TOTAL_LEN 4
// 头部id长度
#define HEAD_ID_LEN 2
// 头部数据长度
#define HEAD_DATA_LEN 2

class Defer {
public:
	Defer(std::function<void()> func);
	~Defer();
private:
	std::function<void()> _func;
};

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
	TokenInvalid = 1010,   //Token失效
	UidInvalid = 1011,  //uid无效
};

enum MSG_IDS {
	MSG_CHAT_LOGIN = 1005, //用户登陆
	MSG_CHAT_LOGIN_RSP = 1006, //用户登陆回包
};