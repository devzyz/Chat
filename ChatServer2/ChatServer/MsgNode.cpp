#include "MsgNode.h"

MsgNode::MsgNode(short total_len) : _cur_len(0), _total_len(total_len){
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
SendNode::SendNode(const char* msg, short msgId, short msgLen) : MsgNode(msgLen + HEAD_TOTAL_LEN), _msg_id(msgId) {
	// id本地转网络
	int netMsgId = boost::asio::detail::socket_ops::host_to_network_short(msgId);
	memcpy(_data, &netMsgId, HEAD_ID_LEN);
	// msgLen本地转网络
	int netMsgLen = boost::asio::detail::socket_ops::host_to_network_short(msgLen);
	memcpy(_data + HEAD_ID_LEN, &netMsgLen, HEAD_DATA_LEN);
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
SendNode::SendNode(const std::string& msg, short msgId, short msgLen) : MsgNode(msgLen + HEAD_TOTAL_LEN), _msg_id(msgId) {
	// id本地转网络
	int netMsgId = boost::asio::detail::socket_ops::host_to_network_short(msgId);
	memcpy(_data, &netMsgId, HEAD_ID_LEN);
	// msgLen本地转网络
	int netMsgLen = boost::asio::detail::socket_ops::host_to_network_short(msgLen);
	memcpy(_data + HEAD_ID_LEN, &netMsgLen, HEAD_DATA_LEN);
	// 数据
	memcpy(_data + HEAD_TOTAL_LEN, msg.c_str(), msgLen);
}

SendNode::~SendNode() {

}

RecvNode::RecvNode(short maxLen, short msgId) : MsgNode(maxLen), _msg_id(msgId) {

}

RecvNode::~RecvNode() {

}