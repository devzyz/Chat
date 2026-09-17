import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import package
import publish
import smoke


class ReleaseContracts(unittest.TestCase):
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
        self.assertFalse((destination / 'GateServer/config.ini').exists())
        self.assertNotIn('fixture-value', (destination / 'GateServer/config.ini.template').read_text())
        self.assertNotIn('private-example', (destination / 'VarifyServer/config.json.template').read_text())
        self.assertEqual((self.output / 'SHA256SUMS').read_text(),
                         package.sha256(archive.read_bytes()) + '  ' + archive.name + '\n')

    def test_missing_package_or_runtime_fails(self):
        (self.artifacts / 'GateServer.zip').unlink()
        with self.assertRaisesRegex(ValueError, 'Expected one CI ZIP'):
            self.build()
        with zipfile.ZipFile(self.artifacts / 'GateServer.zip', 'w') as archive:
            archive.writestr('config.ini', '[Redis]\nHost=\n')
        with self.assertRaisesRegex(ValueError, 'Missing runtime'):
            self.build()

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

    def test_numeric_version_and_published_version_are_protected(self):
        (self.root / 'VERSION').write_text('1.0.0-rc1')
        with self.assertRaises(ValueError):
            package.read_version(self.root)
        self.build()
        environment = {'GITHUB_EVENT_NAME': 'push', 'GITHUB_REF': 'refs/heads/master',
                       'GITHUB_REPOSITORY': 'example/chat'}
        releases = [[{'tag_name': 'v' + package.read_version(ROOT), 'draft': False}]]
        with patch.dict(os.environ, environment), patch.object(publish, 'gh', return_value=json.dumps(releases)) as gh:
            with self.assertRaisesRegex(ValueError, 'already published'):
                publish.publish(self.output, self.sha)
            self.assertEqual(gh.call_count, 1)

    def test_unpublished_draft_retry_uploads_and_checks_same_bytes_before_publication(self):
        archive = self.build()
        calls = []
        def gh(*arguments):
            calls.append(arguments)
            if arguments[0] == 'api':
                return json.dumps([[{'tag_name': 'v' + package.read_version(ROOT),
                                     'draft': True, 'target_commitish': self.sha}]])
            if arguments[:2] == ('release', 'download'):
                directory = Path(arguments[arguments.index('--dir') + 1])
                directory.mkdir()
                for name in (archive.name, 'SHA256SUMS'):
                    (directory / name).write_bytes((self.output / name).read_bytes())
            return ''
        environment = {'GITHUB_EVENT_NAME': 'push', 'GITHUB_REF': 'refs/heads/master',
                       'GITHUB_REPOSITORY': 'example/chat', 'GITHUB_RUN_ATTEMPT': '2'}
        with patch.dict(os.environ, environment), patch.object(publish, 'gh', side_effect=gh):
            publish.publish(self.output, self.sha)
        self.assertEqual(calls[-1][:2], ('release', 'edit'))
        self.assertIn('--draft=false', calls[-1])
        self.assertFalse(any(call[:2] == ('release', 'create') for call in calls))

    def test_smoke_failure_still_writes_failed_report(self):
        report = self.root / 'report.xml'
        with self.assertRaises(Exception):
            smoke.run(self.root, self.sha, report)
        self.assertIn('failures="1"', report.read_text())


if __name__ == '__main__':
    unittest.main()
