ALTER TABLE group_chat_member DROP PRIMARY KEY,
    ADD PRIMARY KEY (chat_id,user_id),
    ADD INDEX member_chats (user_id,chat_id);
-- migration-statement
ALTER TABLE group_chat ADD COLUMN owner_uid INT NULL,
    ADD COLUMN creation_uuid CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
    ADD UNIQUE KEY group_creation (owner_uid,creation_uuid);
