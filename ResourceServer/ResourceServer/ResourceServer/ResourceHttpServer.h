#pragma once
#include "ResourceStore.h"
#include <boost/asio.hpp>
#include <functional>
#include <memory>

namespace resource {
class ResourceHttpServer {
public:
    using Authenticate = std::function<bool(int, const std::string&)>;
    using CanRead = std::function<bool(int, const Metadata&)>;
    using Publish = std::function<void(const Metadata&)>;
    using GetAvatar = std::function<std::string(int)>;
    using SetAvatar = std::function<void(int, const std::string&)>;
    ResourceHttpServer(boost::asio::io_context& context, const std::string& host, unsigned short port,
                       ResourceStore& store, Authenticate authenticate, CanRead can_read = {}, Publish publish = {},
                       GetAvatar get_avatar = {}, SetAvatar set_avatar = {});
    ~ResourceHttpServer();
    void Start();
    void Stop();
    unsigned short Port() const;
private:
    struct Impl;
    std::shared_ptr<Impl> _impl;
};
}
