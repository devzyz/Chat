"""Verify first-attempt CI evidence against live run, job, artifact and byte identities."""
import json
from pathlib import Path
import re
import tempfile
import time
import xml.etree.ElementTree as ET

from github_release import GitHub
from release_gate import (SHA, digest, exact, public_metadata, read_archive, require, seal,
                          validate_context, validate_evidence, validate_identity)


def verify_junit(data, expected):
    require(len(data) <= 2 * 1024**2 and b'<!DOCTYPE' not in data and b'<!ENTITY' not in data, 'invalid-junit')
    root = ET.fromstring(data)
    require(root.tag == 'testsuite', 'invalid-junit-suite')
    cases = root.findall('testcase')
    names = [case.get('name', '').split(' ')[0] for case in cases]
    require(len(names) > 0 and sorted(names) == sorted(expected) and len(set(names)) == len(names),
            'junit-test-identity-mismatch')
    require(root.get('tests') == str(len(cases)) and all(root.get(key, '0') == '0'
            for key in ('failures', 'errors', 'skipped')), 'junit-count-mismatch')
    require(not any(next(root.iter(tag), None) is not None for tag in ('failure', 'error', 'skipped')),
            'junit-case-not-passed')


def reserved_run(deployments, repository, sha):
    records = [d for d in deployments if str(d.get('environment', '')).startswith('candidate-') and d.get('sha') == sha]
    require(len(records) <= 1, 'ambiguous-source-reservation')
    if not records:
        return None
    payload = records[0].get('payload') or {}
    if isinstance(payload, str):
        payload = json.loads(payload)
    identity = payload.get('identity')
    validate_identity(identity)
    require(identity['repository'] == repository and identity['sourceSha'] == sha and
            records[0]['environment'] == 'candidate-' + identity['version'], 'reservation-identity-mismatch')
    return identity['runId']


def verify(options):
    require(re.fullmatch(SHA, options['headSha']) is not None, 'invalid-source')
    require(options['workflowFile'] == '.github/workflows/release.yml' and
            options['expectedWorkflowName'] == 'Release', 'unexpected-workflow')
    require(options['expectedJobName'] == 'Windows release candidate build', 'unexpected-build-job')
    require(options['evidenceProfile'] in ('CandidateBuild', 'CandidateUpload'), 'unsupported-evidence-profile')
    require(type(options['timeoutSeconds']) is int and 1 <= options['timeoutSeconds'] <= 18000,
            'invalid-evidence-deadline')
    expected_ids = options['expectedTestIds']
    require(isinstance(expected_ids, list) and expected_ids and len(set(expected_ids)) == len(expected_ids),
            'invalid-expected-test-ids')
    api = GitHub(options['repository'])
    api.deadline = time.monotonic() + options['timeoutSeconds']
    while True:
        require(time.monotonic() < api.deadline, 'ci-evidence-timeout')
        runs = api.pages('actions/workflows/release.yml/runs?head_sha=' + options['headSha'], 'workflow_runs')
        # Expired preflight dispatches never reserve/build and must not mask the one actual candidate run.
        build_run = reserved_run(api.pages('deployments'), options['repository'], options['headSha'])
        matching = [run for run in runs if run.get('head_sha') == options['headSha'] and
                    run.get('path') == options['workflowFile'] and run.get('name') == options['expectedWorkflowName']
                    and run.get('event') == 'workflow_dispatch' and run.get('id') == build_run]
        require(len(matching) <= 1, 'ambiguous-release-run')
        if matching:
            run = matching[0]
            require(run.get('run_attempt') == 1, 'retry-forbidden')
            if run.get('status') == 'completed':
                require(run.get('conclusion') == 'success', 'release-run-failed')
                break
        time.sleep(min(5, max(0, api.deadline - time.monotonic())))
    jobs = api.pages('actions/runs/' + str(run['id']) + '/jobs?filter=all', 'jobs')
    jobs = [job for job in jobs if job.get('name') == options['expectedJobName']]
    require(len(jobs) == 1 and jobs[0].get('conclusion') == 'success', 'required-job-not-successful')
    artifacts = api.pages('actions/runs/' + str(run['id']) + '/artifacts', 'artifacts')

    def select(name):
        matches = [a for a in artifacts if a.get('name') == name]
        require(len(matches) == 1 and matches[0].get('expired') is False, 'missing-or-colliding-evidence-artifact')
        artifact = matches[0]
        require(artifact.get('workflow_run', {}).get('id') == run['id'] and
                artifact['workflow_run'].get('head_sha') == options['headSha'], 'evidence-artifact-run-mismatch')
        return artifact

    parent = Path(options['downloadRoot']).resolve()
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='release-ci-', dir=parent) as owned:
        owned = Path(owned)
        primary = select(options['expectedArtifactName'])
        api.download(primary, owned / 'primary.zip')
        primary_files = read_archive(owned / 'primary.zip')
        if options['evidenceProfile'] == 'CandidateBuild':
            require(primary['name'] == 'release-candidate-build-evidence', 'unexpected-build-evidence-name')
            files = primary_files
            build = json.loads(files['candidate-build.json'])
            exact(build, 'identity payload buildInvocations status')
            require(build['status'] == 'BUILT' and type(build['buildInvocations']) is int and
                    build['buildInvocations'] == 1, 'build-invocation-mismatch')
            identity = build['identity']
            require(digest(files['release-manifest.json']) == build['payload']['manifestSha256'],
                    'build-manifest-digest-mismatch')
            manifest = json.loads(files['release-manifest.json'])
            validate_context(manifest['context'])
            require(manifest['context']['identity'] == identity, 'build-manifest-identity-mismatch')
            public_metadata(build)
            bound_artifacts = [primary]
        else:
            require(options.get('expectedCompanionArtifactName') == 'release-candidate-upload-evidence',
                    'missing-upload-companion-name')
            companion = select(options['expectedCompanionArtifactName'])
            api.download(companion, owned / 'companion.zip')
            files = read_archive(owned / 'companion.zip')
            evidence = json.loads(files['candidate-evidence.json'])
            validate_evidence(evidence)
            identity = evidence['identity']
            require(seal(identity, primary, owned / 'primary.zip') == evidence, 'upload-evidence-byte-mismatch')
            require(digest(files['release-manifest.json']) == evidence['payload']['manifestSha256'],
                    'upload-manifest-digest-mismatch')
            bound_artifacts = [primary, companion]
        validate_identity(identity)
        require(set(files) == {'release-manifest.json', options['expectedJUnitPath'],
                              'candidate-build.json' if options['evidenceProfile'] == 'CandidateBuild'
                              else 'candidate-evidence.json'}, 'unexpected-evidence-files')
        require(identity['repository'] == options['repository'] and identity['sourceSha'] == options['headSha'] and
                identity['runId'] == run['id'] and identity['runAttempt'] == 1, 'evidence-run-identity-mismatch')
        verify_junit(files[options['expectedJUnitPath']], expected_ids)
    return {'runId': run['id'], 'runAttempt': 1, 'sourceSha': options['headSha'], 'status': 'VERIFIED',
            'artifacts': [{key: artifact[key] for key in ('id', 'name', 'digest')} for artifact in bound_artifacts]}
