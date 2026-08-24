#include "MsgNode.h"
#include "ChatFrameCodec.h"

#include <cstring>
#include <stdexcept>

namespace {

std::size_t CheckedSendTotalLength(const char* message, std::size_t message_length) {
	if (message_length > MAX_LENGTH) {
		throw std::length_error("message body exceeds MAX_LENGTH");
	}
	if (message_length > 0 && message == nullptr) {
		throw std::invalid_argument("message body must not be null");
	}
	return message_length + HEAD_TOTAL_LEN;
}

std::size_t CheckedSendTotalLength(const std::string& message, std::size_t message_length) {
	if (message_length > MAX_LENGTH) {
		throw std::length_error("message body exceeds MAX_LENGTH");
	}
	if (message_length > message.size()) {
		throw std::invalid_argument("message length exceeds the source string");
	}
	return message_length + HEAD_TOTAL_LEN;
}

} // namespace

MsgNode::MsgNode(std::size_t total_len) : _cur_len(0), _total_len(total_len){
	_data = new char[_total_len + 1]();
	_data[_total_len] = '\0';
}

MsgNode::~MsgNode() {
	delete[] _data;
}

void MsgNode::Clear() {
	std::memset(_data, 0, _total_len);
	_cur_len = 0;
}

/**
 * @brief 
 * @param msg 
 * @param msgId 
 * @param msgLen 
 * 注意要将本地字节序转换为网络字节序
 */
SendNode::SendNode(const char* msg, std::uint16_t msgId, std::size_t msgLen) : MsgNode(CheckedSendTotalLength(msg, msgLen)), _msg_id(msgId) {
	// id本地转网络
	const auto header = ChatFrameCodec::EncodeHeader(
		static_cast<std::uint16_t>(msgId), static_cast<std::uint16_t>(msgLen));
	memcpy(_data, header.data(), header.size());
	// msgLen本地转网络
	// 数据
	memcpy(_data + HEAD_TOTAL_LEN, msg, msgLen);
}
/**
 * @brief 
 * @param msg 要发送的数据
 * @param msgId 要发的数据id
 * @param msgLen 要发送的数据长度
 * 注意要将本地字节序转换为网络字节序
 */
SendNode::SendNode(const std::string& msg, std::uint16_t msgId, std::size_t msgLen) : MsgNode(CheckedSendTotalLength(msg, msgLen)), _msg_id(msgId) {
	// id本地转网络
	const auto header = ChatFrameCodec::EncodeHeader(
		static_cast<std::uint16_t>(msgId), static_cast<std::uint16_t>(msgLen));
	memcpy(_data, header.data(), header.size());
	// msgLen本地转网络
	// 数据
	memcpy(_data + HEAD_TOTAL_LEN, msg.c_str(), msgLen);
}

SendNode::~SendNode() {

}

RecvNode::RecvNode(std::size_t maxLen, std::uint16_t msgId) : MsgNode(maxLen), _msg_id(msgId) {

}

RecvNode::~RecvNode() {

}
