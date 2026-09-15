import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts/release'))
spec = importlib.util.spec_from_file_location('release_gate', ROOT / 'scripts/release/release_gate.py')
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class ReleaseContracts(unittest.TestCase):
    def identity(self):
        return dict(version='1.0.0', sourceSha='a' * 40, runId=12, runAttempt=1,
                    repository='devzyz/Chat', workflowSha='b' * 40)

    def admission(self):
        return dict(sourceSha='a' * 40, candidateSha='a' * 40, currentNPass=True,
                    compatibility='BOOTSTRAP_NO_PROMOTED_N_MINUS_1', caseCount=33,
                    manifests={'phase3c': 'c' * 64, 'phase3d': 'd' * 64})

    def test_R01_BUILD_01(self):
        identity = dict(version='1.0.0', sourceSha='a' * 40, runId=12, runAttempt=1,
                        repository='devzyz/Chat', workflowSha='b' * 40)
        gate.validate_identity(identity)
        for version in ('1.0.0-rc.1', '1.0.0-alpha.1', '1.0.0+build.1', 'v1.0.0', '01.0.0', '1.0'):
            with self.subTest(version=version), self.assertRaises(ValueError):
                gate.validate_identity({**identity, 'version': version})
        for field, value in [('version', '../x'), ('sourceSha', 'main'), ('runAttempt', 2), ('runId', 0)]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                gate.validate_identity({**identity, field: value})

    def test_R01_BUILD_02(self):
        from settings_receipt_cases import check_receipts
        check_receipts(self)
        checks = [dict(name=name, head_sha='a' * 40, status='completed', conclusion='success', id=n)
                  for n, name in enumerate(gate.CHECKS, 1)]
        gate.admit(self.identity(), self.admission(), checks, [])
        for missing in range(len(checks)):
            with self.assertRaises(ValueError):
                gate.admit(self.identity(), self.admission(), checks[:missing] + checks[missing + 1:], [])
        checks.append({**checks[0], 'id': 100, 'conclusion': 'failure'})
        with self.assertRaises(ValueError):
            gate.admit(self.identity(), self.admission(), checks, [])
        from github_release import settings_blockers
        self.assertEqual(len(settings_blockers([], {}, {'days': 90, 'maximum_allowed_days': 90},
                                               {'protected': False})), 3)
        environments = [dict(name=name, can_admins_bypass=False,
                             protection_rules=[{'type': 'required_reviewers',
                                                'reviewers': [{'type': 'User', 'reviewer': {'id': 123}}]}],
                             deployment_branch_policy={'custom_branch_policies': True, 'protected_branches': False})
                        for name in ('release-uat', 'release-promotion')]
        policies = {e['name']: [{'name': 'master', 'type': 'branch'}, {'name': 'v*', 'type': 'tag'}]
                    for e in environments}
        self.assertEqual(settings_blockers(environments, policies, {'days': 30, 'maximum_allowed_days': 30},
                                           {'protected': True}), [])
        policies['release-uat'].append({'name': '*', 'type': 'branch'})
        self.assertTrue(settings_blockers(environments, policies, {'days': 30, 'maximum_allowed_days': 30},
                                          {'protected': True}))

    def test_R01_BUILD_03(self):
        with self.assertRaises(ValueError):
            gate.check_unused(self.identity(), [{'environment': 'candidate-0.9.0', 'sha': 'a' * 40}])
        with self.assertRaises(ValueError):
            gate.check_unused(self.identity(), [{'environment': 'candidate-1.0.0', 'payload': {}}])
        gate.check_unused(self.identity(), [{'environment': 'candidate-0.9.0', 'payload': {}}])
        from github_release import reserve

        class DeploymentService:
            def __init__(service):
                service.records = []

            def pages(service, endpoint):
                return service.records

            def request(service, endpoint, *, body):
                service.records.append(body)
                return {'id': 123, 'sha': 'a' * 40}

        service = DeploymentService()
        self.assertEqual(reserve(service, self.identity()), 123)
        with self.assertRaises(ValueError):
            reserve(service, self.identity())
        self.assertEqual(len(service.records), 1)

    def test_R01_BUILD_04(self):
        for name in ['../x', '/x', 'C:/x', 'a/../x', 'a\\x', 'a//x', 'a/x:stream', 'a/NUL', 'a/x.', 'a/x ']:
            with self.subTest(name=name), self.assertRaises(ValueError):
                gate.safe_path(name)
        gate.safe_path('ChatServer/ChatServer.exe')

    def test_R01_BUILD_05(self):
        checks = [dict(name=n, head_sha='a' * 40, status='completed', conclusion='success', id=i)
                  for i, n in enumerate(gate.CHECKS)]
        with self.assertRaises(ValueError):
            gate.admit(self.identity(), {**self.admission(), 'sourceSha': 'b' * 40}, checks, [])
        with self.assertRaises(ValueError):
            gate.admit(self.identity(), {**self.admission(), 'currentNPass': False}, checks, [])

    def test_R01_BUILD_06(self):
        for value in [{'password': 'canary'}, {'note': 'ghp_' + 'a' * 36}, {'path': 'C:\\runner\\secret'}]:
            with self.assertRaises(ValueError):
                gate.public_metadata(value)
        gate.public_metadata({'sourceSha': 'a' * 40, 'retentionDays': 30})
        gate.public_metadata({'files': {'VarifyServer/node_modules/protobufjs/src/tokenize.js': {'size': 123}}})

    def test_R01_BUILD_07(self):
        with tempfile.TemporaryDirectory() as root:
            folder = Path(root)
            (folder / 'app.exe').write_bytes(b'original')
            files = gate.file_inventory(folder, {'app.exe': 'runtime'})
            gate.verify_files(folder, files)
            (folder / 'app.exe').write_bytes(b'modified')
            with self.assertRaises(ValueError):
                gate.verify_files(folder, files)
            (folder / 'app.exe').write_bytes(b'original')
            (folder / 'extra').write_bytes(b'undeclared')
            with self.assertRaises(ValueError):
                gate.verify_files(folder, files)

    def test_R01_BUILD_08(self):
        import zipfile
        with tempfile.TemporaryDirectory() as root:
            archive = Path(root) / 'input.zip'
            for names in [('a', 'A'), ('../escape',), ('a/b', 'a')]:
                with zipfile.ZipFile(archive, 'w') as stream:
                    for name in names:
                        stream.writestr(name, b'x')
                with self.assertRaises(ValueError):
                    gate.read_archive(archive)
            with zipfile.ZipFile(archive, 'w') as stream:
                stream.writestr('valid', b'bytes')
            self.assertEqual(gate.read_archive(archive), {'valid': b'bytes'})
            with self.assertRaises(ValueError):
                gate.read_archive(archive, max_bytes=2)
            for attributes in (0o120777 << 16, 0x400):
                with zipfile.ZipFile(archive, 'w') as stream:
                    entry = zipfile.ZipInfo('link')
                    entry.external_attr = attributes
                    stream.writestr(entry, b'target')
                with self.assertRaises(ValueError):
                    gate.read_archive(archive)

    def context(self):
        return dict(identity=self.identity(), sourceTreeSha='e' * 40, deploymentId=123,
                    lockHashes={'vcpkg.json': 'f' * 64, 'VarifyServer/package-lock.json': gate.digest(b'fixture')},
                    tools=json.loads((ROOT / 'scripts/release/toolchain-lock.json').read_text())['tools'],
                    upstream={'phase3c': 'c' * 64, 'phase3d': 'd' * 64},
                    compatibility='BOOTSTRAP_NO_PROMOTED_N_MINUS_1',
                    buildInvocations={name: 1 for name in gate.TARGETS})

    def payload(self, root):
        root.mkdir()
        layout = json.loads((ROOT / 'scripts/release/payload-layout.json').read_text())
        allowed = dict(layout['required'])
        for name in allowed:
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(b'fixture')
            if name.endswith('.ini.template'):
                destination.write_bytes(b'[Service]\nPassword=\n')
            elif name.endswith('.json.template'):
                destination.write_bytes(b'{"redis":{"host":null}}')
        inventory = dict(format=1, vcpkgStatus='Package: fixture\nStatus: install ok installed\n',
                         vcpkgManifest={'name': 'fixture', 'dependencies': ['fixture']},
                         npmPackages=[{'path': name.removeprefix('VarifyServer/').removesuffix('/package.json'),
                                       'version': '1.0.0', 'integrity': 'fixture', 'license': 'MIT'}
                                      for name in allowed if '/node_modules/' in name and name.endswith('/package.json')],
                         runtimeFiles={name: info for name, info in gate.file_inventory(root, allowed).items()
                                       if name.endswith(('.exe', '.dll'))},
                         tools=self.context()['tools'], lockHashes=self.context()['lockHashes'],
                         licenseFiles=[], provenance={'qt': 'https://doc.qt.io/qt-6/licensing.html'})
        inventory['vcpkgStatusSha256'] = gate.digest(inventory['vcpkgStatus'].encode())
        (root / 'dependency-inventory.json').write_bytes(gate.json_bytes(inventory))
        return allowed

    def test_R01_BUILD_09(self):
        gate.validate_context(self.context())
        for target in gate.TARGETS:
            context = self.context()
            context['buildInvocations'][target] = 2
            with self.assertRaises(ValueError):
                gate.validate_context(context)
        context = self.context()
        del context['tools']['qt']
        with self.assertRaises(ValueError):
            gate.validate_context(context)
        context = self.context()
        context['tools']['node'] = 'v22.0.0'
        with self.assertRaises(ValueError):
            gate.validate_context(context)

    def test_R01_BUILD_10(self):
        with tempfile.TemporaryDirectory() as root:
            stage = Path(root) / 'stage'
            allowed = self.payload(stage)
            gate.validate_layout(allowed)
            for name in list(allowed):
                reduced = dict(allowed)
                del reduced[name]
                with self.assertRaises(ValueError):
                    gate.validate_layout(reduced)
            with self.assertRaises(ValueError):
                gate.validate_layout({**allowed, 'source/private.txt': 'runtime'})

    def test_R01_BUILD_11(self):
        with tempfile.TemporaryDirectory() as root:
            stage = Path(root) / 'stage'
            allowed = self.payload(stage)
            payload = Path(root) / 'Chat-1.0.0-windows-x64.zip'
            result = gate.pack(stage, allowed, self.context(), payload)
            gate.verify_candidate(payload, result['payloadSha256'], result['manifestSha256'])
            with self.assertRaises((ValueError, FileExistsError)):
                gate.pack(stage, allowed, self.context(), payload)
            with self.assertRaises(ValueError):
                gate.verify_candidate(payload, '0' * 64, result['manifestSha256'])

    def test_R01_BUILD_12(self):
        with tempfile.TemporaryDirectory() as root:
            stage = Path(root) / 'stage'
            allowed = self.payload(stage)
            (stage / 'unexpected').write_text('canary')
            with self.assertRaises(ValueError):
                gate.pack(stage, allowed, self.context(), Path(root) / 'payload.zip')
            (stage / 'unexpected').unlink()
            (stage / 'GateServer/config.ini.template').write_bytes(b'[Redis]\nPassword=secret-canary\n')
            with self.assertRaises(ValueError):
                gate.pack(stage, allowed, self.context(), Path(root) / 'Chat-1.0.0-windows-x64.zip')
            (stage / 'GateServer/config.ini.template').write_bytes(b'[Redis]\nPassword=\n')
            (stage / 'proto/chat.proto').write_bytes(b'ghp_' + b'a' * 36)
            with self.assertRaises(ValueError):
                gate.pack(stage, allowed, self.context(), Path(root) / 'Chat-1.0.0-windows-x64.zip')

    def test_R01_BUILD_13(self):
        gate.validate_sbom(dict(status=gate.FALLBACK, reason='No approved generator',
                                policy='tests/plans/PHASE-RELEASE-PLAN.md#53'))
        with self.assertRaises(ValueError):
            gate.validate_sbom(dict(status='COMPLETE', reason='No approved generator', policy='policy'))
        with self.assertRaises(ValueError):
            gate.validate_sbom(dict(status=gate.FALLBACK, reason='', policy=''))
        with tempfile.TemporaryDirectory() as root:
            stage = Path(root) / 'stage'
            allowed = self.payload(stage)
            (stage / 'dependency-inventory.json').write_bytes(b'{}')
            with self.assertRaises(ValueError):
                gate.pack(stage, allowed, self.context(), Path(root) / 'Chat-1.0.0-windows-x64.zip')

    def artifact(self):
        return dict(id=99, name='release-candidate-1.0.0-' + 'a' * 40, digest='sha256:' + 'd' * 64,
                    expired=False, created_at='2026-09-14T12:00:00Z', expires_at='2026-10-14T12:00:00Z',
                    workflow_run={'id': 12, 'head_sha': 'a' * 40})

    def test_R01_BUILD_14(self):
        gate.select_artifact([self.artifact()], self.identity())
        for artifacts in ([], [self.artifact(), self.artifact()], [{**self.artifact(), 'expired': True}]):
            with self.assertRaises(ValueError):
                gate.select_artifact(artifacts, self.identity())

    def test_R01_BUILD_15(self):
        with tempfile.TemporaryDirectory() as root:
            archive = Path(root) / 'artifact.zip'
            archive.write_bytes(b'real downloaded bytes')
            with self.assertRaises(ValueError):
                gate.verify_download(archive, self.artifact())
            record = {**self.artifact(), 'digest': 'sha256:' + gate.digest(archive.read_bytes())}
            gate.verify_download(archive, record)
            with self.assertRaises(ValueError):
                gate.select_artifact([{**record, 'expires_at': '2026-09-15T12:00:00Z'}], self.identity())

    def test_R01_BUILD_16(self):
        with tempfile.TemporaryDirectory() as root:
            stage = Path(root) / 'stage'
            allowed = self.payload(stage)
            payload = Path(root) / 'Chat-1.0.0-windows-x64.zip'
            hashes = gate.pack(stage, allowed, self.context(), payload)
            import zipfile
            archive = Path(root) / 'uploaded.zip'
            with zipfile.ZipFile(archive, 'w') as stream:
                stream.write(payload, payload.name)
                stream.writestr('payload-hashes.json', gate.json_bytes(hashes))
            artifact = {**self.artifact(), 'digest': 'sha256:' + gate.digest(archive.read_bytes())}
            evidence = gate.seal(self.identity(), artifact, archive)
            gate.validate_evidence(evidence)
            for field, value in [('redownloadVerified', False), ('attestation', 'VERIFIED'),
                                 ('deploymentId', 0), ('format', True)]:
                with self.assertRaises(ValueError):
                    gate.validate_evidence({**evidence, field: value})
            with self.assertRaises(ValueError):
                gate.validate_evidence({**evidence, 'extra': 'unapproved'})
        from verify_ci import verify_junit
        from verify_ci import reserved_run
        record = {'environment': 'candidate-1.0.0', 'sha': 'a' * 40, 'payload': {'identity': self.identity()}}
        self.assertEqual(reserved_run([record], 'devzyz/Chat', 'a' * 40), 12)
        self.assertIsNone(reserved_run([], 'devzyz/Chat', 'a' * 40))
        for records in ([record, record], [{**record, 'environment': 'candidate-1.0.1'}]):
            with self.assertRaises(ValueError):
                reserved_run(records, 'devzyz/Chat', 'a' * 40)
        good = b'<testsuite tests="1"><testcase name="R01-BUILD-01"/></testsuite>'
        verify_junit(good, ['R01-BUILD-01'])
        for data in [good.replace(b'tests="1"', b'tests="2"'),
                     good.replace(b'01', b'02'),
                     good.replace(b'/>', b'><skipped/></testcase>'),
                     good.replace(b'/>', b'><failure/></testcase>')]:
            with self.assertRaises(ValueError):
                verify_junit(data, ['R01-BUILD-01'])
        from build_candidate import hosted_root
        import os
        if os.environ.get('GITHUB_ACTIONS') != 'true':
            with self.assertRaises(ValueError):
                hosted_root()


if __name__ == '__main__':
    unittest.main()
