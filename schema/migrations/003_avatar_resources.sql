-- Additive migration. Existing text messages and tables are preserved.
CREATE TABLE IF NOT EXISTS resource_file (
    resource_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
    owner_uid BIGINT UNSIGNED NOT NULL,
    name VARCHAR(255) NOT NULL,
    media_type VARCHAR(64) NOT NULL,
    size_bytes BIGINT UNSIGNED NOT NULL,
    sha256 CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- migration-statement
CREATE TABLE IF NOT EXISTS resource_message (
    message_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    resource_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    sender_uid BIGINT UNSIGNED NOT NULL,
    client_uuid VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    UNIQUE KEY resource_sender_uuid (sender_uid,client_uuid),
    KEY resource_reference (resource_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- migration-statement

-- Profile resources are separate from private message attachment grants.
CREATE TABLE IF NOT EXISTS user_avatar (
    uid BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    resource_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY avatar_resource (resource_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
