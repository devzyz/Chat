import copy
import json
from pathlib import Path
import tempfile
import unittest

from gate import aggregate, registered_groups


class GateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.services = self.root / 'services'
        self.compat = self.root / 'compat'
        self.services.mkdir()
        self.compat.mkdir()
        self.sha = 'a' * 40
        self.groups = registered_groups()
        import hashlib
        reports = []
        for group in self.groups:
            cases = [{'id': f'{group["prefix"]}{index:02}', 'name': 'contract', 'pass': True}
                     for index in range(1, group['expected'] + 1)]
            rows = ''.join(f'<testcase name="{case["id"]} contract"/>' for case in cases)
            data = f'<testsuite tests="{len(cases)}" failures="0">{rows}</testsuite>'.encode()
            (self.services / group['file']).write_bytes(data)
            reports.append({**group, 'cases': cases, 'sha256': hashlib.sha256(data).hexdigest(),
                            'owner': 'tests/services', 'level': 'Integration', 'deadlineSeconds': 690})
        self.manifest = {'format': 1, 'sourceSha': self.sha, 'selector': '3C-07', 'reports': reports}
        self.save('phase3c-reports.json', self.manifest)
        self.save('teardown.json', {'complete': True, 'processComplete': True, 'primaryFailure': None})
        self.save('process-teardown.json', {'complete': True})
        self.save('redaction.json', {'complete': True})
        self.matrix = {'format': 1, 'sourceSha': self.sha, 'releaseEligible': False,
                       'publishedReleaseIds': [], 'schemaManifestSha256': hashlib.sha256(
                           (Path(__file__).resolve().parents[2] / 'schema' / 'manifest.json').read_bytes()).hexdigest(),
                       'status': 'BOOTSTRAP_NO_PROMOTED_N_MINUS_1',
                       'entries': [{'id': f'T10-COMPAT-{i:02}', 'status': 'BOOTSTRAP_NO_PROMOTED_N_MINUS_1',
                                    'executed': False} for i in range(1, 6)]}
        (self.compat / 'compatibility.json').write_text(json.dumps(self.matrix), encoding='utf-8')
        (self.compat / 'linux_compatibility.xml').write_text(
            '<testsuite tests="5" skipped="5">' + ''.join(
                f'<testcase name="T10-COMPAT-{i:02}"><skipped/></testcase>' for i in range(1, 6)) +
            '</testsuite>', encoding='utf-8')
        self.jobs = {name: 'success' for name in ['linux-preflight', 'posix-process-contracts', 'disposable-services']}

    def save(self, name, value):
        (self.services / name).write_text(json.dumps(value), encoding='utf-8')

    def run_gate(self):
        return aggregate(self.services, self.compat, self.root / 'output', self.sha, self.jobs, self.groups)

    def test_current_n_pass_keeps_runtime_compatibility_blocked(self):
        result = self.run_gate()
        self.assertTrue(result['currentNPass'])
        self.assertFalse(result['releaseEligible'])
        self.assertEqual(result['caseCount'], sum(group['expected'] for group in self.groups))

    def test_missing_or_modified_report_fails(self):
        (self.services / self.groups[0]['file']).write_text('<testsuite tests="0"/>')
        self.assertFalse(self.run_gate()['currentNPass'])
        (self.services / self.groups[0]['file']).unlink()
        self.assertFalse(self.run_gate()['currentNPass'])

    def test_failure_nodes_cannot_hide_behind_zero_summary_counts(self):
        import hashlib
        report = self.manifest['reports'][0]
        filename = self.services / report['file']
        data = filename.read_bytes().replace(b'/></testsuite>', b'><failure/></testcase></testsuite>')
        filename.write_bytes(data)
        report['sha256'] = hashlib.sha256(data).hexdigest()
        self.save('phase3c-reports.json', self.manifest)
        self.assertFalse(self.run_gate()['currentNPass'])

    def test_real_report_writer_and_gate_share_one_registration(self):
        import os
        import subprocess
        script = ('const r=require("./tests/services/serviceReports");'
                  'const cases=r.reportGroups("3C-07").flatMap(g=>Array.from({length:g.expected},(_,i)=>'
                  '({id:g.prefix+String(i+1).padStart(2,"0"),name:"contract",pass:true})));'
                  'r.writeReports(process.argv[1],"3C-07",cases);')
        subprocess.run(['node', '-e', script, str(self.services)], check=True, timeout=10,
                       cwd=Path(__file__).resolve().parents[2],
                       env={**os.environ, 'CHAT_CANDIDATE_SHA': self.sha})
        self.assertTrue(self.run_gate()['currentNPass'])

    def test_missing_registration_cannot_reduce_required_reports(self):
        self.manifest['reports'].pop()
        self.save('phase3c-reports.json', self.manifest)
        self.assertFalse(self.run_gate()['currentNPass'])

    def test_other_sha_and_failed_upstream_are_rejected(self):
        self.manifest['sourceSha'] = 'b' * 40
        self.save('phase3c-reports.json', self.manifest)
        self.assertFalse(self.run_gate()['currentNPass'])
        self.manifest['sourceSha'] = self.sha
        self.save('phase3c-reports.json', self.manifest)
        self.jobs['linux-preflight'] = 'failure'
        self.assertFalse(self.run_gate()['currentNPass'])

    def test_cleanup_and_redaction_failures_are_not_ignored(self):
        for name in ['teardown.json', 'process-teardown.json', 'redaction.json']:
            with self.subTest(name=name):
                original = (self.services / name).read_bytes()
                self.save(name, {'complete': False})
                self.assertFalse(self.run_gate()['currentNPass'])
                (self.services / name).write_bytes(original)

    def test_fabricated_compatibility_pass_cannot_close_bootstrap(self):
        changed = copy.deepcopy(self.matrix)
        changed['entries'][0]['status'] = 'SUPPORTED_PASS'
        (self.compat / 'compatibility.json').write_text(json.dumps(changed), encoding='utf-8')
        self.assertFalse(self.run_gate()['currentNPass'])

    def test_cli_propagates_failure_and_writes_failed_gate_evidence(self):
        import subprocess
        import sys
        command = [sys.executable, str(Path(__file__).with_name('gate.py')),
                   '--services', str(self.services), '--compatibility', str(self.compat),
                   '--output', str(self.root / 'output'), '--source-sha', self.sha,
                   '--jobs', json.dumps(self.jobs)]
        self.assertEqual(subprocess.run(command, timeout=15).returncode, 0)
        (self.services / 'teardown.json').unlink()
        self.assertEqual(subprocess.run(command, timeout=15).returncode, 1)
        result = json.loads((self.root / 'output' / 'gate.json').read_text())
        self.assertFalse(result['currentNPass'])
        self.assertIn('<failure', (self.root / 'output' / 'junit' / 'linux_phase3c_gate.xml').read_text())


if __name__ == '__main__':
    unittest.main()
