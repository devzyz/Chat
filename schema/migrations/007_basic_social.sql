ALTER TABLE user ADD COLUMN profile_revision BIGINT NOT NULL DEFAULT 1;
-- migration-statement
ALTER TABLE apply_friend ADD COLUMN revision BIGINT NOT NULL DEFAULT 1;
-- migration-statement
ALTER TABLE private_chat ADD COLUMN relationship_active BOOLEAN NOT NULL DEFAULT TRUE,
    ADD COLUMN relationship_revision BIGINT NOT NULL DEFAULT 1;
