"""Git 增量规则：元数据始终作为数据处理，历史豁免固定于启用前提交。"""

import re
import subprocess

BASELINE = "aa154435af656985a885971b70e54e1ee9f8f546"
TYPES = "feat|fix|refactor|perf|docs|test|build|ci|style|chore|revert"
SCOPES = "client|gate|status|chat|resource|verify|proto|shared|scripts|deps|repo"
BRANCH = re.compile(rf"(?:{TYPES})/(?:{SCOPES})/[a-z0-9]+(?:-[a-z0-9]+)*")
SUBJECT = re.compile(rf"(?P<type>{TYPES})\((?P<scope>{SCOPES})\)(?P<breaking>!)?: (?P<summary>[a-z][ -~]+)")
# 自动检查只识别这些常用祈使动词；语义准确性仍由评审判断。
VERBS = set("add allow align apply audit authenticate avoid bound build cache cancel check clarify clean close compare complete configure consolidate correct cover create decode define delete disable document drain enable enforce ensure exclude expose extract fail fix generate guard handle implement improve include initialize isolate keep limit load make migrate normalize package parse pin preserve prevent record reduce refactor register reject release remove rename render replace require resolve restore retry return reuse revert route run save select separate serialize set share simplify skip sort split standardize stop support sync synchronize test track unify update use validate verify wire write".split())


def git(root, *arguments):
    """执行只读 Git 命令，读取失败和超时向调用方传播。"""
    return subprocess.check_output(["git", "-C", str(root), *arguments], timeout=30).decode("utf-8-sig")


def validate_branch(name):
    """允许长期分支或 type/scope/kebab-case，拒绝空值及额外路径层级。"""
    return [] if name in {"develop", "master"} or BRANCH.fullmatch(name) else [f"invalid branch: {name!r}"]


def validate_message(message, parents=1):
    """校验普通、squash、revert 和真实 merge 提交；返回所有可定位的问题。"""
    lines = message.splitlines()
    subject = lines[0] if lines else ""
    if parents > 1 and re.fullmatch(r"Merge (?:pull request|branch|remote-tracking branch) .+", subject):
        return []
    match = SUBJECT.fullmatch(subject)
    if not match or len(subject) > 100 or subject.endswith("."):
        return [f"invalid subject: {subject!r}"]
    summary = match["summary"]
    problems = []
    if summary.split()[0] not in VERBS or summary in {"update code", "fix bugs"}:
        problems.append(f"summary needs a concrete imperative action: {summary!r}")
    body = "\n".join(lines[1:])
    if match["breaking"] and not re.search(r"^BREAKING CHANGE:\s*\S.+", body, re.M):
        problems.append("breaking change requires BREAKING CHANGE: migration details")
    if match["type"] == "revert" and not re.search(r"\b[0-9a-f]{7,40}\b", body):
        problems.append("revert body requires the reverted commit SHA")
    return problems


def event_range(event, event_name, baseline=BASELINE):
    """从受控事件字段选取新增范围，不读取事件的截断 commits 数组。"""
    if event_name == "pull_request":
        pr = event["pull_request"]
        return {"base": pr["base"]["sha"], "head": pr["head"]["sha"],
                "branch": pr["head"]["ref"], "target": pr["base"]["ref"],
                "message": pr["title"] + "\n\n" + (pr.get("body") or "")}
    if event_name == "push":
        if event.get("deleted"):
            return None
        before, after = event["before"], event["after"]
        return {"base": baseline if set(before) == {"0"} else before, "head": after,
                "branch": event["ref"].removeprefix("refs/heads/"), "target": "", "message": None}
    if event_name in {"schedule", "workflow_dispatch"}:
        return None
    raise ValueError(f"unsupported GitHub event: {event_name}")


def check_range(root, context, baseline=BASELINE):
    """校验来源分支、标题及范围内非历史提交；只有固定基线祖先可获历史豁免。"""
    problems = validate_branch(context["branch"])
    base, head = context["base"], context["head"]
    for revision in (base, head, baseline):
        if not re.fullmatch(r"[0-9a-f]{40}", revision):
            raise ValueError(f"expected full commit SHA: {revision!r}")
        git(root, "cat-file", "-e", revision + "^{commit}")
    if context.get("message") is not None:
        problems += ["PR: " + error for error in validate_message(context["message"])]
        if context["target"] == "master":
            version = git(root, "show", head + ":VERSION").strip()
            if context["branch"] != "develop" or context["message"].splitlines()[0] != f"chore(repo): release {version}":
                problems.append("master PR must be develop -> master with the exact VERSION release title")
    for sha in git(root, "rev-list", "--reverse", head, "--not", base, baseline).splitlines():
        parents = git(root, "rev-list", "--parents", "-n", "1", sha).split()[1:]
        message = git(root, "show", "-s", "--format=%B", sha)
        problems += [f"{sha}: {error}" for error in validate_message(message, len(parents))]
    return problems
