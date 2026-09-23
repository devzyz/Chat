#pragma once

#include <QtGlobal>

enum class SendPhase { Queued, InFlight, Uncertain, Rejected, Committed };
enum class ReceiptLevel { None, Delivered, Read };

/** @brief 保存本地发送进度和服务器回执事实，供展示状态计算使用。 */
struct MessageFacts {
    SendPhase phase = SendPhase::Queued;
    ReceiptLevel receipt = ReceiptLevel::None;
    qint64 messageId = 0;
    qint64 attempt = 0;
};

/** @brief 将发送进度和回执事实归约为单一展示状态。 */
class MessageStateReducer final {
public:
    enum class Event { Dispatch, Timeout, Rejected, Commit, Delivered, Read };
    /** @brief 由本地进度及服务器回执生成展示状态，优先保留确认事实。 */
    static MessageFacts reduce(MessageFacts facts, Event event, qint64 attempt = 0, qint64 messageId = 0) {
        if (event == Event::Commit) {
            if (messageId > 0 && (facts.messageId == 0 || facts.messageId == messageId)) {
                facts.messageId = messageId;
                facts.phase = SendPhase::Committed;
            }
        } else if (event == Event::Read || event == Event::Delivered) {
            if (facts.messageId > 0 && facts.messageId == messageId) {
                facts.phase = SendPhase::Committed;
                if (event == Event::Read) facts.receipt = ReceiptLevel::Read;
                else if (facts.receipt == ReceiptLevel::None) facts.receipt = ReceiptLevel::Delivered;
            }
        } else if (facts.messageId == 0) {
            if (event == Event::Dispatch && attempt > facts.attempt) {
                facts.attempt = attempt;
                facts.phase = SendPhase::InFlight;
            } else if ((event == Event::Timeout || event == Event::Rejected)
                       && attempt == facts.attempt && facts.phase == SendPhase::InFlight) {
                facts.phase = event == Event::Rejected ? SendPhase::Rejected : SendPhase::Uncertain;
            }
        }
        return facts;
    }
};
