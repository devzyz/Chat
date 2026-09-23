#include "../../../ChatServer/ChatServer/MysqlDao.h"
#include "../../../ChatServer/ChatServer/ConfigMgr.h"
#include <gtest/gtest.h>
#include <cstdlib>

/** 在显式隔离 MySQL 中验证私聊双方顺序对称，插入失败不会提交孤立会话。 */
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

    const auto count_chats = /** 读取聊天表记录数供失败回滚断言。 */ [&] {
        std::unique_ptr<sql::ResultSet> rows(statement->executeQuery("SELECT COUNT(*) FROM chat"));
        if (!rows->next()) throw std::runtime_error("missing count");
        return rows->getInt(1);
    };
    const int before = count_chats();
    // Only this disposable schema: force an error after the chat row was written.
    statement->execute("CREATE TRIGGER reject_test_private BEFORE INSERT ON private_chat "
        "FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture rejection'");
    /** 作用域内持有故障触发器清理责任，数据库语句对象必须仍存活。 */
    struct Cleanup {
        sql::Statement& statement;
        /** 删除本测试注入的触发器，清理失败记录测试失败。 */
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
