#!/usr/bin/env python3
"""Exercise the RIP3 reference and requirements checks."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
from pathlib import Path
import json
import tempfile
import unittest
from unittest.mock import patch

from rip3_compliance import (REFERENCE, UPSTREAM, REFERENCE_BRANCH,
                            ISSUE_REPOSITORY, check_blockers,
                            check_reference_review, digest, index_reference)


class ReferenceTests(unittest.TestCase):
    def test_excluded_issue_is_historical_and_cannot_block_a_requirement(self):
        issue = ISSUE_REPOSITORY + "/issues/298"
        blocker = {"id": "B298", "issue": issue, "state": "excluded",
                   "reason": "Ordinary ANSI editing belongs to the ANSI engine."}
        data = {"blockers": [blocker], "requirements": []}
        check_blockers(data, require_complete=True)
        data["requirements"].append({"id": "mixed.md:1", "status": "blocked", "issues": [issue]})
        with self.assertRaisesRegex(ValueError, "requirement cites an excluded blocker"):
            check_blockers(data)
        data["requirements"].clear()
        blocker["state"] = "open"
        with self.assertRaisesRegex(ValueError, "clarification blockers remain"):
            check_blockers(data, require_complete=True)

    def test_normal_reindex_preserves_adoption_record_and_review(self):
        with tempfile.TemporaryDirectory(prefix="syncterm-rip3-reindex-test-") as directory:
            root = Path(directory)
            source, ledger = root / "source", root / "ledger"
            ledger.mkdir()
            names = ("version/3.0/ripscrip/example.md", "version/implementation.md",
                     "version/glossary.md", "version/baseline/techspecs/ansi-vt-support.md")
            documents = []
            for name in sorted(names):
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("# Example\n\nA pinned source block.\n")
                documents.append({"path": name, "sha256": digest(path.read_bytes())})
            reviewed = {"id": names[0] + ":3", "source": names[0],
                        "heading": "Example", "lines": [3, 3],
                        "text": "A pinned source block.\n",
                        "sha256": digest(b"A pinned source block.\n"),
                        "status": "pending", "implementation": [], "tests": [],
                        "scope": "The RIP command's observable effect.",
                        "reason": "Retain the independent review."}
            data = {"revision": REFERENCE, "reference_review": "review.json",
                    "documents": documents, "requirements": [reviewed]}
            path = ledger / "requirements.json"
            path.write_text(json.dumps(data))
            with patch("rip3_compliance.DATA", ledger):
                index_reference(source)
            result = json.loads(path.read_text())
            self.assertEqual(result["reference_review"], "review.json")
            self.assertEqual(result["documents"], documents)
            self.assertIn(reviewed, result["requirements"])
            self.assertEqual(result["repository"], UPSTREAM)
            self.assertEqual(result["reference_branch"], REFERENCE_BRANCH)
            self.assertEqual(result["issue_repository"], ISSUE_REPOSITORY)

    def test_adoption_record_cannot_lag_pin_or_document_inventory(self):
        with tempfile.TemporaryDirectory(prefix="syncterm-rip3-adoption-test-") as directory:
            root = Path(directory)
            source = {"repository": UPSTREAM, "reference_branch": REFERENCE_BRANCH,
                      "issue_repository": ISSUE_REPOSITORY}
            data = {**source, "revision": REFERENCE, "reference_review": "review.json",
                    "documents": [{"path": "contract.md", "sha256": "a" * 64}]}
            review = {**source, "revision": REFERENCE, "previous_revision": "b" * 40,
                      "archive_sha256": "c" * 64, "documents": data["documents"]}
            path = root / "review.json"
            path.write_text(json.dumps(review))
            check_reference_review(data, root)
            review["revision"] = "d" * 40
            path.write_text(json.dumps(review))
            with self.assertRaisesRegex(ValueError, "revision mismatch"):
                check_reference_review(data, root)
            review["revision"] = REFERENCE
            review["documents"] = [{"path": "contract.md", "sha256": "e" * 64}]
            path.write_text(json.dumps(review))
            with self.assertRaisesRegex(ValueError, "inventory mismatch"):
                check_reference_review(data, root)
            with self.assertRaisesRegex(ValueError, "missing reference adoption"):
                check_reference_review({"revision": REFERENCE}, root)

    def test_source_move_cannot_redirect_parent_issues(self):
        with tempfile.TemporaryDirectory(prefix="syncterm-rip3-source-test-") as directory:
            root = Path(directory)
            data = {"repository": UPSTREAM, "reference_branch": REFERENCE_BRANCH,
                    "issue_repository": ISSUE_REPOSITORY, "revision": REFERENCE,
                    "reference_review": "review.json", "documents": []}
            review = {**data, "previous_revision": "b" * 40, "archive_sha256": "c" * 64}
            path = root / "review.json"
            path.write_text(json.dumps(review))
            check_reference_review(data, root)
            for key in ("repository", "reference_branch", "issue_repository"):
                for target in (data, review):
                    original = target[key]
                    target[key] = "unexpected destination"
                    path.write_text(json.dumps(review))
                    with self.assertRaisesRegex(ValueError, key + " mismatch"):
                        check_reference_review(data, root)
                    target[key] = original
                    path.write_text(json.dumps(review))


if __name__ == "__main__":
    unittest.main()
