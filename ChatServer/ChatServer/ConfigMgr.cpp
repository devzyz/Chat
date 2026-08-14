#include "ConfigMgr.h"
#include "LogMgr.h"
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <cstdlib>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
void ValidatePort(const std::string& value, const char* key) {
	std::size_t parsed = 0;
	int port = 0;
	try {
		port = std::stoi(value, &parsed);
	}
	catch (const std::exception&) {
		throw std::invalid_argument(std::string(key) + " must be a number between 1 and 65535");
	}
	if (parsed != value.size() || port < 1 || port > 65535) {
		throw std::invalid_argument(std::string(key) + " must be a number between 1 and 65535");
	}
}

void RequireValue(const SectionInfo& section, const char* section_name, const char* key) {
	auto copy = section;
	if (copy[key].empty()) {
		throw std::invalid_argument(std::string("[") + section_name + "]." + key + " must not be empty");
	}
}

void ValidateEndpoint(const SectionInfo& section, const char* section_name) {
	auto copy = section;
	RequireValue(copy, section_name, "Host");
	ValidatePort(copy["Port"], (std::string("[") + section_name + "].Port").c_str());
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
	_sectionInfo_data = src._sectionInfo_data;
}

SectionInfo& SectionInfo::operator = (const SectionInfo& src) {
	if (this == &src) {
		return *this;
	}
	_sectionInfo_data = src._sectionInfo_data;
	return *this;
}

std::string SectionInfo::operator [] (const std::string key) {
	if (_sectionInfo_data.find(key) == _sectionInfo_data.end()) {
		return "";
	}
	return _sectionInfo_data[key];
}

/**
 * @brief 
 * 从config.ini中读取到配置信息
 */
ConfigMgr::ConfigMgr() {
	boost::filesystem::path current_path = boost::filesystem::current_path();
	const char* env_config = std::getenv("CHAT_CONFIG");
	boost::filesystem::path config_path = !_config_path_override.empty()
		? _config_path_override
		: (env_config ? env_config : (current_path / "config.ini").string());
	_config_path = config_path.string();
	SPDLOG_DEBUG("config path: {}", _config_path);

	boost::property_tree::ptree pt;
	boost::property_tree::read_ini(config_path.string(), pt);

	for (const auto& section_pair : pt) {
		const std::string sectionInfo_name = section_pair.first;
		const boost::property_tree::ptree sectionInfo_pt = section_pair.second;

		SPDLOG_DEBUG("load config section [{}]", sectionInfo_name);

		std::map<std::string, std::string> section_config;
		for (const auto& key_value_pair : sectionInfo_pt) {
			const std::string key = key_value_pair.first;
			const std::string value = key_value_pair.second.get_value<std::string>();

			SPDLOG_DEBUG("config [{}].{} = xxx", sectionInfo_name, key);

			section_config[key] = value;
		}

		SectionInfo sectionInfo;
		sectionInfo._sectionInfo_data = section_config;
		_config_data[sectionInfo_name] = sectionInfo;
	}

	auto self_server = (*this)["SelfServer"];
	RequireValue(self_server, "SelfServer", "Name");
	RequireValue(self_server, "SelfServer", "Host");
	ValidatePort(self_server["Port"], "[SelfServer].Port");
	ValidatePort(self_server["RPCPort"], "[SelfServer].RPCPort");
	if (std::stoi(self_server["Port"]) == std::stoi(self_server["RPCPort"])) {
		throw std::invalid_argument("[SelfServer].Port and [SelfServer].RPCPort must be different");
	}

	ValidateEndpoint((*this)["Redis"], "Redis");
	ValidateEndpoint((*this)["Mysql"], "Mysql");
	RequireValue((*this)["Mysql"], "Mysql", "User");
	RequireValue((*this)["Mysql"], "Mysql", "Schema");
	ValidateEndpoint((*this)["StatusServer"], "StatusServer");

	auto log = (*this)["Log"];
	RequireValue(log, "Log", "Name");
	RequireValue(log, "Log", "LogDir");

	auto peer_list = (*this)["PeerServer"]["Servers"];
	if (!peer_list.empty()) {
		std::set<std::string> peer_names;
		std::stringstream stream(peer_list);
		std::string peer_section;
		while (std::getline(stream, peer_section, ',')) {
			boost::algorithm::trim(peer_section);
			if (peer_section.empty()) {
				throw std::invalid_argument("[PeerServer].Servers contains an empty peer name");
			}
			auto peer = (*this)[peer_section];
			RequireValue(peer, peer_section.c_str(), "Name");
			ValidateEndpoint(peer, peer_section.c_str());
			if (peer["Name"] == self_server["Name"]) {
				throw std::invalid_argument("peer server name must be different from [SelfServer].Name");
			}
			if (!peer_names.insert(peer["Name"]).second) {
				throw std::invalid_argument("[PeerServer].Servers contains duplicate peer name " + peer["Name"]);
			}
		}
	}
}

ConfigMgr::~ConfigMgr() {
	_config_data.clear();
}

ConfigMgr::ConfigMgr(const ConfigMgr& src) {
	_config_data = src._config_data;
	_config_path = src._config_path;
}

ConfigMgr& ConfigMgr::operator = (const ConfigMgr& src) {
	if (this == &src) {
		return *this;
	}
	_config_data = src._config_data;
	_config_path = src._config_path;
	return *this;
}

ConfigMgr& ConfigMgr::GetInstance() {
	static ConfigMgr configMgr;
	return configMgr;
}


void ConfigMgr::DumpLoadedConfig() const {
	SPDLOG_DEBUG("config path: {}", _config_path);
	for (const auto& section_pair : _config_data) {
		const auto& section_name = section_pair.first;
		const auto& section_info = section_pair.second;

		SPDLOG_DEBUG("load config section [{}]", section_name);
		for (const auto& key_value_pair : section_info._sectionInfo_data) {
			SPDLOG_DEBUG("config [{}].{} = xxx", section_name, key_value_pair.first);
		}
	}
}

SectionInfo ConfigMgr::operator [] (const std::string& key) {
	if (_config_data.find(key) == _config_data.end()) {
		return SectionInfo();
	}
	return _config_data[key];
}
