"""Bounded GitHub API adapter. Credentials stay in gh's environment, never in evidence."""
import json
import re
from pathlib import Path
import subprocess
import tempfile
import time
from urllib.parse import quote

from release_gate import CHECKS, admit, digest, json_bytes, require, read_archive, verify_download


class GitHub:
    def __init__(self, repository):
        import re
        require(re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository) is not None, 'invalid-repository')
        self.base = 'repos/' + repository
        self.deadline = None

    def request(self, endpoint, *, body=None, binary=False):
        command = ['gh', 'api', self.base + '/' + endpoint, '-H', 'X-GitHub-Api-Version: 2022-11-28']
        if body is not None:
            command += ['--method', 'POST', '--input', '-']
        timeout = 120 if self.deadline is None else min(120, self.deadline - time.monotonic())
        require(timeout > 0, 'ci-evidence-timeout')
        result = subprocess.run(command, input=None if body is None else json_bytes(body),
                                capture_output=True, timeout=timeout, check=False)
        # gh errors can include request data. Return a category instead of raw stderr.
        require(result.returncode == 0, 'github-api-unavailable:' + endpoint.split('?')[0])
        return result.stdout if binary else json.loads(result.stdout)

    def pages(self, endpoint, key=None):
        items = []
        separator = '&' if '?' in endpoint else '?'
        for page in range(1, 1001):
            response = self.request(endpoint + separator + f'per_page=100&page={page}')
            batch = response if key is None else response[key]
            require(isinstance(batch, list), 'malformed-api-page')
            items.extend(batch)
            if len(batch) < 100:
                return items
        raise ValueError('api-pagination-limit')

    def download(self, artifact, destination):
        require(type(artifact.get('id')) is int and artifact['id'] > 0, 'invalid-artifact-id')
        data = self.request(f"actions/artifacts/{artifact['id']}/zip", binary=True)
        with Path(destination).open('xb') as stream:
            stream.write(data)
        verify_download(destination, artifact)


def settings_blockers(environments, policies, retention, master):
    blockers = []
    for name in ('release-uat', 'release-promotion'):
        matches = [e for e in environments if e.get('name') == name]
        if len(matches) != 1:
            blockers.append(name + ':missing-environment')
            continue
        environment = matches[0]
        rules = environment.get('protection_rules', [])
        reviewers = [r for rule in rules if rule.get('type') == 'required_reviewers'
                     for r in rule.get('reviewers', [])]
        if (not reviewers or not all(r.get('type') in ('User', 'Team') and
                type(r.get('reviewer', {}).get('id')) is int and r['reviewer']['id'] > 0 for r in reviewers)
                or environment.get('can_admins_bypass') is not False):
            blockers.append(name + ':reviewers-or-admin-bypass')
        branch_policy = environment.get('deployment_branch_policy') or {}
        actual = {(p.get('name'), p.get('type')) for p in policies.get(name, [])}
        if (branch_policy.get('custom_branch_policies') is not True or
                branch_policy.get('protected_branches') is not False or
                actual != {('master', 'branch'), ('v*', 'tag')}):
            blockers.append(name + ':branch-tag-policy')
    if retention.get('days', 0) < 30 or retention.get('maximum_allowed_days', 0) < 30:
        blockers.append('artifact-retention-below-30-days')
    if master.get('protected') is not True:
        blockers.append('master-not-protected')
    return blockers


def preflight(api, identity, admission_run):
    from release_gate import validate_identity
    validate_identity(identity)
    require(type(admission_run) is int and admission_run > 0, 'invalid-admission-run')
    run = api.request(f'actions/runs/{admission_run}')
    require(run.get('run_attempt') == 1 and run.get('status') == 'completed' and
            run.get('conclusion') == 'success' and run.get('path') == '.github/workflows/linux-ci.yml' and
            run.get('head_sha') == identity['sourceSha'], 'upstream-run-not-admitted')
    artifacts = api.pages(f'actions/runs/{admission_run}/artifacts', 'artifacts')
    matches = [a for a in artifacts if a.get('name') == 'phase3d-gate-evidence' and a.get('expired') is False]
    require(len(matches) == 1, 'upstream-artifact-missing-or-ambiguous')
    with tempfile.TemporaryDirectory(prefix='chat-release-admission-') as root:
        archive = Path(root) / 'admission.zip'
        api.download(matches[0], archive)
        files = read_archive(archive, max_bytes=64 * 1024**2)
    require('release-admission.json' in files and 'phase3d-reports.json' in files, 'upstream-evidence-missing')
    admission = json.loads(files['release-admission.json'])
    require(admission.get('manifests', {}).get('phase3d') == digest(files['phase3d-reports.json']),
            'upstream-manifest-drift')
    checks = api.pages(f"commits/{identity['sourceSha']}/check-runs", 'check_runs')
    require(all(c.get('app', {}).get('slug') == 'github-actions' for c in checks if c.get('name') in CHECKS),
            'untrusted-required-check-producer')
    deployments = api.pages('deployments')
    admit(identity, admission, checks, deployments)
    checked_runs = set()
    for name in CHECKS:
        check = max((c for c in checks if c.get('name') == name and c.get('head_sha') == identity['sourceSha']),
                    key=lambda c: c['id'])
        match = re.fullmatch(r'https://github\.com/' + re.escape(identity['repository']) +
                             r'/actions/runs/([0-9]+)/job/[0-9]+', check.get('details_url', ''))
        require(match is not None, 'required-check-run-unbound')
        run_id = int(match.group(1))
        if run_id not in checked_runs:
            check_run = api.request('actions/runs/' + str(run_id))
            require(check_run.get('head_sha') == identity['sourceSha'] and check_run.get('run_attempt') == 1 and
                    check_run.get('conclusion') == 'success', 'required-check-run-not-first-attempt-success')
            checked_runs.add(run_id)
    environments = api.pages('environments', 'environments')
    policies = {}
    for name in ('release-uat', 'release-promotion'):
        if any(e.get('name') == name for e in environments):
            policies[name] = api.pages('environments/' + quote(name) + '/deployment-branch-policies', 'branch_policies')
    retention = api.request('actions/permissions/artifact-and-log-retention')
    blockers = settings_blockers(environments, policies, retention, api.request('branches/master'))
    require(not blockers, 'release-settings-blocked:' + ','.join(blockers))
    protection = api.request('branches/master/protection')
    status_checks = protection.get('required_status_checks') or {}
    contexts = set(status_checks.get('contexts', [])) | {c.get('context') for c in status_checks.get('checks', [])}
    bound_contexts = {c.get('context') for c in status_checks.get('checks', []) if c.get('app_id') == 15368}
    require(set(CHECKS) <= contexts and status_checks.get('strict') is True and
            set(CHECKS) <= bound_contexts and protection.get('enforce_admins', {}).get('enabled') is True,
            'master-required-check-policy-missing')
    return {'identity': identity, 'upstream': admission['manifests'],
            'compatibility': admission['compatibility'], 'admissionRunId': admission_run,
            'admissionArtifactId': matches[0]['id'], 'admissionArtifactDigest': matches[0]['digest']}


def reserve(api, identity):
    # Workflow concurrency is the transaction lock. Recheck history immediately before POST.
    from release_gate import check_unused
    check_unused(identity, api.pages('deployments'))
    record = api.request('deployments', body={
        'ref': identity['sourceSha'], 'environment': 'candidate-' + identity['version'],
        # The public register operation has already validated actual Check Runs, including producer/run identity.
        # Deployments' required_contexts refers to legacy commit statuses, not those Check Run names.
        'auto_merge': False, 'required_contexts': [], 'transient_environment': False,
        'production_environment': False, 'description': 'Immutable release build reservation; never reuse version',
        'payload': {'format': 1, 'identity': identity},
    })
    require(type(record.get('id')) is int and record['id'] > 0 and record.get('sha') == identity['sourceSha'],
            'candidate-reservation-failed')
    return record['id']
