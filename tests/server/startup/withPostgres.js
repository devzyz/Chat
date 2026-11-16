'use strict';

const { execFileSync } = require('node:child_process');
const { main } = require('../../../schema/migrate');
const { postgresFixture } = require('../schema-migration/localPostgres');

// 以专属临时 PG 运行真实服务启动测试，Windows 子进程继承夹具凭据和端口。
async function run(binary, args) {
    const fixture = await postgresFixture();
    try {
        await main(fixture.env, 'apply');
        execFileSync(binary, args, { stdio: 'inherit', windowsHide: true, timeout: 180000,
            env: { ...fixture.env, CHAT_STARTUP_PG_PORT: fixture.env.CHAT_PG_PORT,
                CHAT_DB_PASSWORD: fixture.env.CHAT_PG_PASSWORD } });
    } finally { fixture.close(); }
}

// 只公开退出状态，测试二进制自身产生分层报告。
run(process.argv[2], process.argv.slice(3)).catch(error => {
    console.error(error.status ? 'StartupIntegrationFailed' : error.message);
    process.exitCode = 1;
});
