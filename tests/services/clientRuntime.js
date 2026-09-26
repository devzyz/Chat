'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { pack, verify, inspect, dependencies, validateLibrarySource, validateRelocated } = require('./messageRuntime');

/** 验证客户端动态 SQLite 插件的包内依赖，并真实打开独立临时消息库。 */
function verifyClient(bundle) {
    bundle = path.resolve(bundle);
    verify(bundle, 'chat_e2e_client', null);
    const plugin = path.join(bundle, 'sqldrivers', 'libqsqlite.so');
    assert.ok(fs.existsSync(plugin), 'client SQLite plugin missing');
    const names = JSON.parse(fs.readFileSync(path.join(bundle, 'sqlite-libraries.json'), 'utf8'));
    validateRelocated(inspect(plugin, bundle), bundle, names);
    execFileSync(path.join(bundle, 'chat_e2e_client'), ['--check-storage'], { timeout: 10000,
        env: { ...process.env, LD_LIBRARY_PATH: bundle, LD_PRELOAD: '', QT_PLUGIN_PATH: '' }, stdio: 'pipe' });
}

/** 补入 ldd 不会发现的 SQLite 插件及其依赖，确保迁移后不借用构建机 Qt。 */
function packClient(source, destination, qtRoot, installedRoot) {
    pack(source, destination, [qtRoot, installedRoot], 'chat_e2e_client', null);
    const roots = [qtRoot, installedRoot].map(/** 规范化显式安装树供来源校验。 */ root => fs.realpathSync(root));
    const plugin = validateLibrarySource(path.join(qtRoot, 'plugins', 'sqldrivers', 'libqsqlite.so'), [roots[0]]);
    const libraries = dependencies(inspect(plugin));
    for (const [name, library] of libraries) {
        const real = validateLibrarySource(library, roots);
        const target = path.join(destination, name);
        if (fs.existsSync(target)) assert.ok(fs.readFileSync(target).equals(fs.readFileSync(real)), 'conflicting SQLite dependency');
        else fs.copyFileSync(real, target);
    }
    fs.mkdirSync(path.join(destination, 'sqldrivers'));
    fs.copyFileSync(plugin, path.join(destination, 'sqldrivers', 'libqsqlite.so'));
    fs.writeFileSync(path.join(destination, 'sqlite-libraries.json'), JSON.stringify([...libraries.keys()].sort()) + '\n');
    fs.writeFileSync(path.join(destination, 'qt.conf'), '[Paths]\nPlugins=.\n');
    verifyClient(destination);
    const pluginPath = path.join(destination, 'sqldrivers', 'libqsqlite.so');
    const hiddenPlugin = path.join(destination, 'sqlite-plugin.hidden');
    fs.renameSync(pluginPath, hiddenPlugin);
    try {
        assert.throws(/** 缺少插件时真实存储探针必须失败，防止借用宿主 Qt 插件。 */ () => {
            execFileSync(path.resolve(destination, 'chat_e2e_client'), ['--check-storage'], { timeout: 10000,
                env: { ...process.env, LD_LIBRARY_PATH: path.resolve(destination), LD_PRELOAD: '', QT_PLUGIN_PATH: '' }, stdio: 'pipe' });
        });
    } finally { fs.renameSync(hiddenPlugin, pluginPath); }
}

if (require.main === module) {
const [action, source, destination, qtRoot, installedRoot] = process.argv.slice(2);
assert.ok(action === 'pack' || action === 'verify');
if (action === 'pack') {
    assert.ok(qtRoot && installedRoot, 'Qt and vcpkg roots are required for the client bundle');
    packClient(source, destination, qtRoot, installedRoot);
}
else verifyClient(source);
}
