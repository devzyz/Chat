"""基于语法树定位自有声明和回调；检查格式，不推断业务、线程或所有权语义。"""

import dataclasses
import json
import re
import shutil
import subprocess
from pathlib import Path

from tree_sitter import Language, Parser
import tree_sitter_cpp
import tree_sitter_javascript


@dataclasses.dataclass
class Symbol:
    """保存声明范围、参数、所属类及紧邻注释，坐标使用 Unicode 字符偏移。"""

    kind: str
    name: str
    start: int
    end: int
    line: int
    code: str
    comment: str = ""
    parameters: tuple = ()
    owner: str = ""
    external: bool = False
    expression_start: int = 0


def walk(node):
    """深度优先遍历包括宏及错误节点的完整语法树。"""
    yield node
    for child in node.children:
        yield from walk(child)


def qt_source(source):
    """仅遮罩声明位置的已知 Qt 元对象宏，保留每个字节的位置和换行。"""
    data = source
    for pattern in [rb"(?m)^[ \t]*Q_OBJECT\b", rb"(?m)^[ \t]*Q_GADGET\b",
                    rb"(?m)^[ \t]*QT_(?:BEGIN|END)_NAMESPACE\b",
                    rb"(?m)^[ \t]*Q_DECLARE_METATYPE\([^\n]*\)",
                    rb"(?m)^[ \t]*Q_DISABLE_COPY\([^\n]*\)",
                    rb"(?m)^[ \t]*Q_ENUM\([^\n]*\)"]:
        data = re.sub(pattern, lambda match: b" " * len(match[0]), data)
    data = re.sub(rb"(?m)^([ \t]*)(signals|Q_SIGNALS)([ \t]*:)",
                  lambda m: m[1] + b"public" + b" " * (len(m[2]) - 6) + m[3], data)
    data = re.sub(rb"(?m)^([ \t]*(?:public|private|protected)[ \t]+)(slots|Q_SLOTS)([ \t]*:)",
                  lambda m: m[1] + b" " * len(m[2]) + m[3], data)
    # Qt 的 emit 是空宏。仅遮罩词法标识符，不改注释、字符串或原始字符串中的同名文本。
    initial = Parser(Language(tree_sitter_cpp.language())).parse(data)
    result = bytearray(data)
    for node in walk(initial.root_node):
        if node.type in {"identifier", "type_identifier"} and node.text in {b"emit", b"Q_EMIT"}:
            result[node.start_byte:node.end_byte] = b" " * (node.end_byte - node.start_byte)
    data = bytes(result)
    return data


def preceding_comment(source, start, comments):
    """提取与声明相邻的整组注释，不接受隔着其他语句的旧说明。"""
    result = []
    for begin, end, text in reversed(comments):
        if end > start:
            continue
        if source[end:start].strip():
            break
        result.insert(0, text)
        start = begin
    return "\n".join(result)


def parse_powershell(source):
    """在独立 PowerShell 中解析输入文本，不执行源文件；任何解析或工具失败都阻断。"""
    executable = shutil.which("pwsh") or shutil.which("powershell")
    if not executable:
        raise ValueError("PowerShell AST parser unavailable (install PowerShell or use Windows)")
    result = subprocess.run([executable, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                             str(Path(__file__).with_name("powershell_ast.ps1"))],
                            input=source.encode("utf-8"), capture_output=True, timeout=30)
    if result.returncode:
        raise ValueError(result.stderr.decode("utf-8", errors="replace"))
    payload = json.loads(result.stdout.decode("utf-8-sig"))
    # PowerShell 使用 UTF-16 code units；转换后才能安全处理非 BMP 字符前的范围。
    def offset(units):
        """将 PowerShell UTF-16 偏移转换为 Python 字符偏移。"""
        return len(source.encode("utf-16-le")[:units * 2].decode("utf-16-le"))
    comments = [(offset(c["start"]), offset(c["end"]), c["text"]) for c in payload["comments"]]
    symbols = []
    for item in payload["nodes"]:
        start, end = offset(item["start"]), offset(item["end"])
        comment = preceding_comment(source, start, comments)
        if item["kind"] == "lambda" and not comment:
            # 管道/参数中的脚本块可在所在语句正上方说明。
            line_start = source.rfind("\n", 0, start) + 1
            comment = preceding_comment(source, line_start, comments)
        symbols.append(Symbol(item["kind"], item["name"], start, end, item["line"], item["code"],
                              comment, tuple(item["parameters"])))
    return symbols


def parse(source, language):
    """解析 C++/Qt、JS 或 PowerShell；未知宏产生的语法错误也必须显式处理。"""
    if language == "powershell":
        return parse_powershell(source)
    raw = source.encode("utf-8")
    grammar = tree_sitter_cpp if language == "cpp" else tree_sitter_javascript
    parser = Parser(Language(grammar.language()))
    tree = parser.parse(qt_source(raw) if language == "cpp" else raw)
    # cpp 0.23.4 将合法默认空列表 = {} 表示为缺少类型的 compound literal。
    # 只接受这一精确形态；其他 missing/ERROR 仍失败，不掩盖未知宏或残缺声明。
    def empty_default(node):
        """识别固定语法版本的空列表默认参数表示差异。"""
        return (node.is_missing and node.type == "type_identifier" and node.parent is not None
                and node.parent.type == "compound_literal_expression"
                and raw[node.parent.start_byte:node.parent.end_byte].strip() == b"{}"
                and node.parent.parent.type == "optional_parameter_declaration")
    errors = [n for n in walk(tree.root_node) if (n.type == "ERROR" or n.is_missing) and not empty_default(n)]
    if errors:
        node = errors[0]
        raise ValueError(f"parse failure at {node.start_point.row + 1}:{node.start_point.column + 1}: "
                         + raw[node.start_byte:node.end_byte].decode("utf-8")[:100])

    def text(node):
        """读取原始源码中的节点内容，保留 Qt 宏及中文。"""
        return raw[node.start_byte:node.end_byte].decode("utf-8") if node else ""

    def position(byte):
        """将 Tree-sitter UTF-8 字节坐标转换为字符坐标。"""
        return len(raw[:byte].decode("utf-8"))

    comments = [(position(n.start_byte), position(n.end_byte), text(n))
                for n in walk(tree.root_node) if n.type == "comment"]
    symbols = []
    for node in walk(tree.root_node):
        kind, anchor, name, parameters = "", node, "", ()
        if node.type in {"class_specifier", "struct_specifier", "class_declaration", "class"}:
            if node.child_by_field_name("body") is None:
                continue  # 前置声明由完整类型声明拥有说明。
            kind, name = "class", text(node.child_by_field_name("name"))
        elif language == "cpp" and node.type == "function_declarator":
            if node.parent.type not in {"function_definition", "declaration", "field_declaration",
                                        "pointer_declarator", "reference_declarator"}:
                continue
            anchor = node.parent
            while anchor.type in {"pointer_declarator", "reference_declarator"}:
                anchor = anchor.parent
            # typedef / using 函数类型不是函数声明。
            if anchor.type not in {"function_definition", "declaration", "field_declaration"}:
                continue
            if anchor.type == "declaration" and anchor.parent.type == "compound_statement":
                continue  # 局部 direct-init 与函数原型在纯语法层歧义，交由编译及评审。
            kind = "function"
            name = text(node.child_by_field_name("declarator"))
            if name in {"foreach", "Q_FOREACH"} and anchor.child_by_field_name("type") is None:
                continue  # Qt 的循环宏不声明函数，其循环体中的 Lambda 仍会遍历。
            if anchor.type == "function_definition" and anchor.child_by_field_name("type") is None and re.fullmatch(r"[A-Z][A-Z_0-9]+", name):
                raise ValueError(f"unsupported declaration macro {name} at line {anchor.start_point.row + 1}; manual expansion/review required")
            param_node = node.child_by_field_name("parameters")
            values = []
            for param in param_node.named_children:
                if param.type in {"parameter_declaration", "optional_parameter_declaration"}:
                    declarator = param.child_by_field_name("declarator")
                    identifiers = [n for n in walk(declarator) if n.type in {"identifier", "field_identifier"}] if declarator else []
                    if identifiers:
                        values.append(text(identifiers[-1]))
            parameters = tuple(values)
        elif node.type in {"function_declaration", "function_expression", "generator_function_declaration",
                           "generator_function", "arrow_function", "method_definition", "lambda_expression"}:
            kind = "lambda" if node.type in {"arrow_function", "lambda_expression", "function_expression"} else "function"
            name = text(node.child_by_field_name("name"))
            if node.parent.type == "variable_declarator":
                name = text(node.parent.child_by_field_name("name"))
            param_node = node.child_by_field_name("parameters")
            parameters = tuple(text(p) for p in param_node.named_children if p.type == "identifier") if param_node else ()
        if not kind:
            continue
        owner = ""
        ancestor = node.parent
        while ancestor:
            if ancestor.type in {"class_specifier", "struct_specifier", "class_declaration", "class"}:
                owner = text(ancestor.child_by_field_name("name"))
                break
            ancestor = ancestor.parent
        if kind == "lambda":
            # 匿名回调允许在 enclosing call/return/variable 语句前说明，不跨越函数体。
            while anchor.parent and anchor.parent.type in {
                "argument_list", "arguments", "call_expression", "expression_statement", "return_statement",
                "init_declarator", "declaration", "variable_declarator", "lexical_declaration", "pair",
                "assignment_expression", "await_expression", "parenthesized_expression"}:
                anchor = anchor.parent
        if anchor.parent and anchor.parent.type in {"template_declaration", "export_statement"}:
            anchor = anchor.parent
        start, end = position(anchor.start_byte), position(anchor.end_byte)
        comment = preceding_comment(source, start, comments)
        if kind == "lambda":
            comment = preceding_comment(source, position(node.start_byte), comments) or comment
        code = text(node if kind == "lambda" else anchor)
        external = "override" in code.split("{")[0] or name.split("::")[-1].startswith("operator")
        symbols.append(Symbol(kind, name or "<anonymous>", start, end, anchor.start_point.row + 1,
                              code, comment, parameters, owner, external, position(node.start_byte)))
    return symbols


def code_tokens(source, language):
    """比较 AST 叶节点而非文本行，注释及纯空白修改不触发旧函数治理。"""
    if language == "powershell":
        # 原生解析器已经去除注释和换行，字符串 token 的内容仍完整保留。
        return source.strip()
    grammar = tree_sitter_cpp if language == "cpp" else tree_sitter_javascript
    raw = source.encode("utf-8")
    tree = Parser(Language(grammar.language())).parse(raw)
    return tuple((node.type, raw[node.start_byte:node.end_byte]) for node in walk(tree.root_node)
                 if not node.children and node.type != "comment")


def problems(symbol, language, path):
    """检查非空中文说明、参数名和语言命名格式；不判定注释的业务真实性。"""
    result = []
    comment = symbol.comment
    proper = (".SYNOPSIS" in comment.upper()) if language == "powershell" and symbol.kind != "lambda" else (
        bool(comment) if language == "powershell" else "/**" in comment)
    if not proper or not re.search(r"[\u3400-\u9fff]", comment):
        result.append("missing Chinese responsibility comment")
    if language == "powershell" and symbol.kind == "function" and proper:
        synopsis = re.search(r"\.SYNOPSIS[ \t]*([\s\S]*?)(?=\r?\n[ \t]*\.[A-Z]+|#>)", comment, re.I)
        if not synopsis or not re.search(r"[\u3400-\u9fff]", synopsis[1]):
            result.append("missing Chinese .SYNOPSIS description")
    for parameter in re.findall(r"@param(?:\[[^]]+\])?\s+(?:\{[^}]+\}\s*)?([A-Za-z_]\w*)", comment):
        if parameter not in symbol.parameters:
            result.append(f"unknown @param {parameter}")
    if re.search(r"@(brief|return|returns|throws|note)\s*(?=\*/|\n\s*\*\s*@|$)", comment):
        result.append("empty documentation tag")
    name = symbol.name.split("::")[-1]
    if (symbol.kind == "lambda" and (language != "javascript" or name == "<anonymous>")) or symbol.external or name in {"main", "constructor", "GetVarifyCode"}:
        return result
    qualified_owner = symbol.name.split("::")[-2] if "::" in symbol.name else symbol.owner.split("::")[-1]
    if name.startswith("~") or name == qualified_owner:
        return result
    pattern = r"[A-Z][A-Za-z0-9]*" if symbol.kind == "class" else (
        r"[A-Za-z]+-[A-Za-z][A-Za-z0-9]*" if language == "powershell" else
        r"[a-z][A-Za-z0-9]*" if language == "javascript" or path.startswith("chat/") else r"[A-Z][A-Za-z0-9]*")
    if not re.fullmatch(pattern, name):
        result.append(f"invalid {language} {symbol.kind} name {name!r}")
    return result


def affected_symbols(path, before, after, full=False):
    """仅约束新增或代码修改的对象及所属类；全量模式用于审计而不是阻塞未触及旧代码。"""
    language = language_for(path)
    current = parse(after, language)
    previous = parse(before, language) if before else []
    old_codes = {(s.kind, s.name, s.owner, code_tokens(s.code, language)) for s in previous}
    changed = [s for s in current if full or (s.kind, s.name, s.owner, code_tokens(s.code, language)) not in old_codes]
    owners = {s.owner for s in changed if s.owner}
    changed += [s for s in current if s.kind == "class" and s.name in owners and s not in changed]
    return changed


def check_source(path, before, after, full=False):
    """校验增量选中的对象，返回声明与对应错误。"""
    return [(symbol, error) for symbol in affected_symbols(path, before, after, full)
            for error in problems(symbol, language_for(path), path)]


def language_for(path):
    """识别支持的三种语言；其他语言由 CLI 显式报告评审边界。"""
    suffix = Path(path).suffix.lower()
    return "cpp" if suffix in {".h", ".hpp", ".cpp", ".cc"} else "javascript" if suffix == ".js" else "powershell" if suffix == ".ps1" else None
