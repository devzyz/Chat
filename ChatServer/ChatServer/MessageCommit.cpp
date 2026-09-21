#include "MessageCommit.h"

#include <unordered_set>

namespace message_commit {

bool IsCanonicalUuid(const std::string& value) {
    if (value.size() != 36) { return false; }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (character != '-') { return false; }
        } else if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return value != "00000000-0000-0000-0000-000000000000";
}

Result Commit(Store& store, AuthenticatedPrincipal principal, int claimed_sender,
    int recipient, int chat, const Batch& batch, Deadline deadline) {
    if (principal.uid <= 0 || principal.uid != claimed_sender) { return {Error::UNAUTHORIZED_SENDER, {}}; }
    if (recipient <= 0 || chat <= 0 || recipient == principal.uid) { return {Error::INVALID_MEMBERSHIP, {}}; }
    if (batch.empty() || batch.size() > 100) { return {Error::INVALID_UUID, {}}; }
    std::unordered_set<std::string> identities;
    for (const auto& message : batch) {
        if (!IsCanonicalUuid(message.first) || !identities.insert(message.first).second) {
            return {Error::INVALID_UUID, {}};
        }
    }
    if (std::chrono::steady_clock::now() >= deadline) { return {Error::DEADLINE_EXCEEDED, {}}; }
    return store.Commit(principal.uid, recipient, chat, batch, deadline);
}

}
