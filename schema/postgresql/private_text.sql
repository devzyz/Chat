-- One call runs inside the caller's transaction. The caller sends raw text as hex to preserve zero bytes.
CREATE FUNCTION commit_text(sender integer, recipient integer, conversation integer,
    expected_revision bigint, expected_epoch bigint, messages jsonb)
RETURNS TABLE(id bigint, uuid text, created boolean, sent_at bigint)
LANGUAGE plpgsql SET search_path = pg_catalog, chat AS $$
DECLARE
    pair chat.private_chat%ROWTYPE;
    stored chat.chat_message%ROWTYPE;
    msg jsonb;
    body bytea;
    identity text;
    is_new boolean;
BEGIN
    IF recipient=0 THEN
        PERFORM 1 FROM chat.group_chat WHERE chat_id=conversation AND NOT dissolved FOR UPDATE;
        IF NOT FOUND OR NOT EXISTS (SELECT 1 FROM chat.group_chat_member WHERE chat_id=conversation
            AND user_id=sender AND state='active' AND (expected_epoch<=0 OR membership_epoch=expected_epoch)) THEN
            RAISE EXCEPTION USING ERRCODE='P1001', MESSAGE='InvalidMembership';
        END IF;
    ELSE
    SELECT * INTO pair FROM chat.private_chat WHERE chat_id=conversation FOR UPDATE;
    IF NOT FOUND OR NOT ((pair.user1_id=sender AND pair.user2_id=recipient)
        OR (pair.user1_id=recipient AND pair.user2_id=sender)) THEN
        RAISE EXCEPTION USING ERRCODE='P1001', MESSAGE='InvalidMembership';
    END IF;
    END IF;
    FOR msg IN SELECT value FROM jsonb_array_elements(messages) LOOP
        identity := msg->>'uuid';
        body := decode(msg->>'body','hex');
        SELECT * INTO stored FROM chat.chat_message WHERE send_id=sender AND client_msg_uuid=identity FOR UPDATE;
        is_new := false;
        IF NOT FOUND THEN
            IF recipient<>0 AND (NOT pair.relationship_active OR pair.relationship_revision <>
                (CASE WHEN expected_revision>0 THEN expected_revision ELSE 1 END)) THEN
                RAISE EXCEPTION USING ERRCODE='P1001', MESSAGE='InvalidMembership';
            END IF;
            INSERT INTO chat.chat_message(chat_id,send_id,recv_id,status,client_msg_uuid,content)
                VALUES(conversation,sender,recipient,0,identity,body)
                ON CONFLICT (send_id,client_msg_uuid) DO NOTHING RETURNING * INTO stored;
            is_new := FOUND;
            IF NOT is_new THEN
                SELECT * INTO STRICT stored FROM chat.chat_message WHERE send_id=sender AND client_msg_uuid=identity FOR UPDATE;
            END IF;
        END IF;
        IF stored.chat_id<>conversation OR stored.recv_id<>recipient OR stored.content<>body THEN
            RAISE EXCEPTION USING ERRCODE='P1002', MESSAGE='Conflict';
        END IF;
        IF stored.message_id NOT BETWEEN 1 AND 2147483647 THEN RAISE EXCEPTION 'MessageIdOutOfRange'; END IF;
        RETURN QUERY SELECT stored.message_id,identity,is_new,extract(epoch FROM stored.created_at)::bigint;
    END LOOP;
END $$;
