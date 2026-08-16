#include <gtest/gtest.h>

#include "ConfigMgr.h"

#include <Windows.h>
#include <boost/property_tree/ini_parser.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(const char* name, const std::string& value) : name_(name) {
        const char* previous = std::getenv(name);
        if (previous != nullptr) {
            previous_ = previous;
            had_previous_ = true;
        }
        _putenv_s(name_.c_str(), value.c_str());
    }

    ~ScopedEnvironmentVariable() {
        _putenv_s(name_.c_str(), had_previous_ ? previous_.c_str() : "");
    }

private:
    std::string name_;
    std::string previous_;
    bool had_previous_ = false;
};

class ConfigFile {
public:
    explicit ConfigFile(const std::string& contents) {
        static unsigned long sequence = 0;
        path_ = std::filesystem::temp_directory_path() /
            ("chat-config-test-" + std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(++sequence) + ".ini");
        std::ofstream output(path_, std::ios::binary);
        output << contents;
        output.close();
    }

    ~ConfigFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::string ValidConfig(const std::string& peer_servers = "") {
    return
        "[Redis]\n"
        "Host=127.0.0.1\n"
        "Port=6379\n"
        "[Mysql]\n"
        "Host=127.0.0.1\n"
        "Port=3306\n"
        "User=test-user\n"
        "Schema=test-schema\n"
        "[StatusServer]\n"
        "Host=127.0.0.1\n"
        "Port=50052\n"
        "[SelfServer]\n"
        "Name=ChatTest\n"
        "Host=127.0.0.1\n"
        "Port=18090\n"
        "RPCPort=15055\n"
        "[PeerServer]\n"
        "Servers=" + peer_servers + "\n"
        "[Log]\n"
        "Name=ChatTest\n"
        "LogDir=Log\n";
}

std::string ReplaceOnce(std::string value, const std::string& from, const std::string& to) {
    const auto position = value.find(from);
    if (position == std::string::npos) {
        throw std::logic_error("test fixture did not contain expected text: " + from);
    }
    value.replace(position, from.size(), to);
    return value;
}

void ExpectInvalidConfig(const std::string& contents, const std::string& message_fragment) {
    ConfigFile file(contents);
    ConfigMgr::SetConfigPath(file.path().string());
    try {
        ConfigMgr config;
        FAIL() << "Expected invalid configuration to throw";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find(message_fragment), std::string::npos)
            << "Actual error: " << error.what();
    }
}

TEST(ConfigMgrTests, ExplicitPathOverridesChatConfigEnvironmentVariable) {
    ConfigFile selected(ValidConfig());
    ScopedEnvironmentVariable environment("CHAT_CONFIG", "missing-environment-config.ini");

    ConfigMgr::SetConfigPath(selected.path().string());
    ConfigMgr config;

    EXPECT_EQ(config["SelfServer"]["Name"], "ChatTest");
    EXPECT_EQ(config["SelfServer"]["Port"], "18090");
}

TEST(ConfigMgrTests, ShippedDualInstanceConfigurationsHaveDistinctIdentities) {
    const auto fixture_root = std::filesystem::current_path() / "fixtures";
    const auto first_path = fixture_root / "chat-01.ini";
    const auto second_path = fixture_root / "chat-02.ini";

    ConfigMgr::SetConfigPath(first_path.string());
    ConfigMgr first;
    ConfigMgr::SetConfigPath(second_path.string());
    ConfigMgr second;

    EXPECT_NE(first["SelfServer"]["Name"], second["SelfServer"]["Name"]);
    EXPECT_NE(first["SelfServer"]["Port"], second["SelfServer"]["Port"]);
    EXPECT_NE(first["SelfServer"]["RPCPort"], second["SelfServer"]["RPCPort"]);
    EXPECT_NE(first["Log"]["Name"], second["Log"]["Name"]);
}

TEST(ConfigMgrTests, EmptyPeerListAllowsSingleInstanceConfiguration) {
    ConfigFile file(ValidConfig());
    ConfigMgr::SetConfigPath(file.path().string());

    EXPECT_NO_THROW({ ConfigMgr config; });
}

TEST(ConfigMgrTests, MissingConfigFileIsRejected) {
    const auto missing = std::filesystem::temp_directory_path() / "chat-config-that-does-not-exist.ini";
    std::error_code error;
    std::filesystem::remove(missing, error);
    ConfigMgr::SetConfigPath(missing.string());

    EXPECT_THROW({ ConfigMgr config; }, boost::property_tree::ini_parser_error);
}

TEST(ConfigMgrTests, MissingRequiredSectionIsRejected) {
    const auto invalid = ReplaceOnce(ValidConfig(), "[StatusServer]\nHost=127.0.0.1\nPort=50052\n", "");
    ExpectInvalidConfig(invalid, "[StatusServer].Host");
}

TEST(ConfigMgrTests, MissingRequiredKeyIsRejected) {
    const auto invalid = ReplaceOnce(ValidConfig(), "Name=ChatTest\n", "");
    ExpectInvalidConfig(invalid, "[SelfServer].Name");
}

class InvalidPortTest : public testing::TestWithParam<std::string> {};

TEST_P(InvalidPortTest, SelfServerTcpPortIsRejectedOutsideCanonicalRange) {
    const auto invalid = ReplaceOnce(ValidConfig(), "Port=18090\n", "Port=" + GetParam() + "\n");
    ExpectInvalidConfig(invalid, "[SelfServer].Port must be a number between 1 and 65535");
}

INSTANTIATE_TEST_SUITE_P(InvalidPorts, InvalidPortTest,
    testing::Values("", "not-a-number", "-1", "0", "65536", "8090suffix"));

TEST(ConfigMgrTests, EqualTcpAndRpcPortsAreRejected) {
    const auto invalid = ReplaceOnce(ValidConfig(), "RPCPort=15055", "RPCPort=18090");
    ExpectInvalidConfig(invalid, "must be different");
}

TEST(ConfigMgrTests, ReferencedPeerSectionMustExist) {
    ExpectInvalidConfig(ValidConfig("MissingPeer"), "[MissingPeer].Name");
}

TEST(ConfigMgrTests, SelfReferencePeerIsRejected) {
    auto invalid = ValidConfig("SelfPeer");
    invalid += "[SelfPeer]\nName=ChatTest\nHost=127.0.0.1\nPort=15056\n";
    ExpectInvalidConfig(invalid, "different from [SelfServer].Name");
}

TEST(ConfigMgrTests, DuplicatePeerNamesAreRejectedAcrossSections) {
    auto invalid = ValidConfig("PeerOne,PeerTwo");
    invalid +=
        "[PeerOne]\nName=SamePeer\nHost=127.0.0.1\nPort=15056\n"
        "[PeerTwo]\nName=SamePeer\nHost=127.0.0.1\nPort=15057\n";
    ExpectInvalidConfig(invalid, "duplicate peer name SamePeer");
}

TEST(ConfigMgrTests, EmptyPeerEntryIsRejected) {
    auto invalid = ValidConfig("PeerOne,,PeerTwo");
    invalid +=
        "[PeerOne]\nName=PeerOne\nHost=127.0.0.1\nPort=15056\n"
        "[PeerTwo]\nName=PeerTwo\nHost=127.0.0.1\nPort=15057\n";
    ExpectInvalidConfig(invalid, "empty peer name");
}

} // namespace
