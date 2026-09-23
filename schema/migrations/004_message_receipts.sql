CREATE TABLE private_chat_receipt_clock (
    chat_id INT NOT NULL PRIMARY KEY,
    last_revision BIGINT NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_german2_ci;
-- migration-statement
CREATE TABLE chat_message_receipt (
    chat_id INT NOT NULL,
    message_id INT NOT NULL,
    recipient_uid INT NOT NULL,
    level TINYINT NOT NULL,
    delivered_at BIGINT NOT NULL,
    read_at BIGINT NULL,
    revision BIGINT NOT NULL,
    PRIMARY KEY (chat_id, message_id, recipient_uid),
    UNIQUE KEY receipt_revision (chat_id, revision),
    CONSTRAINT receipt_level CHECK (level IN (1,2)),
    CONSTRAINT receipt_read CHECK ((level=1 AND read_at IS NULL) OR (level=2 AND read_at IS NOT NULL)),
    CONSTRAINT receipt_version CHECK (revision>0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_german2_ci;
