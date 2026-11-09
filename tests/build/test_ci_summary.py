"""Exercise the same JUnit reader used by the hosted Windows and Linux jobs."""

import tempfile
import unittest
import json
import re
from pathlib import Path
from scripts.ci.test_summary import render


class SummaryTests(unittest.TestCase):
    """Cover report formats and failures which can occur before a report exists."""

    def setUp(self):
        """Allocate a separate workspace for each simulated run."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def summary(self):
        """Use the production glob, URL and module grouping behavior."""
        return render(self.root, ["*.xml"], "https://example.test/job/1",
                      "https://example.test/artifacts/2", "https://example.test/blob/sha")

    def test_shared_google_test_report(self):
        """Link the failing assertion and retain both successful and failed module counts."""
        source = self.root / "tests/server/gate-request/example.cpp"
        source.parent.mkdir(parents=True)
        source.touch()
        (self.root / "server_component.xml").write_text(
            '<testsuites><testsuite><testcase classname="StatusRoutingTests" name="route"/>'
            '<testcase classname="Endpoints/Login" name="WrongPassword" '
            'file="tests/server/gate-request/example.cpp" line="20">'
            '<failure message="tests/server/gate-request/example.cpp:25&#10;Expected: 1&#10;Actual: 2"/>'
            '</testcase></testsuite></testsuites>', encoding="utf-8")
        summary = self.summary()
        for label in ("StatusServer", "GateServer", "Endpoints/Login.WrongPassword", "example.cpp#L25",
                      "Expected: 1", "Actual: 2", "job/1", "artifacts/2"):
            self.assertIn(label, summary)

    def test_formats_and_independent_errors(self):
        """Show skipped CTest, service cleanup, Node error, and invalid XML independently."""
        reports = {
            "client_unit.xml": '<testcase name="model" status="notrun"/>',
            "varify_unit.xml": '<testcase name="mail"><error message="secret: private credential"/></testcase>',
            "linux_phase3d_recovery.xml": '<testcase name="E03-RECOVER-05 &lt;fault&gt;">'
                                         '<failure message="stop-ChatA:deadline"/></testcase>',
        }
        for name, case in reports.items():
            (self.root / name).write_text(f"<testsuite>{case}</testsuite>", encoding="utf-8")
        (self.root / "broken.xml").write_text("<testsuite>", encoding="utf-8")
        summary = self.summary()
        for label in ("Qt Client", "notrun", "VarifyServer", "Two-server E2E", "stop-ChatA:deadline",
                      "&lt;fault&gt;", "broken.xml", "ParseError"):
            self.assertIn(label, summary)
        self.assertNotIn("private credential", summary)

    def test_build_failure_without_junit(self):
        """Report an evidence gap instead of implying that zero tests passed."""
        summary = self.summary()
        self.assertIn("No testcase evidence", summary)
        self.assertIn("job/1", summary)
        self.assertNotIn("Reported cases by module", summary)

    def test_e2e_failure_links_its_registered_scenario(self):
        """Locate the exact business scenario rather than linking a generic runner."""
        source = self.root / "tests/services/faultRecoveryCases.js"
        source.parent.mkdir(parents=True)
        source.write_text("// scenario\nrecord('E03-RECOVER-07', retry);\n", encoding="utf-8")
        (self.root / "linux_phase3d_recovery.xml").write_text(
            '<testsuite><testcase name="E03-RECOVER-07 retry"><failure message="bounded contract failed"/>'
            '</testcase></testsuite>', encoding="utf-8")
        self.assertIn("faultRecoveryCases.js#L2", self.summary())

    def test_native_diagnostics_point_to_file_and_line(self):
        """Check the registered matchers against typical build output from both toolchains."""
        path = Path(__file__).resolve().parents[2] / "scripts/ci/compiler-matchers.json"
        matchers = json.loads(path.read_text(encoding="utf-8"))["problemMatcher"]
        samples = ["  4>chat.cpp(47,21): error C2065: undeclared identifier",
                   "chat.cpp:47:21: error: missing type"]
        for matcher, sample in zip(matchers, samples, strict=True):
            pattern = matcher["pattern"][0]
            match = re.search(pattern["regexp"], sample)
            self.assertEqual(match[pattern["file"]], "chat.cpp")
            self.assertEqual(match[pattern["line"]], "47")
