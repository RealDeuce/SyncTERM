#!/usr/bin/env python3
"""Index and validate the pinned RIP3 contract.

The index is a review queue, not a generated claim of conformance. Every source
block starts unreviewed; implementation and independent tests must be recorded
before a reviewer marks it implemented. Normal checks require no network/git.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "src/tests/rip3"
REFERENCE = "1e0bff4190bc2bc10faf357b060a4a4c2e6e535b"
UPSTREAM = "https://github.com/RealDeuce/remote-imaging-protocol"
REFERENCE_BRANCH = "stable"
ISSUE_REPOSITORY = "https://github.com/bbs-land/remote-imaging-protocol"
REVIEW_NOTE = (
    "Index of pinned evidence. Active requirements cover RIPscrip commands, variables, framing and "
    "their protocol-defined effects. For mixed blocks, scope identifies the RIPscrip clauses under "
    "review. RIPtel ANSI behavior, Telnet negotiation and client features are not acceptance criteria."
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_index(data):
    # One review unit per line keeps status/test edits reviewable.
    header = {k: v for k, v in data.items() if k != "requirements"}
    text = json.dumps(header, indent=2, ensure_ascii=False)[:-2]
    text += ',\n  "requirements": [\n'
    text += ",\n".join("    " + json.dumps(row, ensure_ascii=False)
                         for row in data["requirements"])
    (DATA / "requirements.json").write_text(text + "\n  ]\n}\n")


def blocks(text):
    """Keep every non-navigation source block, including each table row."""
    heading = ""
    lines = text.splitlines(keepends=True)
    index = 0
    while index < len(lines):
        line = lines[index]
        if not line.strip() or line.strip() == "---" or line.startswith("[◀ Prev:"):
            index += 1
            continue
        start = index
        if line.startswith("#"):
            heading = line.strip().lstrip("#").strip()
            index += 1
            continue
        if line.startswith("```"):
            index += 1
            while index < len(lines) and not lines[index].startswith("```"):
                index += 1
            if index < len(lines):
                index += 1
        elif line.startswith("|"):
            index += 1
        else:
            index += 1
            while index < len(lines) and lines[index].strip() and not lines[index].startswith(("#", "```", "|")):
                index += 1
        yield start + 1, index, heading, "".join(lines[start:index])


def index_reference(directory):
    prefixes = ("version/3.0/ripscrip", "version/3.0/techspecs",
                "version/2.0/techspecs", "version/1.54/techspecs")
    paths = set()
    for prefix in prefixes:
        paths.update(directory.glob(prefix + "/*.md"))
    paths.update(directory / p for p in (
        "version/implementation.md", "version/glossary.md",
        "version/baseline/techspecs/ansi-vt-support.md"))
    old = json.loads((DATA / "requirements.json").read_text()) if (DATA / "requirements.json").exists() else {}
    if old and old["revision"] != REFERENCE:
        raise ValueError("review a reference revision change before reindexing")
    expected = {d["path"]: d["sha256"] for d in old.get("documents", [])}
    if expected and set(expected) != {p.relative_to(directory).as_posix() for p in paths}:
        raise ValueError("pinned source document set changed")
    previous = {row["id"]: row for row in old.get("requirements", [])}
    documents = []
    requirements = []
    for path in sorted(paths):
        relative = path.relative_to(directory).as_posix()
        raw = path.read_bytes()
        if expected and digest(raw) != expected[relative]:
            raise ValueError("reference differs from pinned source: " + relative)
        documents.append({"path": relative, "sha256": digest(raw)})
        for start, end, heading, text in blocks(raw.decode()):
            identifier = relative + ":" + str(start)
            row = {"id": identifier, "heading": heading,
                   "source": relative, "lines": [start, end],
                   "text": text, "sha256": digest(text.encode()),
                   "status": "unreviewed", "implementation": [], "tests": []}
            if relative.endswith("5.1-text-variables.md") and heading in ("$APP$", "$APPx$", "$GOTOURL$"):
                row.update(status="excluded", reason="External application and URL launching excluded by approved goal.")
            prior = previous.get(identifier)
            if prior and prior["sha256"] == row["sha256"]:
                row = prior
            requirements.append(row)
    result = {"schema": 1, "repository": UPSTREAM, "revision": REFERENCE,
              "reference_branch": REFERENCE_BRANCH, "issue_repository": ISSUE_REPOSITORY,
              "license": UPSTREAM + "/blob/" + REFERENCE + "/LICENSE",
              "note": REVIEW_NOTE,
              "blockers": old.get("blockers", []),
              "documents": documents, "requirements": requirements}
    if "reference_review" in old:
        result["reference_review"] = old["reference_review"]
    write_index(result)


def check_reference_review(data, root=ROOT):
    path = data.get("reference_review")
    if not path:
        raise ValueError("missing reference adoption record")
    review = json.loads((root / path).read_text())
    for key, expected in (("repository", UPSTREAM), ("reference_branch", REFERENCE_BRANCH),
                          ("issue_repository", ISSUE_REPOSITORY)):
        if data.get(key) != expected or review.get(key) != expected:
            raise ValueError("reference adoption " + key + " mismatch")
    if review["revision"] != data["revision"]:
        raise ValueError("reference adoption revision mismatch")
    if review["documents"] != data["documents"]:
        raise ValueError("reference adoption document inventory mismatch")
    if (not re.fullmatch(r"[0-9a-f]{40}", review["previous_revision"])
            or review["previous_revision"] == review["revision"]):
        raise ValueError("invalid previous reference revision")
    if not re.fullmatch(r"[0-9a-f]{64}", review["archive_sha256"]):
        raise ValueError("invalid reference archive digest")


def check_blockers(data, require_complete=False):
    excluded = set()
    for blocker in data["blockers"]:
        state = blocker["state"]
        if state not in {"open", "resolved", "excluded"}:
            raise ValueError("invalid blocker state: " + blocker["id"])
        if state == "excluded":
            if not blocker.get("reason"):
                raise ValueError("missing blocker exclusion reason: " + blocker["id"])
            excluded.add(blocker["issue"])
    for row in data["requirements"]:
        if row["status"] == "blocked" and excluded.intersection(row.get("issues", ())):
            raise ValueError("requirement cites an excluded blocker: " + row["id"])
    if require_complete and any(blocker["state"] == "open" for blocker in data["blockers"]):
        raise ValueError("RIP3 upstream clarification blockers remain")


def check_requirements(require_complete=False):
    data = json.loads((DATA / "requirements.json").read_text())
    if data["revision"] != REFERENCE:
        raise ValueError("reference revision changed without contract review")
    check_reference_review(data)
    documents = {d["path"] for d in data["documents"]}
    if len(documents) != len(data["documents"]) or not documents:
        raise ValueError("empty/duplicate source document inventory")
    if not data["requirements"]:
        raise ValueError("empty source-block review queue")
    seen = set()
    counts = {}
    for row in data["requirements"]:
        if row["id"] in seen or row["source"] not in documents:
            raise ValueError("duplicate/unowned requirement: " + row["id"])
        seen.add(row["id"])
        if digest(row["text"].encode()) != row["sha256"]:
            raise ValueError("pinned evidence modified: " + row["id"])
        state = row["status"]
        if state not in {"unreviewed", "pending", "blocked", "implemented", "context", "excluded"}:
            raise ValueError("invalid requirement state: " + row["id"])
        if state in {"context", "excluded"} and not row.get("reason"):
            raise ValueError("missing classification reason: " + row["id"])
        if state == "blocked" and not row.get("issues"):
            raise ValueError("blocker missing upstream issue: " + row["id"])
        if state == "implemented":
            if not row["implementation"] or not row["tests"]:
                raise ValueError("implementation missing evidence: " + row["id"])
            for path in row["implementation"]:
                if not (ROOT / path).is_file():
                    raise ValueError("missing implementation: " + path)
        counts[state] = counts.get(state, 0) + 1
    print("RIP3 source-block review queue:", json.dumps(counts, sort_keys=True))
    if require_complete and any(counts.get(state, 0) for state in ("unreviewed", "pending", "blocked")):
        raise ValueError("RIP3 compliance is not complete")
    check_blockers(data, require_complete)
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, help="reindex an extracted copy of the pinned reference (preserves unchanged reviews)")
    parser.add_argument("--require-complete", action="store_true",
                        help="acceptance gate; fail while any review or implementation is unfinished")
    args = parser.parse_args()
    if args.reference:
        index_reference(args.reference)
    check_requirements(args.require_complete)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
