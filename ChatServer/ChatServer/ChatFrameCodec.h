#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "Const.h"

/** @brief 保存网络帧的消息编号和正文长度，数值已转换为主机字节序。 */
struct ChatFrameHeader {
    std::uint16_t message_id = 0;
    std::uint16_t body_length = 0;
};

/** @brief 按协议编解码固定长度帧头，拒绝非法消息编号及超长正文。 */
class ChatFrameCodec {
public:
    using HeaderBytes = std::array<std::uint8_t, HEAD_TOTAL_LEN>;

    /** @brief 将消息编号和正文长度编码为固定网络字节序帧头。 */
    static HeaderBytes EncodeHeader(std::uint16_t message_id, std::uint16_t body_length);
    /** @brief 读取完整固定长度帧头并校验编号和正文上限，非法时返回空 optional。 */
    static std::optional<ChatFrameHeader> DecodeValidatedHeader(
        const void* bytes,
        std::size_t maximum_body_length);
};
