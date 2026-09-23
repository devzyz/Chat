#pragma once
#include "const.h"

/** @brief 保存一个配置节的键值，缺失查询返回空值供上层校验。 */
struct SectionInfo {
	/** @brief 创建键值集合为空的配置节。 */
	SectionInfo();
	/** @brief 销毁配置节及其键值容器。 */
	~SectionInfo();

	std::map<std::string, std::string> _section_datas;

	/** @brief 复制来源配置节的全部键值，副本独立持有容器。 */
	SectionInfo(const SectionInfo& src);

	/** @brief 复制来源键值并返回自身，自赋值保持不变。 */
	SectionInfo& operator = (const SectionInfo& src);
	
	// 为了通过对SectionInfo[key]直接取到key对应的value,而不是先取到SectionInfo._section_datas[key]取到value
	/** @brief 按给定配置节或键查询值，未找到时按配置容器约定返回空结果。 */
	std::string operator[](const std::string& key);
};

/** @brief 加载并校验服务配置，提供按节查询；配置路径须在首次获取单例之前指定。 */
class ConfigMgr
{
public:
	/** @brief 销毁已加载的配置副本和路径记录。 */
	~ConfigMgr();

	/** @brief 按给定配置节或键查询值，未找到时按配置容器约定返回空结果。 */
	SectionInfo operator[](const std::string& section);
	
	/** @brief 返回以静态状态持有的共享实例；首次构造的配置和依赖由调用前初始化决定。 */
	static ConfigMgr& GetInstance() {
		static ConfigMgr configMgr;
		return configMgr;
	}
	/** @brief 保存显式配置路径，必须在配置单例首次加载前调用。 */
	static void SetConfigPath(const std::string& path);

	/** @brief 复制已加载的配置与路径，不重新读取或校验配置文件。 */
	ConfigMgr(const ConfigMgr& src);
	/** @brief 复制来源配置与路径并返回自身，不重新读取文件。 */
	ConfigMgr& operator = (const ConfigMgr& src);
	/** @brief 按加载结果输出配置诊断；凭据字段按实现的过滤规则隐藏。 */
	void DumpLoadedConfig() const;

private:
	/** @brief 从指定路径加载配置并验证必需项，配置错误阻止服务初始化。 */
	ConfigMgr();
	static std::string _config_path_override;
	std::map<std::string, SectionInfo> _config_map;
	std::string _config_path;
};
