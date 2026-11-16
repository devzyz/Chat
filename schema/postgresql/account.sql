-- Trim only ASCII spaces as MySQL TRIM does, preserving all other bytes.
CREATE FUNCTION trim_ascii_space(raw bytea) RETURNS bytea LANGUAGE plpgsql IMMUTABLE STRICT AS $$
DECLARE first integer := 0; last integer := octet_length(raw);
BEGIN
    WHILE first<last AND get_byte(raw,first)=32 LOOP first:=first+1; END LOOP;
    WHILE first<last AND get_byte(raw,last-1)=32 LOOP last:=last-1; END LOOP;
    RETURN substr(raw,first+1,last-first);
END $$;
CREATE FUNCTION utf8_length(raw bytea) RETURNS integer LANGUAGE plpgsql IMMUTABLE STRICT AS $$
DECLARE
    segment bytea := raw;
    zero_pos integer;
    total integer := 0;
BEGIN
    LOOP
        zero_pos := position(decode('00','hex') IN segment);
        IF zero_pos=0 THEN RETURN total + char_length(convert_from(segment,'UTF8')); END IF;
        total := total + char_length(convert_from(substr(segment,1,zero_pos-1),'UTF8')) + 1;
        segment := substr(segment,zero_pos+1);
    END LOOP;
END $$;

ALTER TABLE account ADD CHECK (utf8_length(name)<=255 AND utf8_length(email)<=255 AND utf8_length(description)<=255);
ALTER TABLE apply_friend ADD CHECK (utf8_length(description)<=255 AND utf8_length(backname)<=255);
ALTER TABLE friend ADD CHECK (utf8_length(backname)<=255);
ALTER TABLE group_chat ADD CHECK (utf8_length(name)<=255 AND utf8_length(original_name)<=255);
ALTER TABLE chat_message ADD CHECK (octet_length(content)<=65535 AND utf8_length(content)>=0);
ALTER TABLE resource_file ADD CHECK (utf8_length(name)<=255);

-- The same key backs both lookups and unique constraints. Keep original UTF-8 bytes, including U+0000.
CREATE FUNCTION account_key(raw bytea) RETURNS bytea LANGUAGE plpgsql STABLE STRICT
SET search_path = pg_catalog, chat AS $$
DECLARE
    pos integer := 0;
    size integer;
    cp integer;
    point bytea;
    r chat.account_weight%ROWTYPE;
    adjusted bigint;
    result bytea := ''::bytea;
BEGIN
    WHILE pos < octet_length(raw) LOOP
        cp := get_byte(raw,pos);
        size := CASE WHEN cp < 128 THEN 1 WHEN cp < 224 THEN 2 WHEN cp < 240 THEN 3 ELSE 4 END;
        IF cp <> 0 THEN
            cp := ascii(convert_from(substr(raw,pos+1,size),'UTF8'));
        END IF;
        pos := pos+size;
        IF cp > 65535 THEN
            point := decode('fffd','hex');
        ELSE
            SELECT * INTO STRICT r FROM chat.account_weight WHERE lo<=cp ORDER BY lo DESC LIMIT 1;
            IF cp > r.hi THEN RAISE EXCEPTION 'AccountWeightMissing'; END IF;
            point := r.weight;
            IF r.step=1 AND cp>r.lo THEN
                adjusted := ('x'||lpad(encode(point,'hex'),16,'0'))::bit(64)::bigint + cp-r.lo;
                point := decode(lpad(to_hex(adjusted),2*octet_length(point),'0'),'hex');
            END IF;
        END IF;
        result := result || point;
    END LOOP;
    -- PAD SPACE operates on collation elements, including equivalent Unicode space characters.
    WHILE octet_length(result)>=2 AND substr(result,octet_length(result)-1)=decode('0209','hex') LOOP
        result := substr(result,1,octet_length(result)-2);
    END LOOP;
    RETURN result;
END $$;

CREATE FUNCTION account_identity() RETURNS trigger LANGUAGE plpgsql SET search_path = pg_catalog, chat AS $$
BEGIN
    NEW.name_key := chat.account_key(NEW.name);
    NEW.email_key := chat.account_key(NEW.email);
    RETURN NEW;
END $$;
CREATE TRIGGER account_identity BEFORE INSERT OR UPDATE ON account
FOR EACH ROW EXECUTE FUNCTION account_identity();

-- The caller owns the transaction. A duplicate never consumes a public UID.
CREATE FUNCTION reg_user(new_name bytea,new_email bytea,new_password text) RETURNS integer
LANGUAGE plpgsql SET search_path = pg_catalog, chat AS $$
DECLARE
    current_uid bigint;
BEGIN
    SELECT id INTO STRICT current_uid FROM chat.user_id FOR UPDATE;
    IF current_uid >= 2147483647 THEN RETURN -1; END IF;
    INSERT INTO chat.account(uid,name,email,password,description,icon,sex)
        VALUES(current_uid+1,new_name,new_email,new_password,'','',0);
    UPDATE chat.user_id SET id=current_uid+1;
    RETURN (current_uid+1)::integer;
EXCEPTION
    WHEN unique_violation THEN
        IF EXISTS(SELECT 1 FROM chat.account WHERE name_key=chat.account_key(new_name)
            OR email_key=chat.account_key(new_email)) THEN RETURN 0; END IF;
        RETURN -1;
    WHEN no_data_found OR too_many_rows THEN RETURN -1;
END $$;
