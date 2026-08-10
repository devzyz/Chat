#pragma once
#include "const.h"

struct SectionInfo {
	SectionInfo();
	~SectionInfo();

	std::map<std::string, std::string> _section_datas;

	SectionInfo(const SectionInfo& src);

	SectionInfo& operator = (const SectionInfo& src);
	
	// 为了通过对SectionInfo[key]直接取到key对应的value,而不是先取到SectionInfo._section_datas[key]取到value
	std::string operator[](const std::string& key);
};

class ConfigMgr
{
public:
	~ConfigMgr();

	SectionInfo operator[](const std::string& section);
	
	static ConfigMgr& GetInstance() {
		static ConfigMgr configMgr;
		return configMgr;
	}
	static void SetConfigPath(const std::string& path);

	ConfigMgr(const ConfigMgr& src);
	ConfigMgr& operator = (const ConfigMgr& src);
	void DumpLoadedConfig() const;

private:
	ConfigMgr();
	static std::string _config_path_override;
	std::map<std::string, SectionInfo> _config_map;
	std::string _config_path;
};
