"""Reject Linux deployment binaries requiring a newer runtime than Ubuntu 22.04."""
from pathlib import Path
import re
import subprocess
import sys

# GCC 11 uses GLIBCXX_3.4.29; Jammy provides glibc 2.35.
LIMITS = {'GLIBC': (2, 35), 'GLIBCXX': (3, 4, 29), 'CXXABI': (1, 3, 13)}


def verify(root):
    """Check versioned imports in every ELF file, including shipped libraries and Node."""
    for path in root.rglob('*'):
        if not path.is_file():
            continue
        with path.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        symbols = subprocess.check_output(['objdump', '-T', path], text=True)
        for row in symbols.splitlines():
            if '*UND*' not in row:
                continue
            for family, version in re.findall(r'\b(GLIBCXX|GLIBC|CXXABI)_([\d.]+)', row):
                if tuple(map(int, version.split('.'))) > LIMITS[family]:
                    raise ValueError(f'{path.name} requires {family}_{version}')


if __name__ == '__main__':
    verify(Path(sys.argv[1]))
