#include "CSession.h"
#include "CServer.h"
#include <boost/uuid.hpp>
#include <iostream>
#include "LogicSystem.h"
#include "RedisMgr.h"
#include "LogMgr.h"

CSession::CSession(boost::asio::io_context& ioc, std::shared_ptr<CServer> server) : 
	_socket(ioc), _server(server), _b_stop(false), _user_uid(0),
	_last_heart_beat(time(nullptr)), _b_head_parse(false) {
	// 通过雪花算法，为每个session连接生成一个唯一的uuid，方便由server管理会话
	boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
	_session_id = boost::uuids::to_string(a_uuid);

	_recv_head_node = std::make_shared<MsgNode>(HEAD_TOTAL_LEN); // 接收头部节点
}

CSession::~CSession() {
	SPDLOG_DEBUG("CSession destructed, session_id={}", _session_id);
}

boost::asio::ip::tcp::socket& CSession::GetSocket() {
	return _socket;
}

/**
 * @brief 
 * 开始接收
 */
void CSession::Start() {
	AsyncReadHead(HEAD_TOTAL_LEN);
}

/**
 * @brief 
 * @param head_total_len 头部需要读取的长度
 * 接收完成的头部
 */
void CSession::AsyncReadHead(std::size_t head_total_len) {
	auto self = shared_from_this();

	asyncReadFull(head_total_len, [self, this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
		try {
			// 如果是正常的可交互的，肯定不会走到这里，走到这里说明是有异常，例如客户端主动断开连接
			if (ec) {
				SPDLOG_DEBUG("session header read failed, session_id={}, error={}", _session_id, ec.message());
				Close();
				// 出错后的处理
				DealExceptionSession();

				return;
			}

			// 判断连接是否有效
			if (!_server->CheckSessionValid(_session_id)) {
				Close();
				return;
			}

			_recv_head_node->Clear();
			memcpy(_recv_head_node->_data, _data, bytes_transferred);

			// 拿到了头部的字节流数据，接下来开始解析
			// 解析id
			short msg_id = 0;
			memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
			msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);

			// id非法，断开连接
			if (msg_id > MAX_LENGTH) {
				SPDLOG_WARN("invalid msg_id, session_id={}, msg_id={}", _session_id, msg_id);
				_server->ClearSession(_session_id);
				return;
			}

			// 解析data_len
			short msg_len = 0;
			memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
			msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);

			// 长度非法，断开连接
			if (msg_len > MAX_LENGTH) {
				SPDLOG_WARN("invalid msg_len, session_id={}, msg_len={}", _session_id, msg_len);
				_server->ClearSession(_session_id);
				return;
			}

			_recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);
			AsyncReadBody(msg_len);

			UpdateHeartBeat();
		}
		catch (std::exception& e) {
			SPDLOG_ERROR("session header read exception, session_id={}, error={}", _session_id, e.what());
		}
	});
}

/**
 * @brief 
 * @param maxLength 
 * @param handler 
 * 封装的异步读函数，完整的读取长度maxLength或者发成错误后，触发handler回调
 */
void CSession::asyncReadFull(std::size_t maxLength,
	std::function<void(const boost::system::error_code& ec, std::size_t bytestransferred)> handler) {
	std::memset(_data, 0, maxLength);
	asyncReadLen(0, maxLength, handler);
}

/**
 * @brief
 * @param read_len 目前已经读取了多少
 * @param total_len 总共需要读多少
 * @param handler 回调函数
 * 读取指定的字节数
 */
void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len,
	std::function<void(const boost::system::error_code& ec, std::size_t bytestransferred)> handler) {
	auto self = shared_from_this();

	_socket.async_read_some(boost::asio::buffer(_data + read_len, total_len - read_len),
		[read_len, total_len, handler, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
			if (ec) {
				// 出现错误，调用回调函数;read_len + bytes_transferred表示一共读取了多少
				handler(ec, read_len + bytes_transferred);
				return;
			}

			if (read_len + bytes_transferred >= total_len) {
				// 长度够了，调用回调函数
				handler(ec, read_len + bytes_transferred);
				return;
			}

			// 没有错误，且长度不够，则继续读取
			self->asyncReadLen(read_len + bytes_transferred, total_len, handler);
	});
}
/**
 * @brief 
 * @param body_total_len 数据长度
 * 读取头部后面对应的数据
 */
void CSession::AsyncReadBody(std::size_t body_total_len) {
	auto self = shared_from_this();

	asyncReadFull(body_total_len, [self, this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
		try {
			// 出现错误，断开服务器的链接
			// 因为服务器踢人逻辑中，都是通过给客户端发送一个信号，由客户端断开链接
			// 因此当接受到错误信息后，代表此时客户端已经断开链接了，此时要清理到对应的session
			if (ec) {
				SPDLOG_DEBUG("session body read failed, session_id={}, error={}", _session_id, ec.message());
				Close();

				DealExceptionSession();
				return;
			}

			// 拷贝数据
			memcpy(_recv_msg_node->_data, _data, bytes_transferred);
			_recv_msg_node->_cur_len += bytes_transferred;
			_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';

			// 将消息体投递到逻辑队列中进行处理
			LogicSystem::GetInstance()->PostMsgToQue(std::make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
			// 继续接收完整的头部
			AsyncReadHead(HEAD_TOTAL_LEN);

			UpdateHeartBeat();
		}
		catch (std::exception& e) {
			SPDLOG_ERROR("session body read exception, session_id={}, error={}", _session_id, e.what());
		}
	});
}

/**
 * @brief 
 * @param msg 
 * @param msg_id 
 * @param msg_len 
 * 异步发送函数
 */
void CSession::Send(const char* msg, short msg_id, short msg_len) {
	std::lock_guard<std::mutex> lock(_send_mutex);
	auto send_que_size = _send_que.size();
	if (_send_que.size() > MAX_SENDQUE) {
		SPDLOG_ERROR("session send queue full, session_id={}, max_size={}", _session_id, MAX_SENDQUE);
		return;
	}
	_send_que.push(std::make_shared<SendNode>(msg, msg_id, msg_len));
	// 本来队列中就又在发送的数据
	if (send_que_size > 0) {
		return;
	}
	// 原本队列中没有在发送的数据；则将当前的这个要发送的数据发出
	auto& msgnode = _send_que.front();
	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
		std::bind(&CSession::HandleWrite, this, std::placeholders::_1, shared_from_this()));
}

/**
 * @brief 
 * @param ec 
 * @param self 
 * async_write内部由多次async_write_some，如果如果我同时async_write两次，那么可能这两个都会往socket写数据就会发生错误
 * 因此我必须保证上一次完整发送完成之后，我再发送下一个，可以用一个队列来保存要发送的数据，然后在async_write的
 * 回调函数内部来进行下一个包的发送
 */
void CSession::HandleWrite(const boost::system::error_code& ec, std::shared_ptr<CSession> self) {
	try {
		if (!ec) {
			std::lock_guard<std::mutex> lock(_send_mutex);
			_send_que.pop();
			if (_send_que.empty()) {
				return;
			}
			auto& msgnode = _send_que.front();
			boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len), 
				std::bind(&CSession::HandleWrite, this, std::placeholders::_1, self));
		}
		else {
			Close();
		
			DealExceptionSession();
			return;
		}
	}
	catch (std::exception& e) {
		SPDLOG_ERROR("session send exception, session_id={}, error={}", _session_id, e.what());
	}
}

void CSession::Send(const std::string& msg, short msg_id) {
	Send(msg.c_str(), msg_id, msg.size());
}

void CSession::Close() {
	if (_b_stop) {
		return;
	}
	std::lock_guard<std::mutex> lock(_session_mutex);
	_socket.close();
	_b_stop = true;
}


std::string& CSession::GetSessionId() {
	return _session_id;
}

void CSession::SetUserId(int uid) {
	_user_uid = uid;
}

int CSession::GetUserId() {
	return _user_uid;
}


// 检测与当前session连接的客户端的心跳是否超时，心跳超时返回ture，否则返回false
bool CSession::CheckHeartBeatAccurate(std::time_t& now) {
	// 检测一下当前时间与上一次心跳时间之间的差值
	double dlt = std::difftime(now, _last_heart_beat);
	// 如果心跳间隔大于规定的心跳时间
	if (dlt > HEARTBEAT_TIME_INTERVAL) {
		return true;
	}
	return false;
}

// 更新当前的心跳时间
void CSession::UpdateHeartBeat() {
	std::time_t now = time(nullptr);
	_last_heart_beat = now;
}

// 清除redis中当前session的连接信息
void CSession::DealExceptionSession() {
	if (_user_uid <= 0) {
		_server->ClearSession(_session_id);
		return;
	}

	// 添加分布式锁，清除redis中保存的session,ipserver信息等
	auto uid_str = std::to_string(_user_uid);
	auto lock_key = LOCK_PREFIX + uid_str;
	// 获取到对uid的锁
	auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, LOCK_ACQUIRE_TIME_OUT);
	Defer defer([this, identifier, lock_key]() {
		_server->ClearSession(_session_id); // 清除server中session连接
		RedisMgr::GetInstance()->releaseLock(lock_key, identifier); // 释放锁
		});

	// 没有获得锁
	if (identifier == "") {
		return;
	}

	// 去redis中查看当前的uid对应的session_id是不是本session的，如果是，则清空redis,否则代表已被其他链接更新了，则不做其他处理
	std::string redis_session_id_value = "";
	auto redis_session_id_key = USER_SESSION_KEY + uid_str;
	auto b_success = RedisMgr::GetInstance()->Get(redis_session_id_key, redis_session_id_value);
	// 出现错误
	if (!b_success) {
		return;
	}

	// 如果不相同，代表已经再次登录了，则直接返回
	if (redis_session_id_value != _session_id) {
		return;
	}

	// 走到这里代表，当前session要断开链接，并且没有再次登录
	RedisMgr::GetInstance()->Del(redis_session_id_key); // 清除session信息
	RedisMgr::GetInstance()->Del(USER_IP_PREFIX + uid_str); // 清除ip信息
}

LogicNode::LogicNode(std::shared_ptr<CSession> session, std::shared_ptr<RecvNode> recvnode) 
	: _session(session), _recv_msg_node(recvnode) {

}

LogicNode::~LogicNode() {

}
