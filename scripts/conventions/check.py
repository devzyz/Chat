"""本地与 CI 共用规范入口；Git 和源码增量检查失败均返回非零。"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCAL_PACKAGES = ROOT / "build/conventions/python"
if LOCAL_PACKAGES.is_dir():
    sys.path.insert(0, str(LOCAL_PACKAGES))
from git_rules import BASELINE, check_range, event_range, git
from syntax import affected_symbols, check_source, language_for, parse, problems, qt_auto_slots

EXCLUDED = ("chat/packages/", "generated/", "tests/auto/", "build/", "Cache/", "help/", "interview/",
            "vcpkg_installed/", ".git/")
OWNED = ("chat/", "ChatServer/", "GateServer/", "StatusServer/", "ResourceServer/", "common/",
         "VarifyServer/", "scripts/", "tests/", "schema/", "proto/", "cmake/", "triplets/", ".github/")


def owned(path):
    """限定自有活动代码，排除第三方、产物及用户维护资料。"""
    return ((path.startswith(OWNED) or "/" not in path) and not path.startswith(EXCLUDED) and "/node_modules/" not in path
            and not path.endswith((".pb.h", ".pb.cc", ".grpc.pb.h", ".grpc.pb.cc")))


def read_revision(root, revision, path):
    """读取指定树的文件；只允许明确不存在的文件返回空，其他 Git 错误传播。"""
    entries = git(root, "ls-tree", "--name-only", revision, "--", path).splitlines()
    return git(root, "show", revision + ":" + path) if path in entries else ""


def source_paths(root, base, head=None):
    """枚举新增和修改文件，保留本地未暂存及未跟踪文件，删除文件无新对象可检查。"""
    arguments = ["diff", "--name-only", "--diff-filter=ACMR", "-z", base]
    if head:
        arguments.append(head)
    files = set(git(root, *arguments).split("\0"))
    if not head:
        files.update(git(root, "ls-files", "--others", "--exclude-standard", "-z").split("\0"))
    return sorted(path for path in files if path and owned(path))


def designer_slots(root, path, head):
    """只使用当前被检查根目录与版本的 Designer 文件判定自动槽名称。"""
    if not path.startswith('chat/') or language_for(path) != 'cpp':
        return set()
    ui = Path(path).with_suffix('.ui').as_posix()
    source = read_revision(root, head, ui) if head else (
        (root / ui).read_text(encoding='utf-8-sig') if (root / ui).is_file() else '')
    return qt_auto_slots(source)


def documented_declarations(root, path, head):
    """读取同名头文件、直接 include 及本文件前置声明，匹配权威接口说明。"""
    result = {}
    def read(candidate):
        """仅读取仓库内存在的文件；拒绝越界 include。"""
        if not (root / candidate).resolve().is_relative_to(root.resolve()):
            return ""
        return read_revision(root, head, candidate) if head else (
            (root / candidate).read_text(encoding="utf-8-sig") if (root / candidate).is_file() else "")
    candidates = {Path(path).with_suffix(".h").as_posix()}
    candidates.update((Path(path).parent / name).as_posix()
                      for name in re.findall(r'^\s*#include\s+"([^"\n]+\.(?:h|hpp))"', read(path), re.M))
    candidates.add(path)
    for candidate in sorted(candidates):
        source = read(candidate)
        if not source:
            continue
        for symbol in parse(source, "cpp"):
            if symbol.kind == "class":
                result.setdefault(("__class__", symbol.name, ()), []).append(symbol)
            if symbol.kind == "function":
                if candidate == path and not symbol.code.rstrip().endswith(';'):
                    continue  # 同文件只借用声明，不能用定义自身或另一实现掩盖遗漏。
                key = (symbol.name.split("::")[-1], symbol.owner.split("::")[-1], symbol.parameters)
                result.setdefault(key, []).append(symbol)
    return result


def has_authoritative_comment(symbol, declarations, path):
    """只接收唯一匹配的合法声明，歧义重载交回定义处说明，避免误借另一重载。"""
    if symbol.kind != "function":
        return False
    parts = symbol.name.split("::")
    owner = parts[-2] if len(parts) > 1 else symbol.owner
    matches = declarations.get((parts[-1], owner, symbol.parameters), [])
    if not matches:
        # 声明与定义允许使用不同参数名；仅在参数数量唯一时借用契约，不猜测歧义重载。
        matches = [candidate for (name, enclosing, params), values in declarations.items()
                   if name == parts[-1] and enclosing == owner and len(params) == len(symbol.parameters)
                   for candidate in values]
    return len(matches) == 1 and not any(
        not error.startswith("invalid ") for error in problems(matches[0], "cpp", path))


def source_errors(root, base, head, paths):
    """检查受影响对象，显式报告未支持语言，不用未触及旧代码阻塞增量。"""
    errors, manual = [], []
    for path in paths:
        if not language_for(path):
            if Path(path).suffix in {".py", ".sh", ".cmake", ".proto", ".yml", ".xml", ".vcxproj"}:
                manual.append(path)
            continue
        before = read_revision(root, base, path)
        after = read_revision(root, head, path) if head else (root / path).read_text(encoding="utf-8-sig")
        try:
            findings = check_source(path, before, after, qt_slots=designer_slots(root, path, head))
            declarations = documented_declarations(root, path, head) if path.endswith((".cpp", ".cc")) else {}
            for symbol, error in findings:
                if error == "missing Chinese responsibility comment" and has_authoritative_comment(symbol, declarations, path):
                    continue
                errors.append(f"{path}:{symbol.line}: {symbol.name}: {error}")
            if declarations:
                owners = {s.name.split("::")[-2] for s in affected_symbols(path, before, after)
                          if s.kind == "function" and "::" in s.name}
                for owner in owners:
                    for declaration in declarations.get(("__class__", owner, ()), []):
                        errors += [f"{path}: owner {owner} (included header): {error}"
                                   for error in problems(declaration, "cpp", path)]
        except ValueError as error:
            errors.append(f"{path}: {error}")
    return errors, manual


def audit(root):
    """全目录审计输出每项缺口及解析失败，不把历史欠账记为通过。"""
    report = {"directories": {}, "findings": [], "parse_failures": [], "manual": []}
    for path in sorted(set(git(root, "ls-files", "--cached", "--others", "--exclude-standard", "-z").split("\0"))):
        if not owned(path) or not (root / path).is_file():
            continue
        directory = path.split("/")[0] if "/" in path else "[root]"
        group = report["directories"].setdefault(directory, {"files": 0, "parsed": 0, "symbols": 0})
        group["files"] += 1
        if not language_for(path):
            report["manual"].append(path)
            continue
        try:
            symbols = parse((root / path).read_text(encoding="utf-8-sig"), language_for(path))
            slots = designer_slots(root, path, None)
            declarations = documented_declarations(root, path, None) if path.endswith((".cpp", ".cc")) else {}
            group["parsed"] += 1
            group["symbols"] += len(symbols)
            for symbol in symbols:
                for error in problems(symbol, language_for(path), path, slots):
                    if error == "missing Chinese responsibility comment" and has_authoritative_comment(symbol, declarations, path):
                        continue
                    report["findings"].append({"path": path, "line": symbol.line, "name": symbol.name, "error": error})
        except (ValueError, UnicodeError) as error:
            report["parse_failures"].append({"path": path, "error": str(error)})
    return report


def main():
    """读取 CLI 或 GitHub 事件，打印可定位结果；异常与违规统一以非零退出。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="HEAD")
    parser.add_argument("--head")
    parser.add_argument("--event", type=Path)
    parser.add_argument("--event-name")
    parser.add_argument("--git-only", action="store_true")
    parser.add_argument("--audit", type=Path)
    args = parser.parse_args()
    root = Path(git(ROOT, "rev-parse", "--show-toplevel").strip())
    if args.audit:
        report = audit(root)
        args.audit.parent.mkdir(parents=True, exist_ok=True)
        args.audit.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"AUDIT (not a pass): {len(report['findings'])} findings; {len(report['parse_failures'])} parse failures; {args.audit}")
        return 0  # 审计是清单生成操作，明确区别于增量门禁通过。
    context = None
    if args.event:
        if not args.event_name:
            parser.error("--event requires --event-name")
        context = event_range(json.loads(args.event.read_text(encoding="utf-8")), args.event_name)
        if context is None:
            print("No introduced range for this event; full regression remains owned by CI")
            return 0
        base, head = context["base"], context["head"]
    else:
        base = git(root, "rev-parse", "--verify", args.base + "^{commit}").strip()
        head = git(root, "rev-parse", "--verify", args.head + "^{commit}").strip() if args.head else None
        context = {"base": base, "head": head or git(root, "rev-parse", "HEAD").strip(),
                   "branch": git(root, "branch", "--show-current").strip(), "message": None, "target": ""}
    errors = check_range(root, context)
    if not args.git_only:
        source_base = git(root, "merge-base", base, context["head"]).strip()
        findings, manual = source_errors(root, source_base, head, source_paths(root, source_base, head))
        errors += findings
        for path in manual:
            print(f"MANUAL (language not supported): {path}")
    for error in errors:
        print(error, file=sys.stderr)
    print(f"{'FAIL' if errors else 'PASS'}: incremental conventions ({len(errors)} violations)")
    return 1 if errors else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"Convention check failed: {error}", file=sys.stderr)
        sys.exit(2)
