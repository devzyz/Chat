-- MySQL schema 7 updates these timestamps only when a row actually changes.
CREATE FUNCTION touch_row_times() RETURNS trigger LANGUAGE plpgsql SET search_path = pg_catalog AS $$
DECLARE
    field text;
    updates jsonb := '{}';
    timestamp_value timestamptz := date_trunc('second',statement_timestamp());
BEGIN
    IF OLD IS DISTINCT FROM NEW THEN
        FOREACH field IN ARRAY TG_ARGV LOOP
            IF to_jsonb(OLD)->field IS NOT DISTINCT FROM to_jsonb(NEW)->field THEN
                updates := updates || jsonb_build_object(field,timestamp_value);
            END IF;
        END LOOP;
        NEW := jsonb_populate_record(NEW,updates);
    END IF;
    RETURN NEW;
END $$;
CREATE TRIGGER chat_time BEFORE UPDATE ON chat FOR EACH ROW EXECUTE FUNCTION touch_row_times('created_at');
CREATE TRIGGER private_time BEFORE UPDATE ON private_chat FOR EACH ROW EXECUTE FUNCTION touch_row_times('created_at');
CREATE TRIGGER group_time BEFORE UPDATE ON group_chat FOR EACH ROW EXECUTE FUNCTION touch_row_times('created_at');
CREATE TRIGGER member_time BEFORE UPDATE ON group_chat_member FOR EACH ROW EXECUTE FUNCTION touch_row_times('joined_at');
CREATE TRIGGER message_times BEFORE UPDATE ON chat_message FOR EACH ROW EXECUTE FUNCTION touch_row_times('created_at','updated_at');
CREATE TRIGGER avatar_time BEFORE UPDATE ON user_avatar FOR EACH ROW EXECUTE FUNCTION touch_row_times('updated_at');
