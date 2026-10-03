#pragma once
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <array>
#include <stdexcept>
#include <string>

namespace authentication {
inline constexpr int PASSWORD_ITERATIONS = 600000;
inline constexpr const char* PASSWORD_PREFIX = "pbkdf2-sha256$600000$";
/** @brief 将二进制编码为固定小写十六进制，不输出到日志。 */
inline std::string Hex(const unsigned char* bytes, std::size_t count) {
    constexpr char DIGITS[] = "0123456789abcdef";
    std::string result;
    result.reserve(count * 2);
    for (std::size_t index = 0; index < count; ++index) {
        result += DIGITS[bytes[index] >> 4]; result += DIGITS[bytes[index] & 15];
    }
    return result;
}
/** @brief 使用 OpenSSL PBKDF2-HMAC-SHA256 派生固定长度校验值；拒绝无效输入和库错误。 */
inline std::string Derive(const std::string& input, const std::string& salt) {
    if (input.empty() || input.size() > 255 || salt.size() != 32) throw std::invalid_argument("invalid credential shape");
    std::array<unsigned char, 32> derived{};
    if (PKCS5_PBKDF2_HMAC(input.data(), static_cast<int>(input.size()),
        reinterpret_cast<const unsigned char*>(salt.data()), static_cast<int>(salt.size()),
        PASSWORD_ITERATIONS, EVP_sha256(), static_cast<int>(derived.size()), derived.data()) != 1)
        throw std::runtime_error("credential derivation failed");
    return Hex(derived.data(), derived.size());
}
/** @brief 生成独立随机盐并保存带算法版本的口令校验值；兼容现有不透明口令传输值。 */
inline std::string HashPassword(const std::string& input) {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) throw std::runtime_error("salt generation failed");
    const auto salt = Hex(bytes.data(), bytes.size());
    return std::string(PASSWORD_PREFIX) + salt + "$" + Derive(input, salt);
}
/** @brief 判断是否为本版本口令记录；旧记录只用于认证成功后的条件升级。 */
inline bool IsPasswordHash(const std::string& stored) { return stored.rfind(PASSWORD_PREFIX, 0) == 0; }
/** @brief 以恒定时间比较校验值，损坏版本记录拒绝；旧记录成功后必须由调用者升级。 */
inline bool VerifyPassword(const std::string& input, const std::string& stored) {
    if (input.empty() || input.size() > 255) return false;
    if (!IsPasswordHash(stored))
        return stored.rfind("pbkdf2-", 0) != 0 && stored.size() == input.size()
            && CRYPTO_memcmp(stored.data(), input.data(), input.size()) == 0;
    const auto offset = std::string(PASSWORD_PREFIX).size();
    if (stored.size() != offset + 97 || stored[offset + 32] != '$'
        || stored.find_first_not_of("0123456789abcdef", offset) != offset + 32
        || stored.find_first_not_of("0123456789abcdef", offset + 33) != std::string::npos) return false;
    const auto derived = Derive(input, stored.substr(offset, 32));
    return CRYPTO_memcmp(derived.data(), stored.data() + offset + 33, derived.size()) == 0;
}
}
