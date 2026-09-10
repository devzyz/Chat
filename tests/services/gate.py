"""Aggregate one workflow's real evidence; never rerun or invent service cases."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET


def registered_groups():
    script = 'process.stdout.write(JSON.stringify(require("./tests/services/serviceReports").reportGroups("3C-07")))'
    return json.loads(subprocess.check_output(['node', '-e', script],
                      cwd=Path(__file__).resolve().parents[2], timeout=10, text=True))


def require(condition, category):
    if not condition:
        raise ValueError(category)


def read(root, name):
    target = (root / name).resolve()
    require(target.is_relative_to(root.resolve()) and target.is_file(), 'missing-or-unsafe-evidence')
    require(target.stat().st_size <= 2 * 1024 * 1024, 'evidence-size-limit')
    return target.read_bytes()


def document(root, name):
    return json.loads(read(root, name))


def xml_document(data):
    require(b'<!DOCTYPE' not in data and b'<!ENTITY' not in data, 'xml-declarations-forbidden')
    tree = ET.fromstring(data)
    require(tree.tag == 'testsuite', 'invalid-junit-root')
    return tree


def aggregate(services, compatibility, output, source_sha, jobs, groups):
    output.mkdir(parents=True, exist_ok=True)
    (output / 'junit').mkdir(exist_ok=True)
    result = {'format': 1, 'sourceSha': source_sha, 'currentNPass': False,
              'releaseEligible': False, 'compatibility': 'UNAVAILABLE', 'caseCount': 0}
    try:
        require(re.fullmatch(r'[a-f0-9]{40}', source_sha) is not None, 'candidate-sha-required')
        require(all(jobs.get(name) == 'success' for name in
                    ['linux-preflight', 'posix-process-contracts', 'disposable-services']), 'upstream-job-failed')
        result['upstreamJobs'] = {name: 'success' for name in
                                  ['linux-preflight', 'posix-process-contracts', 'disposable-services']}
        manifest = document(services, 'phase3c-reports.json')
        require(manifest['format'] == 1 and manifest['selector'] == '3C-07' and
                manifest['sourceSha'] == source_sha, 'manifest-identity-mismatch')
        reports = manifest['reports']
        require([entry['file'] for entry in reports] == [group['file'] for group in groups],
                'required-report-registration-mismatch')
        for report, group in zip(reports, groups):
            require(report['prefix'] == group['prefix'] and report['expected'] == group['expected'] and
                    report['owner'] == 'tests/services' and report['level'] == 'Integration' and
                    report['deadlineSeconds'] == 690, 'report-contract-mismatch')
            expected = [f'{group["prefix"]}{index:02}' for index in range(1, group['expected'] + 1)]
            cases = report['cases']
            require(sorted(case['id'] for case in cases) == expected and
                    all(case['pass'] is True for case in cases), 'missing-duplicate-or-failed-case')
            data = read(services, group['file'])
            require(hashlib.sha256(data).hexdigest() == report['sha256'], 'report-digest-mismatch')
            tree = xml_document(data)
            require(int(tree.get('tests', '-1')) == group['expected'] and
                    all(int(tree.get(key, '0')) == 0 for key in ['failures', 'errors', 'skipped']) and
                    not any(node.tag in ['failure', 'error', 'skipped'] for node in tree.iter()), 'junit-failed')
            require(sorted(node.get('name') for node in tree.findall('testcase')) ==
                    sorted(f'{case["id"]} {case["name"]}' for case in cases), 'junit-case-mismatch')
            (output / 'junit' / group['file']).write_bytes(data)
            result['caseCount'] += len(cases)
        cleanup = document(services, 'teardown.json')
        require(cleanup.get('complete') is True and cleanup.get('processComplete') is True and
                cleanup.get('primaryFailure') is None and
                document(services, 'process-teardown.json').get('complete') is True, 'cleanup-incomplete')
        require(document(services, 'redaction.json').get('complete') is True, 'redaction-incomplete')
        matrix = document(compatibility, 'compatibility.json')
        bootstrap = 'BOOTSTRAP_NO_PROMOTED_N_MINUS_1'
        ids = [f'T10-COMPAT-{index:02}' for index in range(1, 6)]
        require(matrix['format'] == 1 and matrix['sourceSha'] == source_sha and
                matrix['status'] == bootstrap and matrix['releaseEligible'] is False and
                matrix['publishedReleaseIds'] == [] and matrix['schemaManifestSha256'] == hashlib.sha256(
                    (Path(__file__).resolve().parents[2] / 'schema' / 'manifest.json').read_bytes()).hexdigest() and
                [entry['id'] for entry in matrix['entries']] == ids and
                all(entry['status'] == bootstrap and entry['executed'] is False for entry in matrix['entries']),
                'compatibility-review-required')
        compatibility_xml = read(compatibility, 'linux_compatibility.xml')
        tree = xml_document(compatibility_xml)
        require(int(tree.get('tests', '-1')) == 5 and int(tree.get('skipped', '-1')) == 5 and
                [node.get('name') for node in tree.findall('testcase')] == ids and
                all(len(node.findall('skipped')) == 1 for node in tree.findall('testcase')) and
                not any(node.tag in ['failure', 'error'] for node in tree.iter()), 'compatibility-report-mismatch')
        (output / 'junit' / 'linux_compatibility.xml').write_bytes(compatibility_xml)
        (output / 'compatibility.json').write_text(json.dumps(matrix, indent=2), encoding='utf-8')
        (output / 'phase3c-reports.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        result.update(currentNPass=True, compatibility=bootstrap)
    except (OSError, ValueError, KeyError, TypeError, ET.ParseError) as error:
        # Do not copy parser messages or arbitrary evidence fields into logs.
        result['failure'] = str(error) if type(error) is ValueError and re.fullmatch(r'[a-z-]+', str(error)) else 'invalid-evidence'
    (output / 'gate.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    for name in ['teardown.json', 'redaction.json']:
        (output / name).write_text(json.dumps({'complete': result['currentNPass'],
                                             'source': 'validated-service-evidence'}), encoding='utf-8')
    tree = ET.Element('testsuite', name='phase3c-current-n-gate', tests='1',
                      failures='0' if result['currentNPass'] else '1')
    case = ET.SubElement(tree, 'testcase', name='same-candidate-reports-and-cleanup')
    if not result['currentNPass']:
        ET.SubElement(case, 'failure', message=result.get('failure', 'invalid-evidence'))
    ET.ElementTree(tree).write(output / 'junit' / 'linux_phase3c_gate.xml', encoding='utf-8', xml_declaration=True)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for argument in ['services', 'compatibility', 'output', 'source-sha', 'jobs']:
        parser.add_argument('--' + argument, required=True)
    args = parser.parse_args()
    result = aggregate(Path(args.services), Path(args.compatibility), Path(args.output),
                       args.source_sha, json.loads(args.jobs), registered_groups())
    raise SystemExit(0 if result['currentNPass'] else 1)
