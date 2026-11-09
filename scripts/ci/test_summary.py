"""Generate a read-only GitHub summary from the reports produced by one CI lane."""

from collections import Counter
from functools import lru_cache
import html
import os
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET
from urllib.parse import quote


def cell(value):
    """Keep report metadata inside one Markdown table cell."""
    value = re.sub(r"[\x00-\x1f\x7f]+", " ", str(value))[:200]
    return "<code>" + html.escape(value).replace("|", "&#124;") + "</code>"


def source_path(value, workspace):
    """Accept only an existing repository test source, including paths from MSBuild."""
    path = (value or "").replace("\\", "/")
    match = re.search(r"(?:^|/)((?:tests/|chat/tests/|VarifyServer/test/).+)$", path)
    if not match:
        return ""
    relative = match[1]
    if ".." in Path(relative).parts or not (workspace / relative).is_file():
        return ""
    return relative


def module(report, case, source):
    """Use report ownership first, then the source of shared GoogleTest binaries."""
    owners = {
        "server_gate_": "GateServer", "server_status_": "StatusServer",
        "server_chat_": "ChatServer", "server_resource_": "ResourceServer",
        "client_": "Qt Client", "varify_": "VarifyServer", "script_": "Instance scripts",
        "release_candidate_": "Packaging", "linux_migration": "MySQL migration",
        "linux_message": "MySQL message commit", "linux_redis": "Redis",
        "linux_four_process": "Four production services", "linux_first_release": "Four-account E2E",
        "linux_phase3d_": "Two-server E2E", "performance": "Performance",
    }
    for prefix, owner in owners.items():
        if report.startswith(prefix):
            return owner
    if report.startswith("server_"):
        identity = (source + " " + case.get("classname", "")).lower()
        if "gate_status_startup" in identity and "GateGrpc" not in case.get("classname", ""):
            return "GateServer / StatusServer"
        if "gate" in identity:
            return "GateServer"
        if any(part in identity for part in ("status-routing", "status_grpc_transport", "statusrouting")):
            return "StatusServer"
        if any(part in identity for part in ("/chat-session-state/", "/messaging/", "/logic-dispatcher/",
                                              "/message-receipt/", "chat_grpc", "peer_server_routing",
                                              "chat_tcp_transport", "history_response", "server_redis_pool_tests")):
            return "ChatServer"
        return "Shared protocols and runtime"
    return Path(report).stem


def reason(problem):
    """Summarize safe diagnostic codes and numeric comparisons without copying payloads or logs."""
    message = problem.get("message") or problem.text or ""
    if message in {"bounded contract failed", "required cases missing or duplicated", "scenario-failed",
                   "incomplete", "invalid-evidence", "contract-assertion-failed", "service evidence incomplete"}:
        return message
    if re.fullmatch(r"(?:stop|auth|gate|client)-[\w-]+:(?:deadline|assertion|operation-failed|"
                    r"response--?\d+|stop-timeout|graceful-timeout|exit-\d+|harness-incomplete|stop-escalated)", message):
        return message
    values = re.findall(r"(?im)^\s*(expected|actual|which is):\s*(-?\d+(?:\.\d+)?|true|false)\s*$", message)
    return ", ".join(f"{key}: {value}" for key, value in values[:4]) or f"{problem.tag} (see raw report)"


@lru_cache(maxsize=16)
def source_lines(path):
    """Read tracked registration and scenario files only when a case has failed."""
    return path.read_text(encoding="utf-8").splitlines() if path.is_file() else []


def registration(workspace, report, name):
    """Locate named CTest registrations or explicit business IDs missing from JUnit metadata."""
    if report.startswith("client_"):
        files = ["chat/CMakeLists.txt"]
        marker = f"add_test(NAME {name} "
    elif re.match(r"E03-[A-Z]+-\d+\b", name):
        files = [p.relative_to(workspace).as_posix() for p in
                 (workspace / "tests/services").glob("*Cases.js") if not p.is_symlink()]
        marker = "'" + name.split()[0] + "'"
    elif re.fullmatch(r"A02-(?:VAL|LIFE)-\d+", name):
        group = "validation" if "-VAL-" in name else "lifecycle"
        files = [f"tests/scripts/{group}/chatserver-instances.tests.ps1"]
        marker = name
    else:
        return "", ""
    for file in files:
        for line, content in enumerate(source_lines(workspace / file), 1):
            if marker in content:
                return file, str(line)
    return "", ""


def render(workspace, patterns, log_url, artifact_url, source_url):
    """Read only the lane's selected XMLs; retain independent failures and parse errors."""
    counts = Counter()
    rows = []
    errors = []
    paths = sorted({path for pattern in patterns for path in workspace.glob(pattern)})
    for path in paths:
        try:
            root = ET.parse(path).getroot()
        except (OSError, ET.ParseError) as exc:
            errors.append(f"{cell(path.name)}: {cell(type(exc).__name__)}")
            continue
        for case in root.iter("testcase"):
            source = source_path(case.get("file"), workspace)
            owner = module(path.name, case, source)
            counts[owner] += 1
            problem = next((child for child in case if child.tag in {"failure", "error", "skipped"}), None)
            status = case.get("status", "").lower()
            if problem is None and status not in {"fail", "failed", "notrun", "disabled", "skipped"}:
                continue
            suite = case.get("classname", "")
            name = case.get("name", "(unnamed)")
            test = f"{suite}.{name}" if suite and suite != name else name
            location = cell(path.name)
            line = case.get("line") or ""
            if problem is not None:
                first_line = (problem.get("message") or problem.text or "").splitlines()
                diagnostic = re.match(r"^(.+?)(?::(\d+)|\((\d+)\))(?::|\s|$)", first_line[0]) if first_line else None
                if diagnostic and source_path(diagnostic[1], workspace):
                    source = source_path(diagnostic[1], workspace)
                    line = diagnostic[2] or diagnostic[3]
            if not source:
                source, line = registration(workspace, path.name, name)
            if source:
                anchor = "#L" + line if line.isdecimal() else ""
                location += f" · [source]({source_url}/{quote(source)}{anchor})"
            detail = reason(problem) if problem is not None else status
            if path.name.startswith("server_") and re.fullmatch(r"[\w./-]+", test):
                location += "<br>" + cell("--gtest_filter=" + test)
            if path.name.startswith("client_") and re.fullmatch(r"[\w.-]+", name):
                location += "<br>" + cell("ctest -R '^" + re.escape(name) + "$'")
            rows.append(f"| {cell(owner)} | {cell(test)} | {cell(detail)} | {location} |")
    lines = [f"[Job log]({log_url})"]
    if artifact_url:
        lines[0] += f" · [Download original evidence]({artifact_url})"
    if rows or errors or not counts or os.environ.get("CI_JOB_STATUS") == "failure":
        job_id = os.environ.get("CI_CHECK_RUN_ID", "")
        if job_id.isdecimal():
            lines += ["", "Repeat this job: " + cell(f"gh run rerun --job {job_id}")]
    if rows:
        lines += ["", "| Module | Failing or skipped test | Observation | Report / source |",
                  "| --- | --- | --- | --- |", *rows[:80]]
        if len(rows) > 80:
            lines.append(f"Additional cases in original reports: {len(rows) - 80}.")
    if errors:
        lines += ["", "Unreadable reports: " + ", ".join(errors)]
    if counts:
        lines += ["", "Reported cases by module: " + ", ".join(
            f"{cell(owner)} ({count})" for owner, count in sorted(counts.items()))]
        if os.environ.get("CI_JOB_STATUS") == "failure" and not rows:
            lines += ["", "Job failed outside the reported testcase assertions. Check the failed step for "
                      "build, registration, missing report or teardown diagnostics."]
    else:
        lines += ["", "No testcase evidence available. Check the first failed setup, build or report step in the job log."]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    repo = f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}"
    run = f"{repo}/actions/runs/{os.environ['GITHUB_RUN_ID']}"
    check = os.environ.get("CI_CHECK_RUN_ID", "")
    log = f"{run}/job/{check}" if check.isdecimal() else run
    title, *patterns = sys.argv[1:]
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as output:
        output.write(f"### {title} · {os.environ['GITHUB_JOB']}\n\n")
        output.write(render(Path.cwd(), patterns, log, os.environ.get("CI_ARTIFACT_URL", ""),
                            f"{repo}/blob/{os.environ['GITHUB_SHA']}"))
