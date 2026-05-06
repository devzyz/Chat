#pragma once
#include "Singleton.h"
#include <map>

struct SectionInfo {
public:
	SectionInfo();
	~SectionInfo();
	SectionInfo(const SectionInfo& src);
	SectionInfo& operator = (const SectionInfo& src);
	std::string operator [] (const std::string key);

	std::map<std::string, std::string> _sectionInfo_data;
};

class ConfigMgr
{
	friend class Singleton<ConfigMgr>;
public:
	ConfigMgr();
	~ConfigMgr();
	ConfigMgr(const ConfigMgr& src);
	ConfigMgr& operator = (const ConfigMgr& src);
	static ConfigMgr& GetInstance();
	SectionInfo operator [] (const std::string& key);
private:
	std::map<std::string, SectionInfo> _config_data;
};

