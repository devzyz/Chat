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
	MSG_CHAT_LOGIN_REQ = 1005, // 用户登陆
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
	MSG_NOTIFY_OFF_LINE_REQ = 1019, // 服务器通知客户端离线
	MSG_HEART_BEAT_REQ = 1020, // 客户端心跳请求
	MSG_HEART_BEAT_RSP = 1021, // 客户端心跳请求回包
	MSG_CREATE_PRIVATE_CHAT_REQ = 1023, // 创建新的私聊请求
	MSG_CREATE_PRIVATE_CHAT_RSP = 1024, // 创建新的私聊请求回包
	MSG_LOAD_CHAT_LIST_REQ = 1025, // 查询部分的聊天列表请求
	MSG_LOAD_CHAT_LIST_RSP = 1026, // 查询部分的聊天列表请求回包
	MSG_LOAD_CHAT_MESSAGE_REQ = 1027, // 增量加载部分聊天记录
	MSG_LOAD_CHAT_MESSAGE_RSP = 1028, // 增量加载部分聊天记录回包
};

#define USER_IP_PREFIX "uip_"
#define USER_TOKEN_PREFIX "utoken_"
#define IP_COUNT_PREFIX "ipcount_"
#define USER_BASE_INFO "ubaseinfo_" // 用户基本信息的uid前缀，ubaseinfo_1，即1号用户的基本信息
#define LOGIN_COUNT "logincount" // 用来查询某个chatserver服务器，登录的客户端tcp连接数
#define USER_NAME_INFO "unameinfo_" // 通过name查询用户信息的前缀，unameinfo_zzzyz
#define LOCK_PREFIX "lock_" // 分布式锁的名字前缀，例如lock_1001
#define USER_SESSION_KEY "usessionid_"

// 锁的持有时间
#define LOCK_TIME_OUT 10
// 获取锁的尝试时间
#define LOCK_ACQUIRE_TIME_OUT 5
// 心跳超时的时间间隔 单位秒
#define HEARTBEAT_TIME_INTERVAL 20
// 数据库尝试重连的最大重试次数，如果此次不成功，则下次心跳时再进行重试
#define MYSQL_MAX_RETRIES 5
// redis尝试重连的最大重试次数，如果此次不成功，则下次心跳时再进行重试
#define REDIS_MAX_RETRIES 5

#define PAGE_SIZE 10
