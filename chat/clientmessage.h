#pragma once

#include "messagerecord.h"
#include "userdata.h"
#include <memory>

MessageRecord clientMessageRecord(const std::shared_ptr<ChatDataBase> &message);
