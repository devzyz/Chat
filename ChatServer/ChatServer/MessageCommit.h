#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace message_commit {

using Deadline = std::chrono::steady_clock::time_point;
using Batch = std::vector<std::pair<std::string, std::string>>;

struct AuthenticatedPrincipal { int uid = 0; };

enum class Error {
    NONE, UNAUTHORIZED_SENDER, INVALID_UUID, INVALID_MEMBERSHIP,
    CONFLICT, STORAGE_UNAVAILABLE, DEADLINE_EXCEEDED
};
enum class Disposition { CREATED, EXISTING };

struct Item {
    int message_id = 0;
    std::string client_msg_uuid;
    Disposition disposition = Disposition::CREATED;
    std::int64_t created_at = 0;
};
struct Result {
    Error error = Error::NONE;
    std::vector<Item> items;
    bool IsSuccess() const { return error == Error::NONE; }
};

class Store {
public:
    virtual ~Store() = default;
    virtual Result Commit(int sender, int recipient, int chat, const Batch& batch, Deadline deadline) = 0;
};

bool IsCanonicalUuid(const std::string& value);
Result Commit(Store& store, AuthenticatedPrincipal principal, int claimed_sender,
    int recipient, int chat, const Batch& batch, Deadline deadline);

}
