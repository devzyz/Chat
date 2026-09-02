#include "StatusServiceImpl.h"

#include "ConfigMgr.h"
#include "StatusRoutingProduction.h"
#include "const.h"

#include <boost/algorithm/string/trim.hpp>

#include <sstream>
#include <utility>
#include <vector>

StatusServiceImpl::StatusServiceImpl() {
	auto& config = ConfigMgr::GetInstance();
	std::stringstream names(config["ChatServers"]["Name"]);
	std::string section_name;
	std::vector<RoutingServer> servers;
	while (std::getline(names, section_name, ',')) {
		boost::algorithm::trim(section_name);
		auto section = config[section_name];
		servers.push_back({section["Name"], section["Host"], section["Port"]});
	}
	routing_ = CreateProductionStatusRouting(std::move(servers));
}

Status StatusServiceImpl::GetChatServer(
	ServerContext*, const GetChatServerReq* request, GetChatServerRsp* reply) {
	SPDLOG_DEBUG("chat server selection request received, uid={}", request->uid());
	const auto result = routing_->Assign(request->uid());
	reply->set_error(result.error);
	reply->set_host(result.host);
	reply->set_port(result.port);
	reply->set_token(result.token);
	return Status::OK;
}

Status StatusServiceImpl::Login(
	ServerContext*, const LoginReq* request, LoginRsp* response) {
	const auto result = routing_->Validate(request->uid(), request->token());
	response->set_error(result.error);
	response->set_uid(result.uid);
	response->set_token(result.token);
	return Status::OK;
}
