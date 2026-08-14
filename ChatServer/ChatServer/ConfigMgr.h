#pragma once
#include "Singleton.h"
#include <map>
#include <string>

struct SectionInfo {
public:
	SectionInfo();
	~SectionInfo();
	SectionInfo(const SectionInfo& src);
	SectionInfo& operator = (const SectionInfo& src);
	std::string operator [] (const std::string key);

	std::map<std::string, std::string> _sectionInfo_data;
};

/**
 * @brief 
 * 配置文件管理类，读取config.ini内的配置信息
 */
class ConfigMgr
{
	friend class Singleton<ConfigMgr>;
public:
	ConfigMgr();
	~ConfigMgr();
	ConfigMgr(const ConfigMgr& src);
	ConfigMgr& operator = (const ConfigMgr& src);
	/**
	 * @brief 
	 * @return 
	 * 通过静态变量获取静态实例，作为单例实例使用
	 */
	static ConfigMgr& GetInstance();
	static void SetConfigPath(const std::string& path);
	SectionInfo operator [] (const std::string& key);
	void DumpLoadedConfig() const;
private:
	std::map<std::string, SectionInfo> _config_data;
	std::string _config_path;
	static std::string _config_path_override;
};
