"""The reach rule as a lint (docs/specs/function-surface.md section 1, ADR-060).

A Function is handed exactly one object, `core::MusicalContent`. The Project
document — its identity, its file path, its save metadata — is not reachable
from one, and `ProjectHistory` is the writer a Function must not be able to
call.

tests/test_reach_rule.cpp asserts that for the Functions it names. This lint
covers the ones nobody has written yet: any file whose name ends in
`_functions.h` or `_functions.cpp` fails if a `Project`-rooted identifier
appears in it at all. A file with no `Project` in it cannot declare a Function
that takes one.

Prose may name the Project — that is how the reason gets recorded. An
identifier may not.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Project, ProjectMeta, ProjectHistory, core::Project ... — the whole family.
FORBIDDEN = re.compile(r"\bProject[A-Za-z0-9_]*")

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT = re.compile(r"//.*?$", re.MULTILINE)
STRING = re.compile(r'"(?:[^"\\]|\\.)*"')

SUFFIXES = ("_functions.h", "_functions.cpp")


def check(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    # Preserve line numbers when stripping block comments.
    text = BLOCK_COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = LINE_COMMENT.sub("", text)
    text = STRING.sub('""', text)
    failures = []
    for match in FORBIDDEN.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        failures.append(
            f"{path}:{line}: a Function may not name '{match.group(0)}'. "
            "It receives core::MusicalContent and nothing else (ADR-060)."
        )
    return failures


def main() -> int:
    root = Path(sys.argv[1])
    failures: list[str] = []
    checked = 0
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.name.endswith(SUFFIXES):
            checked += 1
            failures.extend(check(path))
    for failure in failures:
        print(failure)
    print(f"reach rule lint: {checked} Function file(s) checked, {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
