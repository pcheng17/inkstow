#!/usr/bin/env python3
"""Check all CI clang-tidy sources against the WebView CMake build."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
LINUX_ONLY_SOURCES = {"tests/WebviewNativeNavigationTests.cpp", "src/desktop/NavigationPolicyLinux.cpp"}
SOURCE_GROUPS = (
    ("src/VaultInspect.cpp", "tests/*.cpp"),
    (
        "src/core/vault/*.cpp",
        "src/desktop/Bridge.cpp",
        "src/desktop/Main.cpp",
        "src/desktop/NavigationPolicyLinux.cpp",
    ),
)


def source_groups(platform=sys.platform):
    groups = []
    for patterns in SOURCE_GROUPS:
        sources = []
        for pattern in patterns:
            matches = sorted(ROOT.glob(pattern))
            if not matches:
                raise ValueError(f"No sources match {pattern}")
            for path in matches:
                source = path.relative_to(ROOT).as_posix()
                if platform == "linux" or source not in LINUX_ONLY_SOURCES:
                    sources.append(source)
        groups.append(sources)
    return groups


def check_entries(database, groups):
    try:
        entries = json.loads(database.read_text(encoding="utf-8"))
        compiled = {
            (Path(entry["directory"]) / entry["file"]).resolve()
            for entry in entries
        }
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise ValueError(f"Cannot read compilation database {database}: {error}") from error
    missing = [source for group in groups for source in group if (ROOT / source).resolve() not in compiled]
    if missing:
        raise ValueError(
            "Missing clang-tidy compilation entries (build all checked targets): "
            + ", ".join(missing)
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/debug")
    parser.add_argument("--clang-tidy", default="clang-tidy", help="clang-tidy executable (default: clang-tidy)")
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    try:
        groups = source_groups()
        check_entries(build_dir / "compile_commands.json", groups)
        for sources in groups:
            command = [args.clang_tidy, "-p", str(build_dir), "--warnings-as-errors=*", *sources]
            print("Running:", " ".join(command), flush=True)
            result = subprocess.run(command, cwd=ROOT, check=False)
            if result.returncode:
                return result.returncode if result.returncode > 0 else 1
    except (ValueError, OSError) as error:
        print(f"clang-tidy check: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
