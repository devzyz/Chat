"use strict";

const path = require("node:path");
const { createRequire } = require("node:module");
const load = createRequire(path.resolve(__dirname, "../../../VarifyServer/package.json"));
const grpc = load("@grpc/grpc-js");
const loader = load("@grpc/proto-loader");
const schema = loader.loadSync(path.resolve(__dirname, "../../../proto/chat.proto"), { keepCase: true });
const { ChatService } = grpc.loadPackageDefinition(schema).message;
const client = new ChatService("127.0.0.1:" + process.argv[2], grpc.credentials.createInsecure());

// 通道就绪后才开始短期限，避免把连接建立耗时算作 Redis 鉴权阻塞。
client.waitForReady(Date.now() + 4000, (startupError) => {
    if (startupError) throw startupError;
    // 由 Python 的 Redis 屏障暂缓本次鉴权，期限内不能发送通知。
    client.NotifyMessageReceiptChanged({ uid: 10, chat_id: 201, revision: "99" },
        { deadline: Date.now() + 500 }, (error) => {
            client.close();
            if (!error || error.code !== grpc.status.DEADLINE_EXCEEDED) process.exitCode = 1;
        });
});
