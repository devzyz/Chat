#pragma once
#include "Singleton.h"
#include <map>
#include <string>

/** @brief 保存一个配置节的键值，缺失查询返回空值供上层校验。 */
struct SectionInfo {
public:
	/** @brief 创建键值集合为空的配置节。 */
	SectionInfo();
	/** @brief 销毁配置节及其键值容器。 */
	~SectionInfo();
	/** @brief 复制来源配置节的全部键值，副本独立持有容器。 */
	SectionInfo(const SectionInfo& src);
	/** @brief 复制来源键值并返回自身，自赋值保持不变。 */
	SectionInfo& operator = (const SectionInfo& src);
	/** @brief 按给定配置节或键查询值，未找到时按配置容器约定返回空结果。 */
	std::string operator [] (const std::string key);

	std::map<std::string, std::string> _sectionInfo_data;
};

/**
 *
 * 配置文件管理类，读取config.ini内的配置信息
 */
class ConfigMgr
{
	friend class Singleton<ConfigMgr>;
public:
	/** @brief 从指定路径加载配置并验证必需项，配置错误阻止服务初始化。 */
	ConfigMgr();
	/** @brief 销毁已加载的配置副本和路径记录。 */
	~ConfigMgr();
	/** @brief 复制已加载的配置与路径，不重新读取或校验配置文件。 */
	ConfigMgr(const ConfigMgr& src);
	/** @brief 复制来源配置与路径并返回自身，不重新读取文件。 */
	ConfigMgr& operator = (const ConfigMgr& src);
	/**
	 *
	 *
	 * 通过静态变量获取静态实例，作为单例实例使用
	 */
	static ConfigMgr& GetInstance();
	/** @brief 保存显式配置路径，必须在配置单例首次加载前调用。 */
	static void SetConfigPath(const std::string& path);
	/** @brief 按给定配置节或键查询值，未找到时按配置容器约定返回空结果。 */
	SectionInfo operator [] (const std::string& key);
	/** @brief 按加载结果输出配置诊断；凭据字段按实现的过滤规则隐藏。 */
	void DumpLoadedConfig() const;
private:
	std::map<std::string, SectionInfo> _config_data;
	std::string _config_path;
	static std::string _config_path_override;
};
