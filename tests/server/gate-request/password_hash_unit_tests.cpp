#include <gtest/gtest.h>
#include "../../../common/auth/PasswordHash.h"

/** 验证口令随机盐、正确和错误输入、损坏记录及旧格式迁移判定。 */
TEST(GatePasswordTests, SaltedHashesRejectWrongOrMalformedCredentials) {
    const std::string input("fixture\0opaque", 14);
    const auto first = authentication::HashPassword(input);
    const auto second = authentication::HashPassword(input);
    EXPECT_NE(first, second);
    EXPECT_TRUE(authentication::VerifyPassword(input, first));
    EXPECT_FALSE(authentication::VerifyPassword("wrong", first));
    EXPECT_FALSE(authentication::VerifyPassword(input, first.substr(0, first.size() - 1)));
    EXPECT_FALSE(authentication::VerifyPassword(input, "pbkdf2-unknown"));
    EXPECT_FALSE(authentication::VerifyPassword("", ""));
    EXPECT_TRUE(authentication::VerifyPassword(input, input));
    EXPECT_FALSE(authentication::IsPasswordHash(input));
}

