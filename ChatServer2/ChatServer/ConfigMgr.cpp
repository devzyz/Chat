#include "ConfigMgr.h"
#include "LogMgr.h"
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

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
	boost::filesystem::path config_path = current_path / "config.ini";
	_config_path = config_path.string();

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