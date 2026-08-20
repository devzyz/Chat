#include "PeerServerRouting.h"

#include <sstream>

std::unordered_map<std::string, PeerServerEndpoint> ResolvePeerServerEndpoints(
    const std::string& configured_servers,
    const PeerConfigLookup& lookup) {
    std::unordered_map<std::string, PeerServerEndpoint> endpoints;
    std::stringstream stream(configured_servers);
    std::string section;
    while (std::getline(stream, section, ',')) {
        const auto name = lookup(section, "Name");
        if (name.empty()) {
            continue;
        }
        endpoints[name] = {lookup(section, "Host"), lookup(section, "Port")};
    }
    return endpoints;
}
