ALTER TABLE `user`
    ADD UNIQUE INDEX `unique_user_uid` (`uid`),
    ADD UNIQUE INDEX `unique_user_name` (`name`),
    ADD UNIQUE INDEX `unique_user_email` (`email`);

-- migration-statement

ALTER TABLE `chat_message`
    ADD COLUMN `client_msg_uuid` CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
    ADD UNIQUE INDEX `unique_sender_client_uuid` (`send_id`, `client_msg_uuid`);

-- migration-statement

INSERT INTO `user_id` (`id`) SELECT COALESCE(MAX(`uid`), 0) FROM `user`;

-- migration-statement

CREATE PROCEDURE `reg_user`(
    IN new_name VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_german2_ci,
    IN new_email VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_german2_ci,
    IN new_pwd VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_german2_ci, OUT result INT)
SQL SECURITY INVOKER
MODIFIES SQL DATA
BEGIN
    DECLARE next_uid BIGINT UNSIGNED;
    DECLARE counter_count INT;
    DECLARE EXIT HANDLER FOR 1062
    BEGIN
        ROLLBACK;
        SET result = 0;
    END;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET result = -1;
    END;
    SET result = -1;
    START TRANSACTION;
    -- All registrations take this one row lock before checking uniqueness.
    SELECT COUNT(*) INTO counter_count FROM user_id;
    IF counter_count <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'invalid_uid_counter';
    END IF;
    SELECT id INTO next_uid FROM user_id FOR UPDATE;
    IF next_uid >= 2147483647 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'uid_out_of_range';
    END IF;
    IF EXISTS (SELECT 1 FROM user WHERE name = new_name OR email = new_email) THEN
        SET result = 0;
        ROLLBACK;
    ELSE
        SET next_uid = next_uid + 1;
        UPDATE user_id SET id = next_uid;
        INSERT INTO user(uid, name, email, password, description, icon, sex)
            VALUES(next_uid, new_name, new_email, new_pwd, '', '', 0);
        COMMIT;
        SET result = next_uid;
    END IF;
END;
