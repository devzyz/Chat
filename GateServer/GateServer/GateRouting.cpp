#include "GateRouting.h"
#include <charconv>
#include <optional>
#include <utility>

namespace gate_routing {
namespace {

/** @brief 持有不可变候选表，通过注入端口同步完成确定性选服和 Token 签发。 */
class GateRoutingImpl final : public GateRouting {
public:
    /** @brief 接管候选表并共享持有有效的存储和 Token 源。 */
    GateRoutingImpl(
        std::vector<RoutingServer> servers,
        std::shared_ptr<AssignmentStore> store,
        std::shared_ptr<TokenSource> token_source)
        : servers_(std::move(servers)), store_(std::move(store)), token_source_(std::move(token_source)) {}

    /** @brief 按有效计数和名称选服，Token 持久化成功才返回地址；所有异常返回空失败结果。 */
    gate::internal::ChatAssignment Assign(int uid, long long auth_version) override {
        try {
            if (uid <= 0 || auth_version < 0)
                return {};
            const RoutingServer* selected = nullptr;
            unsigned long long minimum = 0;
            for (const auto& server : servers_) {
                const auto raw = store_->ReadCount(server.name);
                if (!raw || raw->empty())
                    continue;
                unsigned long long count = 0;
                const auto end = raw->data() + raw->size();
                const auto parsed = std::from_chars(raw->data(), end, count);
                if (parsed.ec != std::errc{} || parsed.ptr != end)
                    continue;
                if (!selected || count < minimum || (count == minimum && server.name < selected->name)) {
                    selected = &server;
                    minimum = count;
                }
            }
            if (selected == nullptr) {
                return {};
            }
            const auto token = token_source_->Next();
            if (token.empty()) {
                return {};
            }
            if (!store_->PutToken(uid, token, auth_version)) {
                return {};
            }
            return {0, token, selected->host, selected->port};
        } catch (...) {
            return {};
        }
    }

private:
    std::vector<RoutingServer> servers_;
    std::shared_ptr<AssignmentStore> store_;
    std::shared_ptr<TokenSource> token_source_;
};

} // namespace

std::unique_ptr<GateRouting> CreateGateRouting(
    std::vector<RoutingServer> servers,
    std::shared_ptr<AssignmentStore> store,
    std::shared_ptr<TokenSource> token_source) {
    return std::make_unique<GateRoutingImpl>(std::move(servers), std::move(store), std::move(token_source));
}

} // namespace gate_routing
