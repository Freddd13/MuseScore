"""Validate personal guide links and source navigation with Python stdlib only.

Read-only: does not compile, change source files, contact remotes, or refresh lines.
Run from any working directory: python personal/tools/check_guides.py
"""

import csv
import json
import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit


REPO = Path(__file__).resolve().parents[2]
PERSONAL = REPO / "personal"
DOCS = PERSONAL / "docs"


def main():
    errors = []
    links = 0
    anchors = 0
    documents = sorted(DOCS.glob("*.md")) + [
        REPO / "AGENTS.md", PERSONAL / "CHANGELOG.md"
    ]
    for document in documents:
        content = document.read_text(encoding="utf-8")
        # Guides use simple inline Markdown links without nested parentheses.
        for target in re.findall(r"\[[^\]\n]+\]\(([^)\n]+)\)", content):
            parsed = urlsplit(target.strip("<>"))
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            destination = (document.parent / unquote(parsed.path)).resolve()
            try:
                destination.relative_to(REPO)
            except ValueError:
                errors.append(f"{document.name}: link leaves repository: {target}")
                continue
            links += 1
            if not destination.exists():
                errors.append(f"{document.name}: broken link: {target}")

    with (DOCS / "source-map.tsv").open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    expected_fields = {"topic", "path", "symbol", "line", "role", "tests"}
    if not rows or set(rows[0]) != expected_fields:
        errors.append("source-map.tsv: missing rows or unexpected columns")
    cache = {}
    for row in rows:
        source = (REPO / row["path"]).resolve()
        try:
            source.relative_to(REPO)
        except ValueError:
            errors.append(f"source map path leaves repository: {row['path']}")
            continue
        if not source.is_file():
            errors.append(f"source map file missing: {row['path']}")
            continue
        if source not in cache:
            cache[source] = source.read_text(encoding="utf-8").splitlines()
        source_lines = cache[source]
        try:
            line = int(row["line"])
        except ValueError:
            errors.append(f"invalid line: {row['path']}:{row['line']}")
            continue
        anchor = row["symbol"]
        matches = [i + 1 for i, value in enumerate(source_lines) if anchor in value]
        if not anchor or not matches:
            errors.append(f"source symbol missing: {row['path']} :: {anchor}")
        elif not (1 <= line <= len(source_lines) and anchor in source_lines[line - 1]):
            errors.append(
                f"source line drift: {row['path']}:{line} :: {anchor}; now {matches}"
            )
        else:
            anchors += 1
        for test_path in (path for path in row["tests"].split(";") if path and path != "-"):
            destination = (REPO / test_path).resolve()
            try:
                destination.relative_to(REPO)
                inside_repo = True
            except ValueError:
                inside_repo = False
            if not inside_repo or not destination.exists():
                errors.append(f"source map test path missing/invalid: {test_path}")

    version = (PERSONAL / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        errors.append("personal/VERSION: expected major.minor.patch")
    changelog = (PERSONAL / "CHANGELOG.md").read_text(encoding="utf-8")
    if f"## {version} — " not in changelog:
        errors.append(f"CHANGELOG.md: missing current version entry {version}")
    if f"personal-v{version}" not in changelog:
        errors.append(f"CHANGELOG.md: missing current version tag locator {version}")

    baseline = json.loads((DOCS / "baseline.json").read_text(encoding="utf-8"))
    if not re.fullmatch(r"[0-9a-f]{40}", baseline["source_commit"]):
        errors.append("baseline.json: invalid full source SHA")
    if baseline["tracked_file_count"] != sum(baseline["top_level_file_counts"].values()):
        errors.append("baseline.json: inconsistent top-level file count")
    if baseline["tracked_file_count"] != sum(baseline["file_type_counts"].values()):
        errors.append("baseline.json: inconsistent file type count")
    if baseline["source_commit"] not in (DOCS / "README.md").read_text(encoding="utf-8"):
        errors.append("README.md: source baseline differs from baseline.json")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print(f"FAILED: {len(errors)} guide issue(s)", file=sys.stderr)
        return 1
    print(
        f"OK: {len(documents)} documents, {links} local links, "
        f"{anchors} source anchors; personal version {version}."
    )
    print("Static navigation checks only; application behavior/build not tested.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError) as error:
        print(f"ERROR: guide input unavailable or malformed: {error}", file=sys.stderr)
        sys.exit(1)
