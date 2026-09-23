#pragma once
#include <iostream>
#include <cstddef>
#include <cstdint>
#include "Const.h"
#include <boost/asio.hpp>

/** @brief 独占消息字节缓冲并保存容量和已处理长度，析构时释放缓冲。 */
class MsgNode
{
public:
	/** @brief 按给定字节数分配并清零独占消息缓冲。 */
	MsgNode(std::size_t total_len);
	/** @brief 释放自身独占的消息字节缓冲。 */
	~MsgNode();
	/** @brief 清零消息缓冲并重置当前已处理长度。 */
	void Clear();

	std::size_t _cur_len;
	std::size_t _total_len;
	char* _data;
};

/** @brief 持有编码后的完整发送帧，复制传入正文以隔离调用方缓冲生命周期。 */
class SendNode : public MsgNode {
public:
	/** @brief 校验正文长度并复制为带帧头的发送缓冲；指针重载要求输入字节可读，字符串重载同时验证输入长度。 */
	SendNode(const char* msg, std::uint16_t msgId, std::size_t msgLen);
	/** @brief 校验正文长度并复制为带帧头的发送缓冲；指针重载要求输入字节可读，字符串重载同时验证输入长度。 */
	SendNode(const std::string& msg, std::uint16_t msgId, std::size_t msgLen);
	/** @brief 结束缓冲对象生命周期，字节存储由 MsgNode 基类释放。 */
	~SendNode();

	std::uint16_t _msg_id;
};

/** @brief 持有接收正文缓冲和消息编号，不包含传输连接所有权。 */
class RecvNode : public MsgNode {
public:
	/** @brief 按正文容量创建接收缓冲并保存协议消息编号。 */
	RecvNode(std::size_t maxLen, std::uint16_t msgId);
	/** @brief 结束缓冲对象生命周期，字节存储由 MsgNode 基类释放。 */
	~RecvNode();

	std::uint16_t _msg_id;
};
