#pragma once
#include <iostream>
#include "const.h"
#include <boost/asio.hpp>

class MsgNode
{
public:
	MsgNode(short total_len);
	~MsgNode();
	void Clear();

	short _cur_len;
	short _total_len;
	char* _data;
};

class SendNode : public MsgNode {
public:
	SendNode(const char* msg, short msgId, short msgLen);
	SendNode(const std::string& msg, short msgId, short msgLen);
	~SendNode();

	short _msg_id;
};

class RecvNode : public MsgNode {
public:
	RecvNode(short maxLen, short msgId);
	~RecvNode();

	short _msg_id;
};

