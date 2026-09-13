#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include <jdbc/cppconn/connection.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>

namespace chat_schema {

// Shared with SchemaMigration.js: semantic metadata, not version-specific SHOW CREATE formatting.
// MySQL 8.0 removes standalone -- comments; 8.4 preserves them. Keep the
// indentation/newline left by 8.0 while excluding those comments from the hash.
inline constexpr const char* CONTRACT_QUERY = R"SQL(
SELECT SHA2(GROUP_CONCAT(item ORDER BY item SEPARATOR '\n'),256) FROM (
    SELECT CONCAT('column:',table_name,':',column_name,':',ordinal_position,':',column_type,':',
        is_nullable,':',COALESCE(column_default,'<NULL>'),':',extra,':',COALESCE(collation_name,'')) AS item
    FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name<>'schema_version'
    UNION ALL
    SELECT CONCAT('index:',table_name,':',index_name,':',non_unique,':',seq_in_index,':',
        COALESCE(column_name,''),':',COALESCE(sub_part,''),':',index_type)
    FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name<>'schema_version'
    UNION ALL
    SELECT CONCAT('table:',table_name,':',COALESCE(engine,''),':',COALESCE(table_collation,''))
    FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name<>'schema_version'
    UNION ALL
    SELECT CONCAT('routine:',routine_name,':',routine_type,':',security_type,':',
        SHA2(REGEXP_REPLACE(REPLACE(routine_definition,CHAR(13),''),
            '(?m)^([ \\t]*)-- [^\\n]*', '$1'),256))
    FROM information_schema.routines WHERE routine_schema=DATABASE()
    UNION ALL
    SELECT CONCAT('parameter:',specific_name,':',ordinal_position,':',parameter_mode,':',
        parameter_name,':',dtd_identifier,':',COALESCE(collation_name,''))
    FROM information_schema.parameters WHERE specific_schema=DATABASE()
    UNION ALL
    SELECT CONCAT('trigger:',trigger_name,':',action_timing,':',event_manipulation,':',
        event_object_table,':',SHA2(action_statement,256))
    FROM information_schema.triggers WHERE trigger_schema=DATABASE()
) AS schema_items
)SQL";

inline constexpr const char* CONTRACT_HASH = "ed537ccc940ce556b6838307a2229223841ee32aebe3dc62ae23e40855a42dab";
inline constexpr const char* VERSION_ROWS =
    "1:a549039f9846ac8dc5120e3f89481a92934a2d485d054019c6dfb69c9241957d:applied,"
    "2:be6b412a603046f67125d6a3f61aff2bf4054ee5154ca73a25ec7d824f3a2f85:applied";

inline void Verify(sql::Connection& connection) {
    try {
        const std::unique_ptr<sql::Statement> statement(connection.createStatement());
        statement->execute("SET SESSION group_concat_max_len=65536");
        const std::unique_ptr<sql::ResultSet> versions(statement->executeQuery(
            "SELECT GROUP_CONCAT(CONCAT(version,':',checksum,':',state) ORDER BY version SEPARATOR ',') "
            "FROM schema_version"));
        if (!versions->next() || versions->getString(1) != VERSION_ROWS) {
            throw std::runtime_error("schema_version_not_current");
        }
        const std::unique_ptr<sql::ResultSet> metadata(statement->executeQuery(CONTRACT_QUERY));
        if (!metadata->next() || metadata->getString(1) != CONTRACT_HASH) {
            throw std::runtime_error("schema_contract_drift");
        }
        const std::unique_ptr<sql::ResultSet> counter(statement->executeQuery(
            "SELECT COUNT(*)=1 AND MIN(id)>=COALESCE((SELECT MAX(uid) FROM user),0) "
            "AND MAX(id)<=2147483647 FROM user_id"));
        if (!counter->next() || !counter->getBoolean(1)) throw std::runtime_error("invalid_uid_counter");
    } catch (const std::exception&) {
        // Driver diagnostics may contain endpoints or data. Startup has one safe error boundary.
        throw std::runtime_error("schema_verification_failed");
    }
}

} // namespace chat_schema
