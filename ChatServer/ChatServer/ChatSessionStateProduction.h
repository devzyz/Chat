#pragma once

#include <memory>

class ChatSessionState;

std::shared_ptr<ChatSessionState> MakeProductionChatSessionState();
