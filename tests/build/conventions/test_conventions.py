"""规范门禁回归：真实解析器、隔离 Git 历史和非法输入不执行契约。"""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / "build/conventions/python"), str(ROOT / "scripts/conventions")]
from git_rules import check_range, event_range, validate_branch, validate_message
from syntax import check_source, parse
from check import source_errors


class GitRulesTests(unittest.TestCase):
    """校验分支和标题格式，以及不会扩大到历史的事件选择。"""

    def test_branches(self):
        """长期分支和规范任务分支合法，额外层级及命令字符拒绝。"""
        for name in ["develop", "master", "docs/repo/server-contracts", "fix/client/late-ack"]:
            self.assertEqual([], validate_branch(name))
        for name in ["feature/foo", "fix/client/Foo", "fix/client/a/b", "fix/client/a--b", "$(echo bad)", ""]:
            self.assertTrue(validate_branch(name))

    def test_subjects_and_squash(self):
        """普通与 squash 共用规则，语义空泛及非英文标题拒绝。"""
        self.assertEqual([], validate_message("docs(repo): document shutdown contracts"))
        self.assertEqual([], validate_message("fix(scripts): authenticate GitHub release asset requests"))
        for message in ["fix(client): fix bugs", "chore(repo): update code", "docs(repo): update docs.",
                        "fix(other): add tests", "fix(chat): 修复错误", "fix(chat): add " + "x" * 100]:
            self.assertTrue(validate_message(message))

    def test_merge_requires_real_parents(self):
        """只允许实际多父 merge 使用自动标题，普通提交不能冒充 merge。"""
        self.assertEqual([], validate_message("Merge pull request #15 from devzyz/docs/repo/server-contracts", 2))
        self.assertTrue(validate_message("Merge branch 'anything'", 1))
        self.assertTrue(validate_message("arbitrary merge title", 2))

    def test_revert_and_breaking(self):
        """回退必须给出 SHA，破坏变更必须解释迁移。"""
        self.assertTrue(validate_message("revert(chat): revert message handling"))
        self.assertEqual([], validate_message("revert(chat): revert message handling\n\nReverts abc1234"))
        self.assertTrue(validate_message("feat(proto)!: replace request fields"))
        self.assertEqual([], validate_message("feat(proto)!: replace request fields\n\nBREAKING CHANGE: migrate all clients"))

    def test_event_ranges_and_edited_title(self):
        """标题编辑仍读取当前 PR 元数据，push 不依赖截断的 commits 列表。"""
        event = {"action": "edited", "pull_request": {"title": "docs(repo): document interfaces", "body": None,
                 "base": {"sha": "a" * 40, "ref": "develop"}, "head": {"sha": "b" * 40, "ref": "docs/repo/interfaces"}}}
        context = event_range(event, "pull_request")
        self.assertEqual("b" * 40, context["head"])
        self.assertTrue(context["message"].startswith(event["pull_request"]["title"]))
        push = event_range({"before": "0" * 40, "after": "c" * 40, "ref": "refs/heads/develop"}, "push", "a" * 40)
        self.assertEqual("a" * 40, push["base"])
        self.assertIsNone(event_range({}, "schedule"))
        self.assertIsNone(event_range({"deleted": True}, "push"))
        with self.assertRaises((ValueError, KeyError)):
            event_range({}, "pull_request")


class SyntaxTests(unittest.TestCase):
    """使用实际 AST 验证注释、命名、未触及遗留对象及失败传播。"""

    def test_cpp_overloads_defaults_and_deleted(self):
        """重载各自要求说明，default/delete 不能漏掉。"""
        source = '/** 管理值。 */ class Value { public: /** 默认空值。 */ Value() = default; /** 禁止复制。 */ Value(const Value&) = delete; /** 按编号查询。 */ void Find(int id); void Find(double id); };'
        errors = check_source("common/value.h", "", source)
        self.assertEqual(1, len(errors))
        self.assertEqual("Find", errors[0][0].name)

    def test_qt_signals_slots_and_lambda(self):
        """元对象宏不被当作函数，信号槽及匿名回调被实际解析。"""
        source = '/** 管理界面。 */\nclass Widget {\n Q_OBJECT\npublic slots:\n/** 刷新数据。 */ void refresh();\nsignals:\n/** 更新完成。 */ void refreshed();\n};\n/** 消费回调。 */ void run() { auto cb = /** 返回输入。 */ [](int value) {return value;}; }'
        self.assertEqual([], check_source("chat/widget.h", "", source))
        self.assertTrue(any(s.kind == "lambda" for s in parse(source, "cpp")))

    def test_cpp_brace_default_and_qt_emit(self):
        """支持明确的默认空列表以及 Qt emit，字符串内同名内容保持原样。"""
        source = '/** 发布信号。 */ void send(const Data& value = {}) { emit ready(); const char* text="emit"; }'
        self.assertEqual([], check_source("chat/sample.cpp", "", source))

    def test_modified_function_requires_owner_comment(self):
        """修改旧函数同时检查所属类，不检查未触及兄弟函数。"""
        before = 'class Store { public: int Count(){return 1;} void Legacy(); };'
        after = 'class Store { public: /** 返回数量。 */ int Count(){return 2;} void Legacy(); };'
        errors = check_source("common/store.h", before, after)
        self.assertTrue(any(s.kind == "class" for s, _ in errors))
        self.assertFalse(any(s.name == "Legacy" for s, _ in errors))

    def test_comment_only_does_not_expand_scope(self):
        """纯注释不触发同文件旧代码清理，删除函数也不检查无关函数。"""
        before = 'void legacy_name();\nvoid Old(){ int value=1; }'
        self.assertEqual([], check_source("common/old.h", before, '// 说明历史行为。\n' + before))
        self.assertEqual([], check_source("common/old.h", before, 'void legacy_name();'))

    def test_empty_tags_and_wrong_parameters(self):
        """空模板和不存在的参数名都会失败。"""
        errors = check_source("common/test.h", "", '/** @brief\n * @param missing 无效参数。\n */ void Read(int uid);')
        self.assertTrue(any("unknown @param" in message for _, message in errors))
        self.assertTrue(any("empty documentation" in message for _, message in errors))

    def test_unknown_macro_and_parse_failure(self):
        """未知扩展和损坏语法不能被当作空文件通过。"""
        for source in ['class X { void broken( ; };', 'UNKNOWN_CLASS(X) { void foo(); };']:
            with self.assertRaises(ValueError):
                check_source("common/broken.h", "", source)

    def test_cpp_strings_and_external_override(self):
        """字符串中的伪声明不成为对象，框架 override 名称保留。"""
        source = '/** 框架实现。 */ class X { /** 响应框架。 */ void lower_name() override; }; const char* text="void Bad();";'
        self.assertEqual([], check_source("common/test.h", "", source))
        self.assertEqual([], check_source("chat/test.cpp", "",
            '/** 比较值。 */ bool Value::operator==(const Value& rhs) const { return true; }'))

    def test_js_callbacks_and_naming(self):
        """JS 函数及匿名回调独立定位，内部方法必须 lowerCamelCase。"""
        self.assertEqual([], check_source("VarifyServer/a.js", "", '/** 创建读取器。 */ function makeReader(){ return /** 读取值。 */ () => 1; }'))
        self.assertTrue(check_source("VarifyServer/a.js", "", '/** 读取键。 */ function GetKey() { return 1; }'))
        self.assertTrue(check_source("VarifyServer/a.js", "", '/** 创建读取器。 */ function makeReader(){ return () => 1; }'))

    def test_direct_callback_doc_precedes_test_id(self):
        """测试编号不能遮蔽直接属于回调的 JSDoc，具名箭头也校验命名。"""
        self.assertEqual([], check_source("VarifyServer/a.js", "",
            '// V01\ntest("case", /** 验证结果。 */ () => 1);'))
        self.assertTrue(check_source("VarifyServer/a.js", "",
            '/** 返回值。 */ const Bad_Name = () => 1;'))

    def test_qt_foreach_and_qualified_constructor(self):
        """循环宏不是声明，类外构造函数不按普通函数命名校验。"""
        self.assertEqual([], check_source("chat/a.cpp", "",
            '/** 初始化。 */ Widget::Widget() { foreach(Item x, items) { use(x); } }'))

    def test_powershell_comment_only_and_strings(self):
        """原生 token 比较忽略帮助文本，但保留字符串中的代码变化。"""
        before = 'function Get-Value { return "one" }'
        self.assertEqual([], check_source("scripts/a.ps1", before, '# 新说明。\n' + before))
        self.assertTrue(check_source("scripts/a.ps1", before, before.replace('one', 'two')))

    def test_js_bad_source(self):
        """JS 解析错误阻断，而非报告没有函数。"""
        with self.assertRaises(ValueError):
            check_source("VarifyServer/a.js", "", "function broken( {")

    def test_powershell_functions_and_callbacks(self):
        """PowerShell 使用原生 AST，检查注释帮助及函数名。"""
        source = '<#\n.SYNOPSIS\n读取数据。\n#>\nfunction Get-Data { param([string]$Name) return $Name }'
        self.assertEqual([], check_source("scripts/sample.ps1", "", source))
        self.assertTrue(check_source("scripts/sample.ps1", "", 'function bad_name { return 1 }'))
        self.assertTrue(check_source("scripts/sample.ps1", "",
            '<#\n.SYNOPSIS\n.NOTES\n只有备注。\n#>\nfunction Get-Data {}'))
        with self.assertRaises(ValueError):
            parse('function Broken {', "powershell")

    def test_powershell_source_is_never_executed(self):
        """解析包含恶意命令的源码时不执行命令或创建文件。"""
        with tempfile.TemporaryDirectory() as folder:
            marker = Path(folder) / "marker"
            parse(f"Set-Content -LiteralPath '{marker}' -Value bad", "powershell")
            self.assertFalse(marker.exists())


class GitIntegrationTests(unittest.TestCase):
    """在运行私有临时仓库构造历史，真实验证基线与发行标题；不提交工作仓库。"""

    def test_history_new_commit_release_and_metadata(self):
        """固定历史允许旧格式，新违规提交失败，PR 元数据不能执行 shell。"""
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            def run(*args):
                """仅在测试创建的临时仓库执行 Git，限制执行期限。"""
                return subprocess.check_output(["git", "-C", folder, *args], stderr=subprocess.DEVNULL, timeout=10).decode().strip()
            run("init")
            run("config", "user.email", "test@example.invalid")
            run("config", "user.name", "Convention Test")
            (root / "VERSION").write_text("1.2.3\n")
            run("add", "VERSION")
            run("commit", "-m", "historical legacy subject")
            baseline = run("rev-parse", "HEAD")
            run("commit", "--allow-empty", "-m", "docs(repo): document interfaces")
            head = run("rev-parse", "HEAD")
            context = {"base": baseline, "head": head, "branch": "develop", "target": "master",
                       "message": "chore(repo): release 1.2.3"}
            self.assertEqual([], check_range(root, context, baseline))
            context["message"] = "chore(repo): release 9.9.9"
            self.assertTrue(check_range(root, context, baseline))
            context["message"] = "$(touch marker)"
            self.assertTrue(check_range(root, context, baseline))
            self.assertFalse((root / "marker").exists())
            run("commit", "--allow-empty", "-m", "new invalid subject")
            context.update(head=run("rev-parse", "HEAD"), message=None)
            self.assertTrue(check_range(root, context, baseline))


class HeaderContractTests(unittest.TestCase):
    """在独立 Git 工作区验证声明归属。"""

    def test_header_contract_and_changed_owner(self):
        """类外方法可引用头文件契约，但不能绕过所属类说明与重载歧义检查。"""
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            subprocess.run(['git', 'init', folder], check=True, capture_output=True)
            for key, value in [('user.name', 'Test'), ('user.email', 'test@example.invalid')]:
                subprocess.run(['git', '-C', folder, 'config', key, value], check=True)
            subprocess.run(['git', '-C', folder, 'commit', '--allow-empty', '-m', 'baseline'], check=True, capture_output=True)
            (root / 'common').mkdir()
            header = root / 'common/store.h'
            header.write_text('/** 管理值。 */ class Store { /** 读取值。 */ int Read(int id); };', encoding='utf8')
            (root / 'common/store.cpp').write_text('#include "store.h"\nint Store::Read(int id){return id;}', encoding='utf8')
            self.assertEqual(([], []), source_errors(root, 'HEAD', None, ['common/store.cpp']))
            header.write_text('class Store { /** 读取值。 */ int Read(int id); };', encoding='utf8')
            errors, _ = source_errors(root, 'HEAD', None, ['common/store.cpp'])
            self.assertTrue(any('owner Store' in e for e in errors))
            header.write_text('/** 管理值。 */ class Store { /** 读取值。 */ int Read(int id); int Read(double id); };', encoding='utf8')
            self.assertTrue(source_errors(root, 'HEAD', None, ['common/store.cpp'])[0])


class EntryPointTests(unittest.TestCase):
    """通过公开进程入口验证失败不会被 PowerShell 包装器吞掉。"""

    def test_invalid_edited_title_fails_local_and_ci_entry(self):
        """仅编辑标题也失败；元数据按 JSON 读取，不执行其中的表达式。"""
        head = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD']).decode().strip()
        event = {'action': 'edited', 'pull_request': {'title': 'invalid edited title', 'body': '',
                 'base': {'sha': head, 'ref': 'develop'},
                 'head': {'sha': head, 'ref': 'docs/repo/conventions'}}}
        with tempfile.TemporaryDirectory() as folder:
            payload = Path(folder) / 'event.json'
            payload.write_text(json.dumps(event), encoding='utf8')
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/conventions/check.py'),
                '--event', str(payload), '--event-name', 'pull_request', '--git-only'],
                capture_output=True, timeout=30)
            self.assertEqual(1, result.returncode)
            self.assertIn(b'invalid subject', result.stderr)
            import shutil
            environment = dict(os.environ, GITHUB_ACTIONS='true', GITHUB_EVENT_PATH=str(payload),
                               GITHUB_EVENT_NAME='pull_request')
            result = subprocess.run([shutil.which('pwsh') or shutil.which('powershell'),
                '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'scripts/windows-local.ps1'),
                '-Task', 'CheckConventions'], env=environment, capture_output=True, timeout=45)
            self.assertNotEqual(0, result.returncode)
            self.assertIn(b'invalid subject', result.stderr)


if __name__ == "__main__":
    unittest.main()
