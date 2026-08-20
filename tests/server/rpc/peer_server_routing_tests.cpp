#include <gtest/gtest.h>

#include "PeerServerRouting.h"

#include <map>
#include <string>

namespace {

// T05-ROUTE-01
TEST(PeerServerRoutingTests, ConfiguredSectionsResolveToRuntimeNamesAndAddresses) {
    const std::map<std::string, std::map<std::string, std::string>> config = {
        {"PeerA", {{"Name", "chat-alpha"}, {"Host", "127.0.0.1"}, {"Port", "51001"}}},
        {"PeerB", {{"Name", "chat-beta"}, {"Host", "localhost"}, {"Port", "51002"}}}
    };
    const auto lookup = [&config](const std::string& section, const std::string& key) {
        const auto section_iter = config.find(section);
        if (section_iter == config.end()) {
            return std::string();
        }
        const auto key_iter = section_iter->second.find(key);
        return key_iter == section_iter->second.end() ? std::string() : key_iter->second;
    };

    const auto endpoints = ResolvePeerServerEndpoints("PeerA,PeerB", lookup);

    ASSERT_EQ(endpoints.size(), 2U);
    EXPECT_EQ(endpoints.at("chat-alpha").host, "127.0.0.1");
    EXPECT_EQ(endpoints.at("chat-alpha").port, "51001");
    EXPECT_EQ(endpoints.at("chat-beta").host, "localhost");
    EXPECT_EQ(endpoints.at("chat-beta").port, "51002");
}

// T05-ROUTE-02
TEST(PeerServerRoutingTests, MissingRuntimeNameDoesNotCreateARoute) {
    const auto lookup = [](const std::string& section, const std::string& key) {
        if (section == "Known" && key == "Name") {
            return std::string("chat-known");
        }
        if (section == "Known" && key == "Host") {
            return std::string("127.0.0.1");
        }
        if (section == "Known" && key == "Port") {
            return std::string("52001");
        }
        return std::string();
    };

    const auto endpoints = ResolvePeerServerEndpoints("Unknown,Known", lookup);

    ASSERT_EQ(endpoints.size(), 1U);
    EXPECT_EQ(endpoints.at("chat-known").host, "127.0.0.1");
    EXPECT_EQ(endpoints.at("chat-known").port, "52001");
}

} // namespace
