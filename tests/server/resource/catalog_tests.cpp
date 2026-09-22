#include "../../../common/resource/ResourceCatalog.h"
#include <gtest/gtest.h>
#include <cstdlib>

TEST(ResourceCatalogIntegration, KilledIdleConnectionsAreReplacedBeforeTheNextOperation) {
    const auto* endpoint = std::getenv("RESOURCE_TEST_MYSQL");
    if (!endpoint) GTEST_SKIP() << "Requires disposable MySQL";
    std::unique_ptr<sql::Connection> control(sql::mysql::get_mysql_driver_instance()->connect(endpoint, "root", ""));
    control->setSchema("resource_test");
    resource::ResourceCatalog catalog(endpoint, "root", "", "resource_test");
    std::unique_ptr<sql::Statement> statement(control->createStatement());
    std::unique_ptr<sql::ResultSet> rows(statement->executeQuery(
        "SELECT ID FROM information_schema.PROCESSLIST WHERE DB='resource_test' AND ID<>CONNECTION_ID()"));
    std::vector<int> ids;
    while (rows->next()) ids.push_back(rows->getInt(1));
    rows.reset();
    ASSERT_EQ(ids.size(), 2);
    for (int id : ids) statement->execute("KILL CONNECTION " + std::to_string(id));
    EXPECT_NO_THROW(catalog.GetAvatar(900001));
    EXPECT_NO_THROW(catalog.GetAvatar(900002));
}

TEST(ResourceCatalogIntegration, CommitRetryMembershipAndDownloadAuthorization) {
    const auto* endpoint = std::getenv("RESOURCE_TEST_MYSQL");
    if (!endpoint) GTEST_SKIP() << "RESOURCE_TEST_MYSQL must identify the disposable test MySQL";
    std::unique_ptr<sql::Connection> connection(sql::mysql::get_mysql_driver_instance()->connect(endpoint, "root", ""));
    connection->setSchema("resource_test");
    std::unique_ptr<sql::Statement> statement(connection->createStatement());
    statement->execute("DELETE FROM resource_message");
    statement->execute("DELETE FROM resource_file");
    statement->execute("DELETE FROM chat_message");
    statement->execute("DELETE FROM private_chat");
    statement->execute("INSERT INTO private_chat(chat_id,user1_id,user2_id) VALUES(1,7,8),(2,7,9)");
    resource::ResourceCatalog catalog(endpoint, "root", "", "resource_test");
    const std::string id = "00000000-0000-0000-0000-000000000001";
    catalog.Publish(id, 7, "image.png", "image/png", 123, std::string(64, 'a'));
    EXPECT_FALSE(catalog.CanRead(8, id));
    const auto first = catalog.CommitMessage(7, 8, 1, "message-1", id);
    const auto retry = catalog.CommitMessage(7, 8, 1, "message-1", id);
    EXPECT_EQ(first.id, retry.id);
    EXPECT_EQ(first.content, retry.content);
    EXPECT_TRUE(catalog.CanRead(8, id));
    EXPECT_FALSE(catalog.CanRead(9, id));
    EXPECT_THROW(catalog.CommitMessage(8, 7, 1, "wrong-owner", id), std::exception);
    EXPECT_THROW(catalog.CommitMessage(7, 9, 1, "wrong-member", id), std::exception);
    EXPECT_THROW(catalog.CommitMessage(7, 9, 2, "message-1", id), std::exception);
    std::unique_ptr<sql::ResultSet> count(statement->executeQuery("SELECT COUNT(*) FROM chat_message"));
    ASSERT_TRUE(count->next()); EXPECT_EQ(count->getInt(1), 1);
    count.reset();
    std::unique_ptr<sql::ResultSet> identity(statement->executeQuery(
        "SELECT client_msg_uuid FROM chat_message"));
    ASSERT_TRUE(identity->next());
    EXPECT_EQ(identity->getString(1), "message-1");
}

TEST(ResourceCatalogIntegration, AvatarPublicationIsOwnedPersistentAndSeparateFromMessages) {
    const auto* endpoint = std::getenv("RESOURCE_TEST_MYSQL");
    if (!endpoint) GTEST_SKIP() << "Requires disposable MySQL";
    resource::ResourceCatalog catalog(endpoint, "root", "", "resource_test");
    const std::string first = "00000000-0000-0000-0000-000000000011";
    const std::string second = "00000000-0000-0000-0000-000000000012";
    catalog.Publish(first, 7, "avatar.png", "image/png", 1024, std::string(64, 'b'));
    catalog.Publish(second, 7, "avatar.png", "image/png", 1024, std::string(64, 'c'));
    EXPECT_FALSE(catalog.CanRead(8, first));
    catalog.SetAvatar(7, first);
    catalog.SetAvatar(7, first);
    EXPECT_EQ(catalog.GetAvatar(7), first);
    EXPECT_TRUE(catalog.CanRead(8, first));
    EXPECT_THROW(catalog.SetAvatar(8, second), std::exception);
    EXPECT_THROW(catalog.SetAvatar(7, "missing"), std::exception);
    EXPECT_EQ(catalog.GetAvatar(7), first);
    catalog.SetAvatar(7, second);
    EXPECT_FALSE(catalog.CanRead(8, first));
    EXPECT_TRUE(catalog.CanRead(8, second));
    resource::ResourceCatalog reopened(endpoint, "root", "", "resource_test");
    EXPECT_EQ(reopened.GetAvatar(7), second);
}
