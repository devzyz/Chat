#pragma once

#include "messagerecord.h"
#include "userdata.h"
#include <memory>

/** @brief 将兼容聊天数据转换为消息模型记录，并补充当前账号及联系人资料。 */
MessageRecord clientMessageRecord(const std::shared_ptr<ChatDataBase> &message);
