#include "ResourceStore.h"
#include "ResourceHttpServer.h"
#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <map>
#include "status.grpc.pb.h"
#include <grpcpp/grpcpp.h>

/** 仅在测试可执行文件中提供固定用户与 Token 的 Status 认证替身。 */
class FixtureStatus final : public message::StatusService::Service {
    /** 核对测试 Token 和用户范围后回显认证结果。 */
    grpc::Status Login(grpc::ServerContext*, const message::LoginReq* request, message::LoginRsp* response) override {
        response->set_error(request->token() == "fixture-token" && request->uid() >= 7 && request->uid() <= 9 ? 0 : 1010);
        response->set_uid(request->uid()); response->set_token(request->token()); return grpc::Status::OK;
    }
};

namespace {
/** 拥有资源测试临时目录、原始字节及摘要，供真实文件存储合同使用。 */
class StoreTest : public testing::Test {
protected:
    std::filesystem::path root;
    std::string bytes = std::string("\x89PNG\r\n\x1a\n", 8) + std::string(150000, 'x');
    std::string digest;
    /** 创建独立临时目录、写入样本并计算摘要。 */
    void SetUp() override {
        root = std::filesystem::temp_directory_path() / ("chat-resource-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        std::ofstream output(root / "source", std::ios::binary); output << bytes; output.close();
        digest = resource::ResourceStore::Digest(root / "source");
    }
    /** 清理本用例拥有的临时资源目录。 */
    void TearDown() override { std::filesystem::remove_all(root); }
    /** 为固定拥有者创建样本资源上传元数据。 */
    resource::Metadata Create(resource::ResourceStore& store) {
        return store.Create(7, "picture.png", "image/png", bytes.size(), digest);
    }
    /** 按存储缓冲上限从指定偏移分块写入剩余样本。 */
    void Write(resource::ResourceStore& store, const std::string& id, std::uint64_t offset = 0) {
        while (offset < bytes.size()) {
            const auto count = std::min<std::size_t>(resource::ResourceStore::BUFFER_SIZE, bytes.size() - offset);
            store.Append(id, 7, offset, bytes.data() + offset, count); offset += count;
        }
    }
};
/** 验证重建存储后恢复偏移、完成幂等及有界下载内容一致。 */
TEST_F(StoreTest, ResumeAfterStoreRecreation) {
    std::string id;
    { resource::ResourceStore store(root, 1000000); id = Create(store).id; store.Append(id, 7, 0, bytes.data(), 12345); }
    resource::ResourceStore reopened(root, 1000000);
    EXPECT_EQ(reopened.Owned(id, 7).offset, 12345u);
    Write(reopened, id, 12345);
    EXPECT_TRUE(reopened.Complete(id, 7).ready);
    EXPECT_TRUE(reopened.Complete(id, 7).ready);
    std::string result;
    for (std::uint64_t offset = 0; offset < bytes.size();) {
        auto part = reopened.Read(id, offset, 900000); EXPECT_LE(part.size(), resource::ResourceStore::BUFFER_SIZE);
        result.append(part.data(), part.size()); offset += part.size();
    }
    EXPECT_EQ(result, bytes);
}
/** 验证错误上传偏移被拒绝且文件长度不变。 */
TEST_F(StoreTest, RejectsWrongOffsetWithoutChangingFile) {
    resource::ResourceStore store(root, 1000000); auto item = Create(store);
    store.Append(item.id, 7, 0, bytes.data(), 3);
    EXPECT_THROW(store.Append(item.id, 7, 0, bytes.data(), 3), resource::Error);
    EXPECT_EQ(store.Inspect(item.id).offset, 3u);
}
/** 验证其他用户不能查询所属上传、追加或完成。 */
TEST_F(StoreTest, RejectsOtherOwner) {
    resource::ResourceStore store(root, 1000000); auto item = Create(store);
    EXPECT_THROW(store.Owned(item.id, 8), resource::Error);
    EXPECT_THROW(store.Append(item.id, 8, 0, bytes.data(), 3), resource::Error);
    EXPECT_THROW(store.Complete(item.id, 8), resource::Error);
}
/** 验证未完整上传的资源不能发布。 */
TEST_F(StoreTest, IncompleteUploadCannotComplete) {
    resource::ResourceStore store(root, 1000000); auto item = Create(store);
    EXPECT_THROW(store.Complete(item.id, 7), resource::Error);
    EXPECT_FALSE(store.Inspect(item.id).ready);
}
/** 验证内容摘要不匹配不能发布。 */
TEST_F(StoreTest, DigestMismatchCannotPublish) {
    resource::ResourceStore store(root, 1000000); auto item = Create(store);
    bytes.back() = 'y'; Write(store, item.id);
    EXPECT_THROW(store.Complete(item.id, 7), resource::Error);
    EXPECT_FALSE(store.Inspect(item.id).ready);
}
/** 验证资源声明大小、单块大小及尾部写入边界。 */
TEST_F(StoreTest, DeclaredLengthAndBufferAreBounded) {
    resource::ResourceStore store(root, 1000000); auto item = Create(store);
    EXPECT_THROW(store.Append(item.id, 7, 0, bytes.data(), bytes.size()), resource::Error);
    EXPECT_THROW(store.Create(7, "x", "image/png", 1000001, digest), resource::Error);
    Write(store, item.id);
    EXPECT_THROW(store.Append(item.id, 7, bytes.size(), bytes.data(), 1), resource::Error);
}
/** 验证资源标识不能穿越存储目录。 */
TEST_F(StoreTest, PathTraversalRejected) {
    resource::ResourceStore store(root, 1000000);
    EXPECT_THROW(store.Inspect("../source"), resource::Error);
}
/** 验证声明媒体类型与内容签名不匹配时拒绝发布。 */
TEST_F(StoreTest, MediaSignatureIsChecked) {
    resource::ResourceStore store(root, 1000000);
    auto item = store.Create(7, "movie.mp4", "video/mp4", bytes.size(), digest);
    Write(store, item.id);
    EXPECT_THROW(store.Complete(item.id, 7), resource::Error);
}
}
/** 按显式参数运行测试认证服务、真实资源 HTTP 夹具或 GTest 合同。 */
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--serve-status-fixture") {
        FixtureStatus service; grpc::ServerBuilder builder; int port = 0;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
        builder.RegisterService(&service); auto server = builder.BuildAndStart();
        if (!server) return 1;
        std::cout << port << std::endl; server->Wait(); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--serve-test") {
        // Test executable only: real HTTP/storage with a deterministic authentication fake.
        boost::asio::io_context context;
        resource::ResourceStore store(argv[2], 8ull * 1024 * 1024 * 1024);
        std::map<int, std::string> avatars;
        resource::ResourceHttpServer server(context, "127.0.0.1", 0, store,
            /** 仅允许固定测试用户与测试 Token 通过认证。 */ [](int uid, const std::string& token) { return (uid == 7 || uid == 8) && token == "fixture-token"; },
            /** 仅当资源已成为某个测试头像时授予测试下载权限。 */ [&avatars](int, const resource::Metadata& metadata) {
                for (const auto& entry : avatars) if (entry.second == metadata.id) return true;
                return false;
            }, {}, /** 查询测试内存头像映射。 */ [&avatars](int owner) { return avatars[owner]; },
            /** 更新指定测试用户的头像资源映射。 */ [&avatars](int owner, const std::string& id) { avatars[owner] = id; });
        server.Start();
        std::cout << server.Port() << std::endl;
        context.run();
        return 0;
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
