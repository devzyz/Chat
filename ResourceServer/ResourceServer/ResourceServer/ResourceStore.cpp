#include "ResourceStore.h"
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <json/json.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>

namespace resource {
ResourceStore::ResourceStore(std::filesystem::path root, std::uint64_t max_size)
    : _root(std::filesystem::absolute(std::move(root))), _max_size(max_size) {
    if (!max_size) throw Error(400, "invalid maximum size");
    std::filesystem::create_directories(_root);
}
std::filesystem::path ResourceStore::Path(const std::string& id, const char* suffix) const {
    if (id.size() != 36 || !std::all_of(id.begin(), id.end(), /** @brief 只接受资源 ID 所允许的十六进制字符与连字符。 */ [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-';
    })) throw Error(400, "invalid resource id");
    return _root / (id + suffix);
}
Metadata ResourceStore::Create(int owner, const std::string& name, const std::string& type,
                              std::uint64_t size, const std::string& sha256) {
    if (owner <= 0 || name.empty() || name.size() > 255 || size == 0) throw Error(400, "invalid metadata");
    if (size > _max_size) throw Error(413, "file exceeds size limit");
    if (type != "image/png" && type != "image/jpeg" && type != "video/mp4" && type != "video/x-msvideo" &&
        type != "application/octet-stream")
        throw Error(415, "unsupported media type");
    if (sha256.size() != 64 || !std::all_of(sha256.begin(), sha256.end(), /** @brief 只接受 SHA256 文本的小写十六进制字符。 */ [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    })) throw Error(400, "SHA-256 required");
    const auto id = boost::uuids::to_string(boost::uuids::random_generator()());
    Json::Value value;
    value["id"] = id; value["owner"] = owner; value["name"] = name;
    value["media_type"] = type; value["size"] = Json::UInt64(size); value["sha256"] = sha256;
    std::ofstream metadata(Path(id, ".json"), std::ios::binary);
    metadata << value;
    metadata.close();
    if (!metadata) throw Error(507, "cannot persist upload metadata");
    std::ofstream bytes(Path(id, ".part"), std::ios::binary);
    bytes.close();
    if (!bytes) { std::filesystem::remove(Path(id, ".json")); throw Error(507, "cannot create upload"); }
    return Inspect(id);
}
Metadata ResourceStore::Inspect(const std::string& id) const {
    std::ifstream stream(Path(id, ".json"), std::ios::binary);
    if (!stream) throw Error(404, "resource not found");
    Json::Value value;
    stream >> value;
    Metadata result{id, value["owner"].asInt(), value["name"].asString(),
        value["media_type"].asString(), value["size"].asUInt64(), 0, false, value["sha256"].asString()};
    result.ready = std::filesystem::exists(Path(id, ".data"));
    result.offset = std::filesystem::file_size(Path(id, result.ready ? ".data" : ".part"));
    return result;
}
Metadata ResourceStore::Owned(const std::string& id, int owner) const {
    auto result = Inspect(id);
    if (result.owner != owner) throw Error(403, "resource belongs to another user");
    return result;
}
Metadata ResourceStore::Append(const std::string& id, int owner, std::uint64_t offset,
                              const char* data, std::size_t size) {
    auto metadata = Owned(id, owner);
    if (metadata.ready || metadata.offset != offset) throw Error(409, "upload offset mismatch");
    if (offset > metadata.size || size > metadata.size - offset) throw Error(413, "upload exceeds declared size");
    if (size > BUFFER_SIZE) throw Error(400, "write buffer exceeds limit");
    std::ofstream stream(Path(id, ".part"), std::ios::binary | std::ios::app);
    stream.write(data, static_cast<std::streamsize>(size));
    stream.close();
    if (!stream) throw Error(507, "file write failed; query offset before retry");
    metadata.offset += size;
    return metadata;
}
std::string ResourceStore::Digest(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Error(404, "file missing");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!digest || EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1) throw Error(500, "digest initialization failed");
    std::array<char, BUFFER_SIZE> buffer{};
    while (stream) {
        stream.read(buffer.data(), buffer.size());
        if (EVP_DigestUpdate(digest.get(), buffer.data(), static_cast<std::size_t>(stream.gcount())) != 1)
            throw Error(500, "digest update failed");
    }
    if (!stream.eof()) throw Error(500, "file read failed");
    unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned int count = 0;
    if (EVP_DigestFinal_ex(digest.get(), bytes, &count) != 1) throw Error(500, "digest failed");
    std::ostringstream output;
    for (unsigned int i = 0; i < count; ++i) output << std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[i]);
    return output.str();
}
Metadata ResourceStore::Complete(const std::string& id, int owner) {
    auto metadata = Owned(id, owner);
    if (metadata.ready) return metadata;
    if (metadata.offset != metadata.size) throw Error(409, "upload incomplete");
    if (Digest(Path(id, ".part")) != metadata.sha256) throw Error(422, "SHA-256 mismatch");
    const auto prefix = Read(id, 0, 12);
    const bool png = prefix.size() >= 8 && std::string(prefix.data(), 8) == std::string("\x89PNG\r\n\x1a\n", 8);
    const bool jpeg = prefix.size() >= 3 && static_cast<unsigned char>(prefix[0]) == 255 &&
        static_cast<unsigned char>(prefix[1]) == 216 && static_cast<unsigned char>(prefix[2]) == 255;
    const bool mp4 = prefix.size() >= 12 && std::string(prefix.data() + 4, 4) == "ftyp";
    const bool avi = prefix.size() >= 12 && std::string(prefix.data(), 4) == "RIFF" && std::string(prefix.data() + 8, 4) == "AVI ";
    if (!((metadata.media_type == "image/png" && png) || (metadata.media_type == "image/jpeg" && jpeg) ||
          (metadata.media_type == "video/mp4" && mp4) || (metadata.media_type == "video/x-msvideo" && avi) ||
          metadata.media_type == "application/octet-stream"))
        throw Error(415, "media signature mismatch");
    std::filesystem::rename(Path(id, ".part"), Path(id, ".data"));
    metadata.ready = true;
    return metadata;
}
std::vector<char> ResourceStore::Read(const std::string& id, std::uint64_t offset, std::size_t limit) const {
    const auto metadata = Inspect(id);
    if (offset > metadata.offset) throw Error(416, "offset outside resource");
    const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(std::min(limit, BUFFER_SIZE), metadata.offset - offset));
    std::vector<char> bytes(size);
    std::ifstream stream(Path(id, metadata.ready ? ".data" : ".part"), std::ios::binary);
    stream.seekg(static_cast<std::streamoff>(offset));
    stream.read(bytes.data(), static_cast<std::streamsize>(size));
    if (!stream) throw Error(500, "file read failed");
    return bytes;
}
}
