#include "../../../ChatServer/ChatServer/MysqlDao.h"
#include "../../../ChatServer/ChatServer/ConfigMgr.h"
#include <gtest/gtest.h>
#include <cstdlib>

TEST(MysqlDaoIntegration, PrivateChatIsSymmetricAndFailedInsertDoesNotCommitAnOrphan) {
    const auto* endpoint = std::getenv("RESOURCE_TEST_MYSQL");
    const auto* config = std::getenv("MYSQL_DAO_TEST_CONFIG");
    if (!endpoint || !config) GTEST_SKIP() << "Requires disposable MySQL fixture";
    ConfigMgr::SetConfigPath(config);
    std::unique_ptr<sql::Connection> control(sql::mysql::get_mysql_driver_instance()->connect(endpoint, "root", ""));
    control->setSchema("resource_test");
    std::unique_ptr<sql::Statement> statement(control->createStatement());
    statement->execute("ALTER TABLE chat AUTO_INCREMENT=100");
    MysqlDao dao;
    int forward = -1, reverse = -1;
    ASSERT_TRUE(dao.CreatePrivateChat(7001, 7002, forward));
    ASSERT_TRUE(dao.CreatePrivateChat(7002, 7001, reverse));
    EXPECT_EQ(forward, reverse);

    const auto count_chats = [&] {
        std::unique_ptr<sql::ResultSet> rows(statement->executeQuery("SELECT COUNT(*) FROM chat"));
        if (!rows->next()) throw std::runtime_error("missing count");
        return rows->getInt(1);
    };
    const int before = count_chats();
    // Only this disposable schema: force an error after the chat row was written.
    statement->execute("CREATE TRIGGER reject_test_private BEFORE INSERT ON private_chat "
        "FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture rejection'");
    struct Cleanup {
        sql::Statement& statement;
        ~Cleanup() {
            try { statement.execute("DROP TRIGGER IF EXISTS reject_test_private"); }
            catch (...) { ADD_FAILURE() << "fixture trigger cleanup failed"; }
        }
    } cleanup{*statement};
    int rejected = -1;
    EXPECT_FALSE(dao.CreatePrivateChat(7003, 7004, rejected));
    EXPECT_EQ(rejected, -1);
    EXPECT_EQ(count_chats(), before);
    statement->execute("DROP TRIGGER reject_test_private");
    EXPECT_TRUE(dao.CreatePrivateChat(7003, 7004, rejected));
    statement->execute("DELETE FROM private_chat WHERE user1_id IN (7001,7003)");
    statement->execute("DELETE FROM chat WHERE chat_id IN (" + std::to_string(forward) + "," + std::to_string(rejected) + ")");
}
