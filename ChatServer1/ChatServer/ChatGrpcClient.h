#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "message.pb.h"
#include <atomic>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "singleton.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::ChatService;

using message::AddFriendReq;
using message::AddFriendRsp;

using message::AuthFriendReq;
using message::AuthFriendRsp;

using message::TextChatMsgReq;
using message::TextChatMsgRsp;

class ChatConnectionPool {
public:
	ChatConnectionPool(const std::string& _host, const std::string& port, std::size_t poolSize);
	~ChatConnectionPool();
	void returnConnectioni(std::unique_ptr<ChatService::Stub>);
	std::unique_ptr<ChatService::Stub> getConnection();
	void close();
private:
	std::atomic<bool> _b_stop;
	std::size_t _pool_size;
	std::string _host;
	std::string _port;
	std::queue<std::unique_ptr<ChatService::Stub>> _connection;

	std::mutex _que_mutex;
	std::condition_variable _cond;
};

class ChatGrpcClient : public Singleton<ChatGrpcClient>
{
	friend class Singleton<ChatGrpcClient>;
public:
	~ChatGrpcClient();
	AddFriendRsp NotifyOtherAddFriend(const std::string& serverIp, const AddFriendReq request);
	AuthFriendRsp NotifyOtherAuthFriend(const std::string& serverIp, const AuthFriendReq request);
	TextChatMsgRsp NotifyOtherReceiveTextChatMsg(const std::string& serverIp, const TextChatMsgReq request);
private:
	ChatGrpcClient();
	std::unordered_map<std::string, std::unique_ptr<ChatConnectionPool>> _pool;
};

