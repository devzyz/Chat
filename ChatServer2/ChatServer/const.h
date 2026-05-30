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
	MSG_CHAT_LOGIN = 1005, // 用户登陆
	MSG_CHAT_LOGIN_RSP = 1006, // 用户登陆回包
	MSG_SEARCH_USER_REQ = 1007, // 搜索用户请求
	MSG_SEARCH_USER_RSP = 1008, // 搜索用户请求回包
	MSG_ADD_FRIEND_REQ = 1009, // 申请添加好友请求
	MSG_ADD_FRIEND_RSP = 1010, // 申请添加好友请求回包
	MSG_NOTIFY_ADD_FRIEND_REQ = 1011, // 通知对方有添加好友请求
	MSG_NOTIFY_ADD_FRIEND_RSP = 1012, // 通知对方有添加好友请求回包
	MSG_AUTH_FRIEND_REQ = 1013, // 认证添加好友请求
	MSG_AUTH_FRIEND_RSP = 1014, // 认证添加好友请求回包
	MSG_NOTIFY_AUTH_FRIEND_REQ = 1015, // 通知认证好友请求
	MSG_TEXT_CHAT_MSG_REQ = 1016, // 发送文本聊天数据请求
	MSG_TEXT_CHAT_MSG_RSP = 1017, // 发送文本聊天数据请求回包
	MSG_NOTIFY_CHAT_MSG_REQ = 1018, // 通知接收文本聊天数据
};

#define USER_IP_PREFIX "uip_"
#define USER_TOKEN_PREFIX "utoken_"
#define IP_COUNT_PREFIX "ipcount_"
#define USER_BASE_INFO "ubaseinfo_" // 用户基本信息的uid前缀，ubaseinfo_1，即1号用户的基本信息
#define LOGIN_COUNT "logincount" // 用来查询某个chatserver服务器，登录的客户端tcp连接数
#define USER_NAME_INFO "unameinfo_" // 通过name查询用户信息的前缀，unameinfo_zzzyz