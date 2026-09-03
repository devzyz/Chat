#include "StatusRoutingInternal.h"

#include "const.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <utility>

namespace status_routing_internal {
namespace {

class StatusRoutingImpl final : public StatusRouting {
public:
	StatusRoutingImpl(
		std::vector<RoutingServer> servers,
		std::shared_ptr<StatusStore> store,
		std::shared_ptr<TokenSource> token_source)
		: servers_(std::move(servers)),
		  store_(std::move(store)),
		  token_source_(std::move(token_source)) {
	}

	AssignmentResult Assign(int uid) override {
		try {
			const RoutingServer* selected = nullptr;
			std::optional<unsigned long long> selected_count;
			for (const auto& server : servers_) {
				const auto raw_count = store_->ReadCount(server.name);
				std::optional<unsigned long long> count;
				if (raw_count && !raw_count->empty()) {
					unsigned long long parsed = 0;
					const auto parsed_result = std::from_chars(
						raw_count->data(), raw_count->data() + raw_count->size(), parsed);
					if (parsed_result.ec == std::errc{} && parsed_result.ptr == raw_count->data() + raw_count->size()) {
						count = parsed;
					}
				}
				const bool better = selected == nullptr ||
					(count.has_value() && !selected_count.has_value()) ||
					(count.has_value() == selected_count.has_value() &&
						((count.has_value() && *count < *selected_count) ||
						 (count == selected_count && server.name < selected->name)));
				if (better) {
					selected = &server;
					selected_count = count;
				}
			}
			if (selected == nullptr) {
				return {};
			}
			const auto token = token_source_->Next();
			if (token.empty()) {
				return {};
			}
			if (!store_->PutToken(uid, token)) {
				return {};
			}
			return {ErrorCodes::Success, selected->host, selected->port, token};
		}
		catch (...) {
			return {};
		}
	}

	LoginResult Validate(int uid, const std::string& token) override {
		try {
			const auto stored_token = store_->GetToken(uid);
			if (!stored_token.has_value()) {
				return {ErrorCodes::UidInvalid, 0, {}};
			}
			if (*stored_token != token) {
				return {ErrorCodes::TokenInvalid, 0, {}};
			}
			return {ErrorCodes::Success, uid, token};
		}
		catch (...) {
			return {};
		}
	}

private:
	std::vector<RoutingServer> servers_;
	std::shared_ptr<StatusStore> store_;
	std::shared_ptr<TokenSource> token_source_;
};

} // namespace

std::unique_ptr<StatusRouting> CreateStatusRouting(
	std::vector<RoutingServer> servers,
	std::shared_ptr<StatusStore> store,
	std::shared_ptr<TokenSource> token_source) {
	return std::make_unique<StatusRoutingImpl>(
		std::move(servers), std::move(store), std::move(token_source));
}

} // namespace status_routing_internal
