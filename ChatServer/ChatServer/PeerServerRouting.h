#pragma once

#include <functional>
#include <string>
#include <unordered_map>

using PeerConfigLookup = std::function<std::string(const std::string&, const std::string&)>;

struct PeerServerEndpoint {
    std::string host;
    std::string port;
};

std::unordered_map<std::string, PeerServerEndpoint> ResolvePeerServerEndpoints(
    const std::string& configured_servers,
    const PeerConfigLookup& lookup);
