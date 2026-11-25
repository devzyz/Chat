"""Check the exact system symbol requirements of the Jammy release contract."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/deploy'))
from abi import verify


class JammyAbi(unittest.TestCase):
    def check_import(self, version):
        """Give objdump a minimal ELF fixture and inject its reported import."""
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp) / 'service').write_bytes(b'\x7fELF')
            with patch('abi.subprocess.check_output', return_value=f'00000000 *UND* ({version}) symbol'):
                verify(Path(temp))

    def test_jammy_runtime_imports(self):
        for version in ('GLIBC_2.35', 'GLIBCXX_3.4.30', 'CXXABI_1.3.13'):
            with self.subTest(version=version):
                self.check_import(version)

    def test_previous_incompatible_release(self):
        for version in ('GLIBC_2.38', 'GLIBCXX_3.4.32'):
            with self.subTest(version=version), self.assertRaisesRegex(ValueError, 'requires'):
                self.check_import(version)


if __name__ == '__main__':
    unittest.main()
