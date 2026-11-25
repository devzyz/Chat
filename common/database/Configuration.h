#pragma once
#include <boost/property_tree/ptree.hpp>
#include <charconv>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>

namespace database {
// 三个服务共用一个驱动选择及凭据来源，兼容既有单独的 Mysql 配置节。
inline std::map<std::string, std::string> Configuration(const boost::property_tree::ptree& tree) {
    const auto modern = tree.get_child_optional("Database");
    const auto legacy = tree.get_child_optional("Mysql");
    if (modern && legacy) throw std::invalid_argument("ConflictingDatabaseSections");
    if (!modern && !legacy) throw std::invalid_argument("MissingDatabaseSection");
    const auto& section = modern ? *modern : *legacy;
    const auto driver = modern ? section.get<std::string>("Driver", "postgresql") : "mysql";
    if (driver != "mysql" && driver != "postgresql") throw std::invalid_argument("InvalidDatabaseDriver");
    std::map<std::string, std::string> values{{"Driver", driver}};
    for (const auto* key : {"Host", "Port", "Schema", "User"}) {
        values[key] = section.get<std::string>(key, "");
        if (values[key].empty()) throw std::invalid_argument(std::string("MissingDatabase") + key);
    }
    const auto& port = values.at("Port");
    unsigned number = 0;
    const auto parsed = std::from_chars(port.data(), port.data() + port.size(), number);
    if (parsed.ec != std::errc() || parsed.ptr != port.data() + port.size() || number < 1 || number > 65535) {
        throw std::invalid_argument("InvalidDatabasePort");
    }
    if (modern) {
        const char* password = std::getenv("CHAT_DB_PASSWORD");
        if (!password || section.count("Password")) throw std::invalid_argument("InvalidDatabasePasswordSource");
        values["Password"] = password;
    } else {
        values["Password"] = section.get<std::string>("Password", "");
    }
    return values;
}
} // namespace database
