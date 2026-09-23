#pragma once

#include <functional>
#include <string>
#include <unordered_map>

using PeerConfigLookup = std::function<std::string(const std::string&, const std::string&)>;

/** @brief 保存一个对端 ChatServer 的名称和 RPC 地址。 */
struct PeerServerEndpoint {
    std::string host;
    std::string port;
};

/** @brief 解析配置中的对端名称与 RPC 地址，拒绝缺失或冲突的端点配置。 */
std::unordered_map<std::string, PeerServerEndpoint> ResolvePeerServerEndpoints(
    const std::string& configured_servers,
    const PeerConfigLookup& lookup);
