#include "../../../common/message/MessageReceipts.h"
#include <gtest/gtest.h>

/** 验证回执请求拒绝重复条目、非法等级及格式错误。 */
TEST(MessageReceipt, RejectsMalformedAndDuplicateReports) {
    Json::Value request;
    request["version"] = 1; request["chat_id"] = 12; request["request_id"] = "receipt-test";
    Json::Value item;
    item["message_id"] = 10; item["level"] = "read";
    request["items"].append(item);
    EXPECT_NO_THROW(messaging::ValidateReceiptRequest(request, true));
    request["items"].append(item);
    EXPECT_THROW(messaging::ValidateReceiptRequest(request, true), messaging::ReceiptError);
    request["items"].resize(1);
    request["items"][0]["level"] = "sent";
    EXPECT_THROW(messaging::ValidateReceiptRequest(request, true), messaging::ReceiptError);
    request["items"][0]["level"] = "read";
    request["items"][0]["message_id"] = 1.5;
    EXPECT_THROW(messaging::ValidateReceiptRequest(request, true), messaging::ReceiptError);
    request["items"][0]["message_id"] = 10;
    request["request_id"] = std::string(65, 'x');
    EXPECT_THROW(messaging::ValidateReceiptRequest(request, true), messaging::ReceiptError);
}

/** 验证 revision 只接受无符号规范十进制字符串且不溢出有符号 64 位范围。 */
TEST(MessageReceipt, RevisionIsAnExactCanonicalInteger) {
    EXPECT_EQ(messaging::ReceiptRevision(Json::Value("0")), 0);
    EXPECT_EQ(messaging::ReceiptRevision(Json::Value("9223372036854775807")), INT64_MAX);
    for (const auto* invalid : {"-1", "+1", "01", "1.5", "", " 1", "9223372036854775808"})
        EXPECT_THROW(messaging::ReceiptRevision(Json::Value(invalid)), messaging::ReceiptError);
    EXPECT_THROW(messaging::ReceiptRevision(Json::Value(1)), messaging::ReceiptError);
}
