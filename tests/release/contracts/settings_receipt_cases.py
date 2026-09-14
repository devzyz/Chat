"""Owner dispatch authentication and admission-time expiry regression cases."""
import copy
import json
from unittest.mock import patch


def check_receipts(test):
    from settings_receipt import create_receipt, verify_receipt, validate_settings
    from release_gate import CHECKS
    identity = test.identity()
    identity['workflowSha'] = identity['sourceSha']
    names = ('release-uat', 'release-promotion')
    settings = {
        'master': {'sha': identity['sourceSha'], 'protected': True},
        'retention': {'days': 90, 'maximum_allowed_days': 90},
        'environments': [{'name': name, 'can_admins_bypass': False,
                          'deployment_branch_policy': {'custom_branch_policies': True, 'protected_branches': False},
                          'protection_rules': [{'type': 'required_reviewers', 'prevent_self_review': False,
                                                'reviewers': [{'type': 'User', 'reviewer': {'id': 99313511}}]}]}
                         for name in names],
        'policies': {name: [{'name': 'master', 'type': 'branch'}, {'name': 'v*', 'type': 'tag'}] for name in names},
        'protection': {'required_status_checks': {'strict': True, 'checks': [{'context': c, 'app_id': 15368} for c in CHECKS]},
                       'enforce_admins': {'enabled': True}, 'allow_force_pushes': {'enabled': False},
                       'allow_deletions': {'enabled': False}, 'required_conversation_resolution': {'enabled': True},
                       'required_pull_request_reviews': {'required_approving_review_count': 0,
                                                        'dismiss_stale_reviews': True, 'require_code_owner_reviews': False}},
    }
    validate_settings(settings)
    for key in ('allow_force_pushes', 'allow_deletions', 'enforce_admins', 'required_conversation_resolution'):
        bad = copy.deepcopy(settings)
        bad['protection'][key]['enabled'] = not bad['protection'][key]['enabled']
        with test.subTest(policy=key), test.assertRaises(ValueError):
            validate_settings(bad)
    for key, value in [('prevent_self_review', True), ('reviewers', [])]:
        bad = copy.deepcopy(settings)
        bad['environments'][0]['protection_rules'][0][key] = value
        with test.subTest(policy=key), test.assertRaises(ValueError):
            validate_settings(bad)
    # Spy through the real validator so receipt tests still cover the policy boundary.
    with patch('settings_receipt.validate_settings', wraps=validate_settings):
        receipt = create_receipt(identity['repository'], identity['sourceSha'], identity['version'], settings, now=1000)
        event = {'inputs': {'settings_receipt': json.dumps(receipt)},
                 'ref': 'master', 'sender': {'id': 99313511}}
        run = {'id': 12, 'run_attempt': 1, 'event': 'workflow_dispatch', 'head_branch': 'master',
               'head_sha': identity['sourceSha'], 'path': '.github/workflows/release.yml',
               'repository': {'full_name': 'devzyz/Chat'},
               'actor': {'id': 99313511}, 'triggering_actor': {'id': 99313511}}
        verify_receipt(receipt, identity, run, event, [], now=1001)
        for key, value in [('repository', 'other/Chat'), ('sourceSha', 'c' * 40), ('releaseVersion', '1.0.1'),
                           ('settingsSha256', '0' * 64), ('dispatchChallenge', 'short'),
                           ('createdAt', 1002), ('expiresAt', 2000), ('actorId', 123)]:
            bad = {**receipt, key: value}
            with test.subTest(receipt=key), test.assertRaises(ValueError):
                verify_receipt(bad, identity, run, event, [], now=1001)
        for now in (999, 1900, 1901):
            with test.subTest(now=now), test.assertRaises(ValueError):
                verify_receipt(receipt, identity, run, event, [], now=now)
        for key, value in [('actor', {'id': 123}), ('triggering_actor', {'id': 123}),
                           ('event', 'push'), ('run_attempt', 2), ('id', 13), ('head_branch', 'develop'),
                           ('head_sha', 'c' * 40), ('path', '.github/workflows/other.yml'),
                           ('repository', {'full_name': 'other/Chat'})]:
            with test.subTest(run=key), test.assertRaises(ValueError):
                verify_receipt(receipt, identity, {**run, key: value}, event, [], now=1001)
        for event_bad in ({**event, 'sender': {'id': 123}}, {**event, 'inputs': {}},
                          {**event, 'ref': 'develop'}):
            with test.assertRaises(ValueError):
                verify_receipt(receipt, identity, run, event_bad, [], now=1001)
        replay = [{'payload': {'settingsChallenge': receipt['dispatchChallenge']}}]
        with test.assertRaises(ValueError):
            verify_receipt(receipt, identity, run, event, replay, now=1001)
        changed = copy.deepcopy(receipt)
        changed['settings']['retention']['days'] = 1
        with test.assertRaises(ValueError):
            verify_receipt(changed, identity, run, event, [], now=1001)
