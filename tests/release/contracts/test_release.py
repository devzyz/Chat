import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import package
import publish
import smoke


class ReleaseContracts(unittest.TestCase):
    """验证发布包、发布保护及失败诊断，不启动个人服务。"""
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.artifacts = self.root / 'artifacts'
        self.artifacts.mkdir()
        self.sha = 'a' * 40
        for app in package.APPS:
            with zipfile.ZipFile(self.artifacts / (app + '.zip'), 'w') as archive:
                for name in package.REQUIRED:
                    if name.startswith(app + '/'):
                        archive.writestr(name[len(app) + 1:], b'synthetic unit fixture')
                name = 'config.json' if app == 'VarifyServer' else 'config.ini'
                archive.writestr(name, '{"redis":{"host":"private-example"}}' if app == 'VarifyServer'
                                 else '[Redis]\nHost=private-example\nUser=fixture-value\n')
        self.output = self.root / 'output'

    def build(self):
        return package.assemble(ROOT, self.artifacts, self.output, self.sha)

    def test_assembled_package_uses_production_proto_layout_and_removes_runtime_config(self):
        archive = self.build()
        destination = self.root / 'extracted'
        metadata = package.verify(archive, destination, self.sha)
        self.assertEqual(metadata['version'], package.read_version(ROOT))
        self.assertTrue((destination / 'proto/varify.proto').is_file())
        self.assertFalse((destination / 'StatusServer').exists())
        self.assertFalse((destination / 'proto/status.proto').exists())
        for name in ('migrate.js', 'SchemaMigration.js', 'MysqlSession.js', 'SchemaContract.h'):
            self.assertEqual((destination / 'migrations' / name).read_bytes(),
                             (ROOT / 'schema' / name).read_bytes())
        for name in package.PG_MIGRATION_FILES:
            self.assertEqual((destination / 'migrations/postgresql' / name).read_bytes(),
                             (ROOT / 'schema/postgresql' / name).read_bytes())
        self.assertFalse((destination / 'GateServer/config.ini').exists())
        self.assertNotIn('fixture-value', (destination / 'GateServer/config.ini.template').read_text())
        self.assertNotIn('private-example', (destination / 'VarifyServer/config.json.template').read_text())
        self.assertEqual((self.output / 'SHA256SUMS').read_text(),
                         package.sha256(archive.read_bytes()) + '  ' + archive.name + '\n')

    def test_database_template_preserves_pg_default_and_removes_machine_settings(self):
        source = self.root / 'config.ini'
        source.write_text('[Database]\nDriver=postgresql\nHost=old-host\nUser=old-user\nPassword=fixture-only\n',
                          encoding='utf-8')
        template = package.blank_config(source)
        self.assertIn('[Database]\nDriver=postgresql\nHost=\nUser=\nPassword=\n', template)

    def test_missing_package_or_runtime_fails(self):
        (self.artifacts / 'GateServer.zip').unlink()
        with self.assertRaisesRegex(ValueError, 'Expected one CI ZIP'):
            self.build()
        with zipfile.ZipFile(self.artifacts / 'GateServer.zip', 'w') as archive:
            archive.writestr('config.ini', '[Redis]\nHost=\n')
        with self.assertRaisesRegex(ValueError, 'Missing runtime'):
            self.build()

    def test_resource_package_is_required_and_sanitized(self):
        """资源服务必须随包提供，且只交付无连接信息的配置模板。"""
        self.assertIn('ResourceServer', package.APPS)
        archive = self.build()
        destination = self.root / 'resource-package'
        package.verify(archive, destination, self.sha)
        self.assertTrue((destination / 'ResourceServer/ResourceServer.exe').is_file())
        self.assertFalse((destination / 'ResourceServer/config.ini').exists())
        self.assertNotIn('private-example', (destination / 'ResourceServer/config.ini.template').read_text())
        (self.artifacts / 'ResourceServer.zip').unlink()
        with self.assertRaisesRegex(ValueError, 'Expected one CI ZIP for ResourceServer'):
            package.assemble(ROOT, self.artifacts, self.root / 'missing-resource', self.sha)

    def test_every_server_requires_its_own_postgres_runtime(self):
        """每个发布单元都必须能从自身目录加载 PG 驱动。"""
        root = self.root / 'missing-libpq'
        package.verify(self.build(), root, self.sha)
        for server in package.SERVERS:
            with self.subTest(server=server):
                dll = root / server / 'libpq.dll'
                content = dll.read_bytes()
                dll.unlink()
                with self.assertRaisesRegex(ValueError, f'Missing runtime file: {server}/libpq'):
                    package.check_layout(root)
                dll.write_bytes(content)

    def test_resource_runtime_files_cannot_be_omitted(self):
        """资源服务缺少可执行文件或运行库时必须拒绝发布包。"""
        self.assertIn('ResourceServer', package.APPS)
        archive = self.build()
        destination = self.root / 'missing-runtime'
        package.verify(archive, destination, self.sha)
        for name in ('ResourceServer.exe', 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
            file = destination / 'ResourceServer' / name
            content = file.read_bytes()
            file.unlink()
            with self.assertRaisesRegex(ValueError, 'Missing runtime file: ResourceServer/'):
                package.check_layout(destination)
            file.write_bytes(content)

    def test_other_commit_and_modified_payload_fail_verification(self):
        archive = self.build()
        with self.assertRaisesRegex(ValueError, 'another commit'):
            package.verify(archive, self.root / 'wrong', 'b' * 40)
        damaged = self.root / 'damaged.zip'
        with zipfile.ZipFile(archive) as original, zipfile.ZipFile(damaged, 'w') as changed:
            for entry in original.infolist():
                changed.writestr(entry, b'changed' if entry.filename == 'GateServer/GateServer.exe'
                                 else original.read(entry))
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            package.verify(damaged, self.root / 'damaged', self.sha)

    def test_zip_cannot_escape_extraction_directory(self):
        archive = self.root / 'unsafe.zip'
        with zipfile.ZipFile(archive, 'w') as content:
            content.writestr('../escape', 'bad')
        with self.assertRaises(ValueError):
            package.unpack(archive, self.root / 'extract')
        self.assertFalse((self.root / 'escape').exists())

    def test_roles_and_rolling_releases(self):
        from rehearse import LocalReleases
        import receive
        import windows_roles
        real = self.root / 'windows'
        windows_roles.build(self.artifacts, real, self.sha, 'v1.0.0')
        windows_roles.extract_pair(real, self.root / 'smoke', self.sha)
        with zipfile.ZipFile(real / publish.FORMAL[0]) as client:
            self.assertIn('chat.exe', client.namelist())
            self.assertNotIn('GateServer/GateServer.exe', client.namelist())
        (self.root / 'VERSION').write_text('1.0.0-rc1')
        with self.assertRaises(ValueError):
            package.read_version(self.root)
        hub = LocalReleases(self.root / 'hub')
        hub.root.mkdir()
        for tag, sha in [('v1.0.0', self.sha), ('v1.10.0', 'b'*40), ('v1.2.0', 'c'*40)]:
            files = self.root / tag
            files.mkdir()
            for name in publish.FORMAL:
                (files / name).write_bytes((name+tag).encode())
            hub.refs[tag] = sha
            if tag == 'v1.0.0':
                hub.fail_once = (tag, publish.FORMAL[2])
                with self.assertRaises(IOError):
                    publish.publish_formal(hub, tag, sha, files)
                self.assertTrue(hub.rows[tag]['draft'])
                self.assertNotIn('latest', hub.rows)
            publish.publish_formal(hub, tag, sha, files)
            publish.publish_formal(hub, tag, sha, files)
            if tag == 'v1.10.0':
                hub.fail_once = ('latest', publish.ROLLING[1])
                with self.assertRaises(IOError):
                    publish.sync_latest(hub)
                self.assertEqual(receive.resolve_tag('latest', hub.get)[:2], ('v1.0.0', self.sha))
                self.assertEqual(publish.contents(hub.rows['latest']), {
                    n:publish.contents(hub.rows['v1.0.0'])[n] for n in publish.ROLLING})
            publish.sync_latest(hub)
        self.assertEqual(set(publish.contents(hub.rows['latest'])), set(publish.ROLLING))
        self.assertEqual(receive.resolve_tag('latest', hub.get)[:2], ('v1.10.0', 'b'*40))
        self.assertEqual(hub.ref('latest'), 'b'*40)
        for invalid in ['v1', 'v01.0.0', 'v1.0.0-rc.1', '../v1.0.0']:
            with self.assertRaises(ValueError):
                receive.resolve_tag(invalid, hub.get)
        hub.refs['v1.10.0'] = 'd'*40
        with self.assertRaises(ValueError):
            receive.resolve_tag('latest', hub.get)


    def test_smoke_failure_still_writes_failed_report(self):
        report = self.root / 'report.xml'
        with self.assertRaises(Exception):
            smoke.run(self.root, self.sha, report)
        self.assertIn('failures="1"', report.read_text())

    def test_mysql_tools_reject_missing_or_non_mysql8_pair(self):
        """在创建数据目录前拒绝缺失客户端或不匹配的 MySQL 版本。"""
        tools = self.root / 'mysql-bin'
        tools.mkdir()
        # 使用同一目录的非规范路径复现 Windows TEMP 短文件名展开后的路径差异。
        tools = tools / '..' / tools.name
        (tools / 'mysqld.exe').touch()
        with patch.dict(os.environ, {'CHAT_SMOKE_MYSQL_BIN': str(tools)}), patch.object(smoke.subprocess, 'run') as run:
            with self.assertRaisesRegex(RuntimeError, 'mysql.exe'):
                smoke.mysql_tools()
            run.assert_not_called()
            (tools / 'mysql.exe').touch()
            for versions in [('mysqld Ver 5.7.44', 'mysql Ver 8.4.4'),
                             ('mysqld Ver 8.4.4', 'mysql Ver 9.0.0')]:
                run.side_effect = [Mock(stdout=value, returncode=0) for value in versions]
                with self.assertRaisesRegex(RuntimeError, 'MySQL 8'):
                    smoke.mysql_tools()
            run.side_effect = [Mock(stdout=value, returncode=0) for value in
                               ('mysqld  Ver 8.4.4 for Win64', 'mysql  Ver 8.4.4 for Win64')]
            self.assertEqual(smoke.mysql_tools(), ((tools / 'mysqld.exe').resolve(),
                                                  (tools / 'mysql.exe').resolve()))

    def test_mysql_initialization_failure_retains_external_diagnostics(self):
        """初始化失败仍保留 stderr 到临时应用目录之外，不启动数据库。"""
        diagnostics = self.root / 'diagnostics'
        def initialize(command, **options):
            """模拟 mysqld 把初始化错误写到真实日志句柄后失败。"""
            self.assertIn('--console', command)
            options['stderr'].write(b'synthetic initialization failure\n')
            raise smoke.subprocess.CalledProcessError(1, command)
        with tempfile.TemporaryDirectory(dir=self.root) as temporary:
            with patch.object(smoke, 'mysql_tools', return_value=(self.root / 'mysqld.exe', self.root / 'mysql.exe')), \
                 patch.object(smoke.subprocess, 'run', side_effect=initialize), \
                 patch.object(smoke.subprocess, 'Popen') as start:
                with self.assertRaises(smoke.subprocess.CalledProcessError):
                    with smoke.resource_database(Path(temporary), diagnostics):
                        self.fail('Failed initialization cannot yield a ready database')
                start.assert_not_called()
        self.assertIn('synthetic initialization failure',
                      (diagnostics / 'resource-mysql.log').read_text())

    def test_mysql_startup_failure_retains_log_after_application_cleanup(self):
        """数据库就绪失败后保留启动错误，不因应用临时目录清理而丢失。"""
        diagnostics = self.root / 'startup-diagnostics'
        process = Mock()
        process.poll.return_value = 1
        def start(command, **options):
            """模拟数据库在绑定阶段写入错误并自行退出。"""
            self.assertIn('--console', command)
            options['stderr'].write(b'synthetic startup failure\n')
            return process
        with tempfile.TemporaryDirectory(dir=self.root) as temporary:
            with patch.object(smoke, 'mysql_tools', return_value=(self.root / 'mysqld.exe', self.root / 'mysql.exe')), \
                 patch.object(smoke.subprocess, 'run'), patch.object(smoke.subprocess, 'Popen', side_effect=start):
                with self.assertRaisesRegex(RuntimeError, 'exited before readiness'):
                    with smoke.resource_database(Path(temporary), diagnostics):
                        self.fail('Exited database cannot become ready')
        self.assertIn('synthetic startup failure', (diagnostics / 'resource-mysql.log').read_text())
        process.kill.assert_not_called()


if __name__ == '__main__':
    unittest.main()
