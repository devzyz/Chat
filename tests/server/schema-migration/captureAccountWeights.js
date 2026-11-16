'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { startLocalMysql } = require('./localMysql');

// 从独立 MySQL 实例导出历史排序规则的主权重，不读取账号或部署数据库。
async function capture(bin, target) {
    const fixture = await startLocalMysql(bin);
    try {
        // 生成十六进制数位，枚举 BMP 码点。
        const digits = Array.from({ length: 16 },
            // 构造一个固定 SQL 字面量作为枚举数位。
            (_, index) => 'SELECT ' + index + ' AS d').join(' UNION ALL ');
        const rows = await fixture.execute(
            'WITH digits AS (' + digits + '), points AS (' +
            'SELECT a.d+16*b.d+256*c.d+4096*d.d AS cp FROM digits a,digits b,digits c,digits d) ' +
            "SELECT cp,HEX(WEIGHT_STRING(CONVERT(CONVERT(UNHEX(LPAD(HEX(cp),8,'0')) USING utf32) USING utf8mb4) " +
            "COLLATE utf8mb4_german2_ci)) FROM points WHERE cp NOT BETWEEN 55296 AND 57343 ORDER BY cp;");
        const ranges = [];
        let previous;
        // 将连续等值或线性两字节权重合成区间，其余权重原样保留。
        for (const row of rows.split(/\r?\n/)) {
            const [id, hex = ''] = row.split('\t');
            const point = Number(id);
            if (hex.length % 4 !== 0 || !/^[0-9A-F]*$/.test(hex)) throw new Error('InvalidAccountWeight');
            if (previous && point === previous.end + 1 && hex.length === previous.hex.length) {
                let step = hex === previous.last ? 0 : 1;
                if (step === 0 || (hex.length <= 8 && Number.parseInt(hex,16) === Number.parseInt(previous.last,16)+1)) {
                    if (previous.start === previous.end || previous.step === step) {
                        previous.end = point;
                        previous.last = hex;
                        previous.step = step;
                        continue;
                    }
                }
            }
            previous = { start: point, end: point, step: 0, hex, last: hex };
            ranges.push(previous);
        }
        if (ranges[0].start !== 0 || ranges.at(-1).end !== 65535) throw new Error('IncompleteAccountWeights');
        // 将连续权重区间序列化为确定的 SQL。
        const tuples = ranges.map(r => '(' + [r.start,r.end,r.step,"decode('" + r.hex + "','hex')"].join(',') + ')');
        const header = '-- MySQL utf8mb4_german2_ci primary weights. Regenerate with captureAccountWeights.js.\n';
        fs.writeFileSync(target, header + 'INSERT INTO account_weight(lo,hi,step,weight) VALUES\n' + tuples.join(',\n') + ';\n');
    } finally {
        await fixture.close();
    }
}

// 生成失败以非零状态退出。
if (require.main === module) capture(process.env.CHAT_MYSQL_BIN,
    path.join(__dirname, '../../../schema/postgresql/account_weights.sql')).catch(
    // 只发布夹具错误类别。
    error => {
    console.error(error.message);
    process.exitCode = 1;
});
