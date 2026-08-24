"""Invariant I10 lint (docs/specs/project-data-model.md section 6).

Fails when a source identifier contains the product name or a retired
section 5.1 vocabulary term. Prose (comments) may name a retired term in
order to retire it; an identifier may not.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

FORBIDDEN = re.compile(
    r"[A-Za-z0-9_]*(fruity|claw|choreograph|sound|tune|song)[A-Za-z0-9_]*",
    re.IGNORECASE,
)

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT = re.compile(r"//.*?$", re.MULTILINE)
STRING = re.compile(r'"(?:[^"\\]|\\.)*"')


def check(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    # Preserve line numbers when stripping block comments.
    text = BLOCK_COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = LINE_COMMENT.sub("", text)
    text = STRING.sub('""', text)
    failures = []
    for match in FORBIDDEN.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        failures.append(f"{path}:{line}: forbidden identifier '{match.group(0)}'")
    return failures


def main() -> int:
    root = Path(sys.argv[1])
    failures: list[str] = []
    for path in sorted(root.rglob("*")):
        if path.suffix in {".h", ".cpp"}:
            failures.extend(check(path))
    for failure in failures:
        print(failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
