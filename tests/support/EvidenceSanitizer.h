#pragma once

#include <regex>
#include <string>

namespace integration {

/** 替换证据中凭据形状赋值的值，保留键名及其他诊断文本。 */
inline std::string SanitizeEvidence(std::string evidence) {
	static const std::regex secret_assignment(
		R"((password|token|code|email)\s*[:=]\s*[^\s\r\n]+)",
		std::regex::icase);
	return std::regex_replace(evidence, secret_assignment, "$1=[REDACTED]");
}

} // namespace integration
