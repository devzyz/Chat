"""使用固定版本的 clang-format 检查或排版 Git 跟踪的自有 C++ 文件。"""

import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
VERSION = "19.1.5"
EXTENSIONS = {".h", ".hh", ".hpp", ".hxx", ".cc", ".cpp", ".cxx", ".ipp", ".inl", ".tpp"}
EXCLUDED = ("chat/packages/", "generated/", "tests/auto/", "build/", "Cache/", "help/",
            "interview/", "vcpkg_installed/")


def main():
    """默认只读检查，显式传入 --write 时使用同一份文件清单排版。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="clang-format")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    version = subprocess.check_output([args.binary, "--version"], text=True).split()
    if VERSION not in version:
        parser.error(f"clang-format {VERSION} required, got {' '.join(version)}")

    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode("utf-8").split("\0")
    files = [path for path in tracked if Path(path).suffix.lower() in EXTENSIONS
             and not path.startswith(EXCLUDED) and "/node_modules/" not in path
             and not path.endswith((".pb.h", ".pb.cc"))]
    if not files:
        parser.error("no tracked C++ files found")
    options = ["-i"] if args.write else ["--dry-run", "--Werror"]
    for start in range(0, len(files), 32):
        subprocess.run([args.binary, "--style=file", *options, "--", *files[start:start + 32]],
                       cwd=ROOT, check=True)
    print(f"clang-format {VERSION}: {len(files)} files")


if __name__ == "__main__":
    main()
