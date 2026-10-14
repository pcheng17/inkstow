"""Focused command-line tests for check-clang-tidy.py."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("check-clang-tidy.py")
spec = importlib.util.spec_from_file_location("check_clang_tidy", SCRIPT)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class ClangTidyCheckTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.build_dir = Path(self.temp.name)
        self.sources = [source for group in checker.source_groups() for source in group]

    def write_database(self, sources):
        entries = [{"directory": str(self.build_dir), "file": str(checker.ROOT / source)} for source in sources]
        (self.build_dir / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")

    def run_checker(self, binary):
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--build-dir", str(self.build_dir), "--clang-tidy", str(binary)],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_missing_entry_prevents_lint(self):
        self.write_database(self.sources[1:])
        marker = self.build_dir / "ran"
        binary = self.build_dir / "tidy"
        binary.write_text(f"#!/bin/sh\ntouch '{marker}'\n", encoding="utf-8")
        binary.chmod(0o755)
        result = self.run_checker(binary)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(self.sources[0], result.stderr)
        self.assertFalse(marker.exists())

    def test_lint_failure_propagates(self):
        self.write_database(self.sources)
        binary = self.build_dir / "tidy"
        binary.write_text("#!/bin/sh\nexit 7\n", encoding="utf-8")
        binary.chmod(0o755)
        result = self.run_checker(binary)
        self.assertEqual(result.returncode, 7)
        self.assertIn("--warnings-as-errors=*", result.stdout)


if __name__ == "__main__":
    unittest.main()
