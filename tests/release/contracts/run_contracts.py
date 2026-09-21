"""Emit actual unittest cases as JUnit; failures are never converted into expected RED success."""
from pathlib import Path
import sys
import json
import unittest
import xml.etree.ElementTree as ET


class Result(unittest.TextTestResult):
    def startTest(self, test):
        super().startTest(test)
        name = test._testMethodName.removeprefix('test_').replace('_', '-')
        self.case = ET.SubElement(self.xml, 'testcase', name=name, classname='ReleaseContracts')

    def addFailure(self, test, error):
        super().addFailure(test, error)
        ET.SubElement(self.case, 'failure', message='contract-assertion-failed')

    def addError(self, test, error):
        super().addError(test, error)
        ET.SubElement(self.case, 'error', message='contract-execution-error')

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        ET.SubElement(self.case, 'skipped', message='unexpected-skip')

    def addSubTest(self, test, subtest, error):
        super().addSubTest(test, subtest, error)
        if error is not None:
            ET.SubElement(self.case, 'failure', message='contract-subtest-failed')


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[3]
    report = root / 'build/test-results/release_candidate_contracts.xml'
    report.parent.mkdir(parents=True, exist_ok=True)
    report.unlink(missing_ok=True)
    from test_release import ReleaseContracts
    Result.xml = ET.Element('testsuite', name='release-candidate-contracts')
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ReleaseContracts)
    result = unittest.TextTestRunner(resultclass=Result).run(suite)
    for key, count in [('tests', result.testsRun), ('failures', len(result.failures)),
                       ('errors', len(result.errors)), ('skipped', len(result.skipped))]:
        Result.xml.set(key, str(count))
    ET.ElementTree(Result.xml).write(report, encoding='utf-8', xml_declaration=True)
    sys.exit(0 if result.wasSuccessful() and result.testsRun > 0 and not result.skipped else 1)
