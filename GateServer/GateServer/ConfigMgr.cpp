#include "ConfigMgr.h"
#include "LogMgr.h"
#include "../../common/grpc/GrpcClientRuntime.h"
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <cstdlib>
#include <stdexcept>

namespace {
void ValidatePort(const std::string& value, const std::string& key) {
	std::size_t parsed = 0;
	int port = 0;
	try {
		port = std::stoi(value, &parsed);
	}
	catch (const std::exception&) {
		throw std::invalid_argument(key + " must be a number between 1 and 65535");
	}
	if (parsed != value.size() || port < 1 || port > 65535) {
		throw std::invalid_argument(key + " must be a number between 1 and 65535");
	}
}

void RequireValue(SectionInfo section, const char* section_name, const char* key) {
	if (section[key].empty()) {
		throw std::invalid_argument(std::string("[") + section_name + "]." + key + " must not be empty");
	}
}

void ValidateEndpoint(SectionInfo section, const char* section_name) {
	RequireValue(section, section_name, "Host");
	ValidatePort(section["Port"], std::string("[") + section_name + "].Port");
}

void ValidatePositiveInteger(SectionInfo section, const char* section_name, const char* key) {
	RequireValue(section, section_name, key);
	std::size_t parsed = 0;
	unsigned long long value = 0;
	try {
		value = std::stoull(section[key], &parsed);
	}
	catch (const std::exception&) {
		throw std::invalid_argument(std::string("[") + section_name + "]." + key + " must be a positive integer");
	}
	if (parsed != section[key].size() || value == 0) {
		throw std::invalid_argument(std::string("[") + section_name + "]." + key + " must be a positive integer");
	}
}
}

std::string ConfigMgr::_config_path_override;

void ConfigMgr::SetConfigPath(const std::string& path) {
	_config_path_override = path;
}

SectionInfo::SectionInfo() {

}

SectionInfo::~SectionInfo() {

}

SectionInfo::SectionInfo(const SectionInfo& src) {
	_section_datas = src._section_datas;
}

SectionInfo& SectionInfo::operator = (const SectionInfo& src) {
	if (&src == this) return *this;
	this->_section_datas = src._section_datas;
	return *this;
}

// 为了通过对SectionInfo[key]直接取到key对应的value,而不是先取到SectionInfo._section_datas[key]取到value
std::string SectionInfo::operator[](const std::string& key) {
	if (_section_datas.find(key) == _section_datas.end()) return "";
	return _section_datas[key];
}

ConfigMgr::~ConfigMgr() {
	_config_map.clear();
}

SectionInfo ConfigMgr::operator[](const std::string& section) {
	if (_config_map.find(section) == _config_map.end()) {
		return SectionInfo();
	}
	return _config_map[section];
}

/**
 * 从config.ini文件内，读取对应的服务的配置信息
 */
ConfigMgr::ConfigMgr() {
	boost::filesystem::path current_path = boost::filesystem::current_path();
	const char* env_config = std::getenv("CHAT_CONFIG");
	boost::filesystem::path config_path = !_config_path_override.empty()
		? _config_path_override
		: (env_config ? env_config : (current_path / "config.ini").string());
	_config_path = config_path.string();

	boost::property_tree::ptree pt;
	boost::property_tree::read_ini(config_path.string(), pt);

	for (const auto& section_pair : pt) {
		const std::string& section_name = section_pair.first;
		const boost::property_tree::ptree& section_tree = section_pair.second;

		std::map<std::string, std::string> section_config;
		for (const auto& key_value_pair : section_tree) {
			const std::string& key = key_value_pair.first;
			// second仍然是ptree类型
			const std::string& value = key_value_pair.second.get_value<std::string>();
			section_config[key] = value;
		}

		SectionInfo sectionInfo;
		sectionInfo._section_datas = section_config;
		_config_map[section_name] = sectionInfo;
	}

	auto gate = (*this)["GateServer"];
	ValidatePort(gate["Port"], "[GateServer].Port");
	ValidateEndpoint((*this)["VarifyServer"], "VarifyServer");
	ValidateEndpoint((*this)["Redis"], "Redis");
	ValidateEndpoint((*this)["Mysql"], "Mysql");
	RequireValue((*this)["Mysql"], "Mysql", "User");
	RequireValue((*this)["Mysql"], "Mysql", "Schema");
	ValidateEndpoint((*this)["StatusServer"], "StatusServer");

	auto grpc = (*this)["Grpc"];
	(void)rpc::ParseDurationMs(
		grpc["PoolAcquireTimeoutMs"], "[Grpc].PoolAcquireTimeoutMs", std::chrono::milliseconds(1000));
	(void)rpc::ParseDurationMs(
		grpc["StatusDeadlineMs"], "[Grpc].StatusDeadlineMs", std::chrono::milliseconds(3000));
	(void)rpc::ParseDurationMs(
		grpc["VarifyDeadlineMs"], "[Grpc].VarifyDeadlineMs", std::chrono::milliseconds(15000));

	auto log = (*this)["Log"];
	RequireValue(log, "Log", "Name");
	RequireValue(log, "Log", "LogDir");
	ValidatePositiveInteger(log, "Log", "MaxSizeMB");
	ValidatePositiveInteger(log, "Log", "MaxTotalFiles");
}

ConfigMgr::ConfigMgr(const ConfigMgr& src) {
	_config_map = src._config_map;
	_config_path = src._config_path;
}
ConfigMgr& ConfigMgr::operator = (const ConfigMgr& src) {
	if (&src == this) return *this;
	_config_map = src._config_map;
	_config_path = src._config_path;
	return *this;
}

void ConfigMgr::DumpLoadedConfig() const {
	SPDLOG_DEBUG("config path: {}", _config_path);
	for (const auto& section_pair : _config_map) {
		SPDLOG_DEBUG("load config section [{}]", section_pair.first);
		for (const auto& key_value_pair : section_pair.second._section_datas) {
			SPDLOG_DEBUG("config [{}].{} = xxx", section_pair.first, key_value_pair.first);
		}
	}
}
