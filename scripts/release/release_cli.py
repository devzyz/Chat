"""Public release entry point; unsupported publication operations fail closed."""
import argparse
import json
from pathlib import Path
import sys

from release_gate import json_bytes, require, verify_candidate


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('task', choices=['PreflightCandidate', 'VerifyCandidate', 'PackCandidate',
                                        'SealCandidate', 'VerifyPlanTask', 'RegisterCandidate', 'BuildCandidate',
                                        'VerifyUpload', 'CleanupCandidate', 'VerifyCiEvidence'])
    parser.add_argument('--input')
    parser.add_argument('--output')
    parser.add_argument('--plan-task')
    parser.add_argument('--version')
    parser.add_argument('--admission-run', type=int)
    parser.add_argument('--ci-options-file')
    args = parser.parse_args()
    ci_options = None if args.ci_options_file is None else json.loads(Path(args.ci_options_file).read_text())
    if args.task == 'VerifyCiEvidence':
        from verify_ci import verify
        require(ci_options is not None, 'ci-verification-parameters-required')
        print(json.dumps(verify(ci_options)))
        return
    if args.task == 'VerifyPlanTask':
        require(args.plan_task in ('R-00-T1', 'R-00-T2', 'R-00-T3'), 'unimplemented-plan-task')
        if args.plan_task != 'R-00-T1':
            from verify_ci import verify
            profile = 'CandidateBuild' if args.plan_task == 'R-00-T2' else 'CandidateUpload'
            require(ci_options is not None and ci_options.get('evidenceProfile') == profile,
                    'plan-task-requires-hosted-evidence-profile')
            print(json.dumps(verify(ci_options)))
            return
        import subprocess
        test = Path(__file__).resolve().parents[2] / 'tests/release/contracts/run_contracts.py'
        result = subprocess.run([sys.executable, str(test)], timeout=120, check=False)
        require(result.returncode == 0, 'release-contract-tests-failed')
        return
    if args.task in ('RegisterCandidate', 'BuildCandidate', 'VerifyUpload', 'CleanupCandidate'):
        from build_candidate import build, hosted_root, register
        if args.task == 'RegisterCandidate':
            require(args.version is not None and args.admission_run is not None, 'candidate-inputs-required')
            register(args.version, args.admission_run)
        elif args.task == 'BuildCandidate':
            result = build()
            with (hosted_root() / 'candidate-build.json').open('xb') as stream:
                stream.write(json_bytes(result))
        elif args.task == 'VerifyUpload':
            from github_release import GitHub
            from release_gate import seal, select_artifact
            import tempfile
            root = hosted_root()
            receipt = json.loads((root / 'receipt.json').read_text())
            api = GitHub(receipt['identity']['repository'])
            artifacts = api.pages('actions/runs/' + str(receipt['identity']['runId']) + '/artifacts', 'artifacts')
            artifact = select_artifact(artifacts, receipt['identity'])
            import os
            require(str(artifact['id']) == os.environ.get('EXPECTED_ARTIFACT_ID') and
                    artifact['digest'] == 'sha256:' + os.environ.get('EXPECTED_ARTIFACT_DIGEST', ''),
                    'upload-action-api-identity-mismatch')
            with tempfile.TemporaryDirectory(prefix='download-', dir=root) as temporary:
                archive = Path(temporary) / 'candidate.zip'
                api.download(artifact, archive)
                evidence = seal(receipt['identity'], artifact, archive)
            evidence_root = root / 'upload-evidence'
            evidence_root.mkdir()
            (evidence_root / 'candidate-evidence.json').write_bytes(json_bytes(evidence))
            from release_gate import read_archive
            payload = root / 'candidate' / evidence['payload']['payloadName']
            (evidence_root / 'release-manifest.json').write_bytes(read_archive(payload)['release-manifest.json'])
            import shutil
            report = evidence_root / 'build/test-results/release_candidate_build.xml'
            report.parent.mkdir(parents=True)
            shutil.copyfile(root / 'build-evidence/build/test-results/release_candidate_build.xml', report)
            api.request('deployments/' + str(receipt['deploymentId']) + '/statuses', body={
                'state': 'success', 'description': 'Candidate bytes verified; not UAT approved or promoted',
                'auto_inactive': False,
                'log_url': 'https://github.com/' + receipt['identity']['repository'] + '/actions/runs/' +
                           str(receipt['identity']['runId'])})
        else:
            import shutil
            root = hosted_root()
            if root.exists():
                require(root.resolve() == root and not root.is_symlink(), 'unsafe-cleanup-root')
                for path in root.rglob('*'):
                    require(not path.lstat().st_file_attributes & 0x400, 'linked-cleanup-entry')
                shutil.rmtree(root)
            print('Release cleanup: PASS')
        return
    require(args.input is not None, 'input-file-required')
    require(args.output is not None, 'output-file-required')
    require(not Path(args.output).exists(), 'evidence-output-already-exists')
    value = json.loads(Path(args.input).read_text(encoding='utf-8-sig'))
    if args.task == 'PreflightCandidate':
        from github_release import GitHub, preflight
        result = preflight(GitHub(value['identity']['repository']), value['identity'], value['admissionRunId'])
    elif args.task == 'VerifyCandidate':
        result = verify_candidate(value['payloadPath'], value['payloadSha256'], value['manifestSha256'])
    elif args.task == 'PackCandidate':
        from release_gate import pack
        result = pack(value['stage'], value['allowed'], value['context'], value['payloadPath'])
    else:
        from release_gate import seal
        result = seal(value['identity'], value['artifact'], value['archivePath'])
    # Evidence cannot replace an earlier result or rewrite candidate bytes.
    with Path(args.output).open('xb') as stream:
        stream.write(json_bytes(result))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        # Only our category strings are safe to print; parser/OS errors may contain user data or paths.
        message = str(error) if type(error) is ValueError else 'invalid-or-unavailable-release-input'
        print('Release gate: ' + message, file=sys.stderr)
        sys.exit(1)
