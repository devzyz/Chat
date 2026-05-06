#pragma once
#include "const.h"

struct SectionInfo {
	SectionInfo();
	~SectionInfo();

	std::map<std::string, std::string> _section_datas;

	SectionInfo(const SectionInfo& src);
	SectionInfo& operator = (const SectionInfo& src);

	std::string operator [](const std::string& key);
};

class ConfigMgr
{
public:
	~ConfigMgr();

	SectionInfo operator[](const std::string& section);
	ConfigMgr(const ConfigMgr& section);
	ConfigMgr& operator = (const ConfigMgr& section);

	static ConfigMgr& GetInstance();
private:
	ConfigMgr();
	std::map<std::string, SectionInfo> _config_map;
};

