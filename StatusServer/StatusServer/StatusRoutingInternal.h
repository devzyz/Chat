#pragma once

#include "StatusRouting.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct RoutingServer {
	std::string name;
	std::string host;
	std::string port;
};

namespace status_routing_internal {

class StatusStore {
public:
	virtual ~StatusStore() = default;
	virtual std::optional<std::string> ReadCount(const std::string& server_name) = 0;
	virtual bool PutToken(int uid, const std::string& token) = 0;
	virtual std::optional<std::string> GetToken(int uid) = 0;
};

class TokenSource {
public:
	virtual ~TokenSource() = default;
	virtual std::string Next() = 0;
};

std::unique_ptr<StatusRouting> CreateStatusRouting(
	std::vector<RoutingServer> servers,
	std::shared_ptr<StatusStore> store,
	std::shared_ptr<TokenSource> token_source);

} // namespace status_routing_internal
