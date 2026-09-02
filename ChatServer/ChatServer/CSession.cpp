#include "CSession.h"
#include "ChatFrameCodec.h"
#include "ChatSessionStateInternal.h"
#include "CServer.h"
#include <iostream>
#include "LogicSystem.h"
#include "LogMgr.h"

class CSessionWriterAdapter final : public SessionWriter {
public:
	void Bind(const std::shared_ptr<CSession>& session) {
		session_ = session;
	}

	void Write(SessionFrame frame, Completion completion) override {
		auto session = session_.lock();
		if (!session) {
			completion(false);
			return;
		}
		auto node = std::make_shared<SendNode>(frame.body, frame.message_id, frame.body.size());
		boost::asio::async_write(session->_socket,
			boost::asio::buffer(node->_data, node->_total_len),
			[session, node, completion = std::move(completion)](
				const boost::system::error_code& error, std::size_t) mutable {
				completion(!error);
			});
	}
	void Close() override {
		if (auto session = session_.lock()) {
			boost::system::error_code ignored;
			session->_socket.close(ignored);
		}
	}
private:
	std::weak_ptr<CSession> session_;
};

CSession::CSession(boost::asio::io_context& ioc, std::shared_ptr<CServer> server,
	std::shared_ptr<ChatSessionState> session_state) :
	_socket(ioc), _server(std::move(server)), _session_state(std::move(session_state)),
	_writer(std::make_shared<CSessionWriterAdapter>()), _b_stop(false),
	_last_heart_beat(time(nullptr)), _b_head_parse(false) {
	_handle = _session_state->Create(_writer);
	_recv_head_node = std::make_shared<MsgNode>(HEAD_TOTAL_LEN); // 接收头部节点
}

CSession::~CSession() {
	Close();
	SPDLOG_DEBUG("CSession destructed");
}

boost::asio::ip::tcp::socket& CSession::GetSocket() {
	return _socket;
}

/**
 * @brief 
 * 开始接收
 */
void CSession::Start() {
	_writer->Bind(shared_from_this());
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
				SPDLOG_DEBUG("session header read failed, error={}", ec.message());
				Close();
				// 出错后的处理
				DealExceptionSession();

				return;
			}

			// 判断连接是否有效
			if (!_server->CheckSessionValid(_handle)) {
				Close();
				return;
			}

			_recv_head_node->Clear();
			memcpy(_recv_head_node->_data, _data, bytes_transferred);

			// 拿到了头部的字节流数据，接下来开始解析
			// 解析id
			const auto header = ChatFrameCodec::DecodeValidatedHeader(
				_recv_head_node->_data, sizeof(_data));
			if (!header) {
				SPDLOG_WARN("invalid frame body length");
				_server->ClearSession(_handle);
				return;
			}

			const std::uint16_t msg_id = header->message_id;
			const std::size_t msg_len = header->body_length;

			_recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);
			AsyncReadBody(msg_len);

			UpdateHeartBeat();
		}
		catch (std::exception& e) {
				SPDLOG_ERROR("session header read exception, error={}", e.what());
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
				SPDLOG_DEBUG("session body read failed, error={}", ec.message());
				Close();

				DealExceptionSession();
				return;
			}

			// 拷贝数据
			memcpy(_recv_msg_node->_data, _data, bytes_transferred);
			_recv_msg_node->_cur_len += bytes_transferred;
			_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';

			// 将消息体投递到逻辑队列中进行处理
			const auto submit_result = LogicSystem::GetInstance()->Submit({
				shared_from_this(),
				static_cast<std::int16_t>(_recv_msg_node->_msg_id),
				std::string(_recv_msg_node->_data, _recv_msg_node->_cur_len),
			});
			switch (submit_result) {
			case LogicSubmitResult::Accepted:
				break;
			case LogicSubmitResult::Full:
				SPDLOG_WARN("logic message rejected, reason=full, msg_id={}", _recv_msg_node->_msg_id);
				break;
			case LogicSubmitResult::Closed:
				SPDLOG_INFO("logic message rejected, reason=closed, msg_id={}", _recv_msg_node->_msg_id);
				Close();
				return;
			}
			// 继续接收完整的头部
			AsyncReadHead(HEAD_TOTAL_LEN);

			UpdateHeartBeat();
		}
		catch (std::exception& e) {
			SPDLOG_ERROR("session body read exception, error={}", e.what());
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
SessionSendResult CSession::Send(const char* msg, std::uint16_t msg_id, std::size_t msg_len) {
	if (msg_len > 0 && msg == nullptr) {
		return SessionSendResult::Closed;
	}
	return _session_state->Send(_handle, {msg_id, std::string(msg ? msg : "", msg_len)});
}

SessionSendResult CSession::Send(const std::string& msg, std::uint16_t msg_id) {
	return Send(msg.data(), msg_id, msg.size());
}

void CSession::Close() {
	bool expected = false;
	if (!_b_stop.compare_exchange_strong(expected, true)) {
		return;
	}
	boost::system::error_code ignored;
	_socket.close(ignored);
	_session_state->Close(_handle);
}

const ChatSessionState::Handle& CSession::GetHandle() const {
	return _handle;
}

void CSession::SetUserId(int uid) {
	_session_state->RegisterCurrent(_handle, uid);
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
	Close();
	_server->ClearSession(_handle);
}
