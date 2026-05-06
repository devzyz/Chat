#include "ConfigMgr.h"
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

ConfigMgr::ConfigMgr() {
	boost::filesystem::path current_path = boost::filesystem::current_path();
	boost::filesystem::path config_path = current_path / "config.ini";
	std::cout << "config path : " << config_path << std::endl;

	boost::property_tree::ptree pt;
	boost::property_tree::read_ini(config_path.string(), pt);

	for (const auto& section_pair : pt) {
		const std::string sectionInfo_name = section_pair.first;
		const boost::property_tree::ptree sectionInfo_pt = section_pair.second;

		std::cout << "[" << sectionInfo_name << "]" << std::endl;

		std::map<std::string, std::string> section_config;
		for (const auto& key_value_pair : sectionInfo_pt) {
			const std::string key = key_value_pair.first;
			const std::string value = key_value_pair.second.get_value<std::string>();

			std::cout << key << " = " << value << std::endl;

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
}

ConfigMgr& ConfigMgr::operator = (const ConfigMgr& src) {
	if (this == &src) {
		return *this;
	}
	_config_data = src._config_data;
	return *this;
}

ConfigMgr& ConfigMgr::GetInstance() {
	static ConfigMgr configMgr;
	return configMgr;
}

SectionInfo ConfigMgr::operator [] (const std::string& key) {
	if (_config_data.find(key) == _config_data.end()) {
		return SectionInfo();
	}
	return _config_data[key];
}
