#pragma once
#include <iostream>
#include <cstddef>
#include <cstdint>
#include "Const.h"
#include <boost/asio.hpp>

class MsgNode
{
public:
	MsgNode(std::size_t total_len);
	~MsgNode();
	void Clear();

	std::size_t _cur_len;
	std::size_t _total_len;
	char* _data;
};

class SendNode : public MsgNode {
public:
	SendNode(const char* msg, std::uint16_t msgId, std::size_t msgLen);
	SendNode(const std::string& msg, std::uint16_t msgId, std::size_t msgLen);
	~SendNode();

	std::uint16_t _msg_id;
};

class RecvNode : public MsgNode {
public:
	RecvNode(std::size_t maxLen, std::uint16_t msgId);
	~RecvNode();

	std::uint16_t _msg_id;
};
