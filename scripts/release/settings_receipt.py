"""Recent owner-reviewed settings, authenticated by GitHub's dispatch actor, not a JSON signature."""
import json
import re
import secrets
import time

from release_gate import SHA, VERSION, digest, exact, json_bytes, require, validate_identity

OWNER_ID = 99313511
REPOSITORY = 'devzyz/Chat'
MAX_AGE = 900


def validate_settings(settings):
    from github_release import settings_blockers
    exact(settings, 'environments policies retention master protection')
    blockers = settings_blockers(settings['environments'], settings['policies'], settings['retention'], settings['master'])
    require(not blockers, 'release-settings-blocked:' + ','.join(blockers))
    from release_gate import CHECKS
    protection = settings['protection']
    checks = protection.get('required_status_checks') or {}
    bound = {c.get('context') for c in checks.get('checks', []) if c.get('app_id') == 15368}
    review = protection.get('required_pull_request_reviews') or {}
    require(set(CHECKS) <= bound and checks.get('strict') is True and
            protection.get('enforce_admins', {}).get('enabled') is True and
            protection.get('allow_force_pushes', {}).get('enabled') is False and
            protection.get('allow_deletions', {}).get('enabled') is False and
            protection.get('required_conversation_resolution', {}).get('enabled') is True and
            review.get('required_approving_review_count') == 0 and review.get('dismiss_stale_reviews') is True and
            review.get('require_code_owner_reviews') is False,
            'master-required-check-policy-missing')
    for environment in settings['environments']:
        rules = [r for r in environment['protection_rules'] if r.get('type') == 'required_reviewers']
        require(len(rules) == 1 and rules[0].get('prevent_self_review') is False and
                [(r.get('type'), r.get('reviewer', {}).get('id')) for r in rules[0].get('reviewers', [])] ==
                [('User', OWNER_ID)], 'release-reviewer-policy-mismatch')


def collect_settings(api):
    from urllib.parse import quote
    names = ('release-uat', 'release-promotion')
    settings = {
        'environments': [api.request('environments/' + quote(name)) for name in names],
        'policies': {name: api.pages('environments/' + quote(name) + '/deployment-branch-policies', 'branch_policies')
                     for name in names},
        'retention': api.request('actions/permissions/artifact-and-log-retention'),
        'master': api.request('branches/master'),
        'protection': api.request('branches/master/protection'),
    }
    # Keep only policy fields; commit messages, author emails and unrelated response metadata are not evidence.
    settings['master'] = {'protected': settings['master']['protected'],
                          'sha': settings['master']['commit']['sha']}
    settings['environments'] = [
        {k: e[k] for k in ('name', 'protection_rules', 'can_admins_bypass', 'deployment_branch_policy')}
        for e in settings['environments']]
    for environment in settings['environments']:
        for rule in environment['protection_rules']:
            if rule.get('type') == 'required_reviewers':
                rule['reviewers'] = [{'type': r['type'], 'reviewer': {'id': r['reviewer']['id']}}
                                     for r in rule['reviewers']]
    settings['policies'] = {name: [{'name': p['name'], 'type': p['type']} for p in policies]
                            for name, policies in settings['policies'].items()}
    validate_settings(settings)
    return settings


def create_receipt(repository, sha, version, settings, *, now=None):
    require(repository == REPOSITORY and re.fullmatch(SHA, sha) is not None and
            re.fullmatch(VERSION, version) is not None, 'invalid-settings-identity')
    validate_settings(settings)
    created = int(time.time()) if now is None else now
    return {'format': 1, 'repository': repository, 'sourceSha': sha, 'releaseVersion': version,
            'actorId': OWNER_ID, 'dispatchChallenge': secrets.token_hex(32),
            'createdAt': created, 'expiresAt': created + MAX_AGE,
            'settingsSha256': digest(json_bytes(settings)), 'settings': settings}


def verify_receipt(receipt, identity, run, event, deployments, *, now=None):
    validate_identity(identity)
    exact(receipt, 'format repository sourceSha releaseVersion actorId dispatchChallenge createdAt expiresAt settingsSha256 settings')
    require(type(receipt['format']) is int and receipt['format'] == 1 and
            receipt['repository'] == identity['repository'] == REPOSITORY and
            receipt['sourceSha'] == identity['sourceSha'] == identity['workflowSha'] and
            receipt['releaseVersion'] == identity['version'] and receipt['actorId'] == OWNER_ID,
            'settings-receipt-identity-mismatch')
    require(run.get('id') == identity['runId'] and run.get('run_attempt') == identity['runAttempt'] == 1 and
            run.get('event') == 'workflow_dispatch' and run.get('head_branch') == 'master' and
            run.get('head_sha') == identity['sourceSha'] and run.get('path') == '.github/workflows/release.yml' and
            run.get('repository', {}).get('full_name') == REPOSITORY and
            run.get('actor', {}).get('id') == OWNER_ID and run.get('triggering_actor', {}).get('id') == OWNER_ID and
            event.get('sender', {}).get('id') == OWNER_ID and event.get('ref') in ('master', 'refs/heads/master'),
            'settings-receipt-untrusted-dispatch')
    require(json.loads(event.get('inputs', {}).get('settings_receipt', 'null')) == receipt,
            'settings-receipt-dispatch-mismatch')
    timestamp = int(time.time()) if now is None else now
    require(type(receipt['createdAt']) is int and type(receipt['expiresAt']) is int and
            receipt['expiresAt'] - receipt['createdAt'] == MAX_AGE and
            receipt['createdAt'] <= timestamp < receipt['expiresAt'], 'settings-receipt-expired-or-future')
    require(isinstance(receipt['dispatchChallenge'], str) and
            re.fullmatch(r'[a-f0-9]{64}', receipt['dispatchChallenge']) is not None,
            'invalid-settings-challenge')
    for deployment in deployments:
        payload = deployment.get('payload') or {}
        if isinstance(payload, str):
            payload = json.loads(payload)
        require(isinstance(payload, dict), 'invalid-deployment-payload')
        require(payload.get('settingsChallenge') != receipt['dispatchChallenge'], 'settings-receipt-already-consumed')
    require(digest(json_bytes(receipt['settings'])) == receipt['settingsSha256'], 'settings-snapshot-drift')
    validate_settings(receipt['settings'])
    require(receipt['settings'].get('master', {}).get('sha') == identity['sourceSha'], 'settings-master-source-mismatch')
    return receipt


def prepare(api, value):
    import subprocess
    require(value.get('repository') == REPOSITORY, 'invalid-settings-repository')
    result = subprocess.run(['gh', 'api', 'user', '--jq', '.id'], capture_output=True, timeout=30, check=False)
    require(result.returncode == 0 and result.stdout.strip() == str(OWNER_ID).encode(), 'settings-owner-login-required')
    started = int(time.time())
    settings = collect_settings(api)
    require(settings['master']['sha'] == value['sourceSha'], 'settings-master-source-mismatch')
    require(type(value.get('admissionRunId')) is int and value['admissionRunId'] > 0, 'invalid-admission-run')
    receipt = create_receipt(value['repository'], value['sourceSha'], value['version'], settings, now=started)
    return {'build_candidate': True, 'release_version': value['version'],
            'admission_run_id': str(value['admissionRunId']), 'settings_receipt': json.dumps(receipt, separators=(',', ':'))}
