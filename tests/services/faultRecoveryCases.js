'use strict';
const assert = require('node:assert/strict');
const { randomUUID, createHash } = require('node:crypto');
const { poll } = require('./dependencyCoordinator');

async function runFaultRecoveryCases({ alice, bob, users, chatId, sql, record, prepareRelay,
    relogin, restartBob, invalidHistory, stopRelay }) {
    const dropUuid = randomUUID(), replayUuid = randomUUID();
    let relay;
    const rows = client => client.control.command('snapshot', { chatId }).then(value => value.messages);
    const durable = uuid => sql.execute(`SELECT message_id FROM chat_message WHERE client_msg_uuid='${uuid}'`);
    const send = (client, target, uuid, text) => client.control.command('send', { chatId, toUid: target.uid, uuid, text });
    const observe = async (uuid, text, sender) => {
        let matches;
        await poll(async () => {
            matches = await Promise.all([alice, bob].map(async client =>
                (await rows(client)).filter(row => row.uuid === uuid && row.sender === sender)));
            return matches.every(items => items.length === 1 && BigInt(items[0].messageId) > 0);
        }, 10000);
        const id = await durable(uuid);
        for (const items of matches) {
            assert.equal(items[0].messageId, id);
            assert.equal(items[0].sha256, createHash('sha256').update(text).digest('hex'));
        }
        return id;
    };
    await record('E03-RECOVER-05', 'owned codec relay replaces only the fault-path endpoint after server restart', async () => {
        relay = await prepareRelay(dropUuid, replayUuid);
        await relogin(alice, 0);
    });
    let id;
    await record('E03-RECOVER-06', 'committed ACK is dropped and its client generation disconnects', async () => {
        const result = await send(alice, users[1], dropUuid, 'uncertain committed message');
        assert.equal(result.status, 'disconnected');
        const evidence = relay.read();
        assert.equal(evidence.droppedAck, true);
        id = await durable(dropUuid);
        assert.equal(evidence.committedId, id);
        const pending = (await rows(alice)).filter(row => row.uuid === dropUuid);
        assert.equal(pending.length, 1);
        assert.equal(pending[0].messageId, '0');
        assert.equal((await alice.control.command('snapshot')).active, false);
    });
    await record('E03-RECOVER-07', 'same process reauthenticates and retries original bytes to the same durable ID', async () => {
        await relogin(alice, 0);
        assert.equal(await observe(dropUuid, 'uncertain committed message', users[0].uid), id);
        assert.equal(await sql.execute(`SELECT COUNT(*) FROM chat_message WHERE client_msg_uuid='${dropUuid}'`), '1');
    });
    await record('E03-RECOVER-08', 'replayed real peer notification produces one model item', async () => {
        assert.equal((await send(bob, users[0], replayUuid, 'replayed notification')).error, 0);
        await observe(replayUuid, 'replayed notification', users[1].uid);
        assert.equal(relay.read().replayedNotification, true);
    });
    await record('E03-RECOVER-09', 'Chat B restart releases its old session and discovery restores cross-instance delivery', async () => {
        await restartBob();
        await relogin(bob, 1);
        const uuid = randomUUID();
        assert.equal((await send(alice, users[1], uuid, 'after Chat B restart')).error, 0);
        await observe(uuid, 'after Chat B restart', users[0].uid);
    });
    await record('E03-RECOVER-10', 'invalid wire history cursors return bounded stable errors without messages', invalidHistory);
    await record('E03-RECOVER-11', 'fault relay releases every owned connection and records completed fault evidence', async () => {
        await stopRelay(relay.owned);
        const evidence = relay.read();
        assert.equal(evidence.complete, true);
        assert.equal(evidence.droppedAck, true);
        assert.equal(evidence.replayedNotification, true);
        assert.equal(evidence.committedId, id);
    });
}
module.exports = { runFaultRecoveryCases };
