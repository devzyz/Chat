'use strict';
const fs = require('node:fs');
const path = require('node:path');

/** 保存顶层及一层服务目录的有界日志尾部，拒绝符号链接并脱敏，不复制配置或任意深层文件。 */
function preserveLogs(root, destination, secrets) {
    if (!fs.existsSync(root)) return;
    const rootInfo = fs.lstatSync(root);
    if (!rootInfo.isDirectory() || rootInfo.isSymbolicLink()) return;
    const directories = [{ directory: root, prefix: '' }];
    let saved = 0;
    for (const { directory, prefix } of directories) {
        for (const name of fs.readdirSync(directory).sort().slice(0, 20)) {
            if (saved >= 20) return;
            const file = path.join(directory, name); const info = fs.lstatSync(file);
            if (info.isSymbolicLink()) continue;
            if (info.isDirectory() && !prefix) { directories.push({ directory: file, prefix: name }); continue; }
            if (!info.isFile() || !/\.(txt|log)$/i.test(name)) continue;
            const offset = Math.max(0, info.size - 65536);
            const handle = fs.openSync(file, 'r'); let bytes;
            try { bytes = Buffer.alloc(Math.min(info.size, 65536)); fs.readSync(handle, bytes, 0, bytes.length, offset); }
            finally { fs.closeSync(handle); }
            let text = bytes.toString('utf8');
            if (offset > 0) text = text.includes('\n') ? text.slice(text.indexOf('\n') + 1) : '';
            text = text.split('\n').filter(/** 敏感协议与 SQL 整行剔除，不导出业务正文。 */ line =>
                !/token|passwd|password|验证码|varify|verifycode|SELECT |INSERT |UPDATE /i.test(line)).join('\n');
            for (const secret of secrets.filter(Boolean)) text = text.replaceAll(secret, '<redacted>');
            const target = path.join(destination, prefix, name);
            fs.mkdirSync(path.dirname(target), { recursive: true });
            fs.writeFileSync(target, text); saved++;
        }
    }
}
module.exports = { preserveLogs };
