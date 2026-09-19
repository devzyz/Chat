#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace resource {
struct Error : std::runtime_error {
    Error(int status, const std::string& message) : std::runtime_error(message), status(status) {}
    int status;
};
struct Metadata {
    std::string id;
    int owner = 0;
    std::string name;
    std::string media_type;
    std::uint64_t size = 0;
    std::uint64_t offset = 0;
    bool ready = false;
    std::string sha256;
};
// All calls run on the single storage executor. No file operation holds a network lock.
class ResourceStore {
public:
    ResourceStore(std::filesystem::path root, std::uint64_t max_size);
    Metadata Create(int owner, const std::string& name, const std::string& type,
                    std::uint64_t size, const std::string& sha256);
    Metadata Inspect(const std::string& id) const;
    Metadata Owned(const std::string& id, int owner) const;
    Metadata Append(const std::string& id, int owner, std::uint64_t offset,
                    const char* data, std::size_t size);
    Metadata Complete(const std::string& id, int owner);
    std::vector<char> Read(const std::string& id, std::uint64_t offset, std::size_t limit) const;
    static std::string Digest(const std::filesystem::path& path);
    static constexpr std::size_t BUFFER_SIZE = 64 * 1024;
private:
    std::filesystem::path Path(const std::string& id, const char* suffix) const;
    std::filesystem::path _root;
    std::uint64_t _max_size;
};
}
