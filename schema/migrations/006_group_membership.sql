CREATE TABLE group_migration_guard (valid TINYINT NOT NULL CHECK(valid=1));
-- migration-statement
INSERT INTO group_migration_guard(valid)
SELECT 0 FROM group_chat g WHERE g.owner_uid IS NULL OR
    NOT EXISTS(SELECT 1 FROM user u WHERE u.uid=g.owner_uid) OR
    NOT EXISTS(SELECT 1 FROM group_chat_member m WHERE m.chat_id=g.chat_id AND m.user_id=g.owner_uid AND m.role=1) OR
    EXISTS(SELECT 1 FROM group_chat_member m WHERE m.chat_id=g.chat_id AND (m.role NOT IN (0,1) OR (m.role=1 AND m.user_id<>g.owner_uid))) LIMIT 1;
-- migration-statement
DROP TABLE group_migration_guard;
-- migration-statement
ALTER TABLE group_chat ADD COLUMN creator_uid INT NULL,
    ADD COLUMN original_name VARCHAR(255) NULL,
    ADD COLUMN dissolved BOOLEAN NOT NULL DEFAULT FALSE,
    ADD COLUMN revision BIGINT NOT NULL DEFAULT 1;
-- migration-statement
UPDATE group_chat SET creator_uid=owner_uid, original_name=name;
-- migration-statement
ALTER TABLE group_chat DROP INDEX group_creation,
    ADD UNIQUE KEY group_creation(creator_uid,creation_uuid);
-- migration-statement
CREATE TABLE group_creation_member (chat_id BIGINT UNSIGNED NOT NULL, user_id BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY(chat_id,user_id)) ENGINE=InnoDB;
-- migration-statement
INSERT INTO group_creation_member SELECT chat_id,user_id FROM group_chat_member;
-- migration-statement
ALTER TABLE group_chat_member ADD COLUMN state VARCHAR(16) NOT NULL DEFAULT 'active',
    ADD COLUMN membership_epoch BIGINT NOT NULL DEFAULT 1,
    ADD COLUMN joined_after_id BIGINT NOT NULL DEFAULT 0;
-- migration-statement
CREATE TABLE group_operation (actor_uid INT NOT NULL, request_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    chat_id BIGINT UNSIGNED NOT NULL, payload TEXT NOT NULL, result TEXT NOT NULL,
    PRIMARY KEY(actor_uid,request_id)) ENGINE=InnoDB;
