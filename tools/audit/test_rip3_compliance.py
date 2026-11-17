#!/usr/bin/env python3
"""Exercise the source-preservation guard against actual file/body changes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
from pathlib import Path
import json
import tempfile
import unittest
from unittest.mock import patch

from rip3_compliance import (BASELINE, REFERENCE, c_functions, check_freeze,
                            check_reference_review, digest, index_reference)


class FreezeTests(unittest.TestCase):
    def test_c_tokens_do_not_end_function(self):
        source = '''static int
rip_example(void)
{
    const char *s = "} /* not a comment */";
    char c = '}'; /* } */
    // }
    if (s[0] == c) { return 1; }
    return 0;
}
int other(void) { return 2; }
'''
        functions = c_functions(source)
        self.assertEqual(set(functions), {"rip_example"})
        self.assertTrue(functions["rip_example"].endswith(b"return 0;\n}"))

    def test_frozen_file_and_body(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = root / "legacy.c"
            data.write_bytes(b"legacy bytes\n")
            glue = root / "glue.c"
            original = "int\nrip_old(void)\n{\n\treturn 1;\n}\n"
            glue.write_text(original)
            manifest = {"baseline": BASELINE,
                        "files": {"legacy.c": digest(data.read_bytes())},
                        "functions": {"glue.c": {"rip_old": digest(c_functions(original)["rip_old"])}}}
            self.assertEqual(check_freeze(root, manifest), (1, 1))
            glue.write_text(original + "int new_dispatch(void) { return 3; }\n")
            self.assertEqual(check_freeze(root, manifest), (1, 1))
            glue.write_text(original.replace("return 1", "return 2"))
            with self.assertRaisesRegex(ValueError, "glue.c::rip_old"):
                check_freeze(root, manifest)
            glue.write_text(original.replace("int\n", "unsigned\n"))
            with self.assertRaisesRegex(ValueError, "glue.c::rip_old"):
                check_freeze(root, manifest)
            glue.write_text(original)
            data.write_bytes(b"changed expectation\n")
            with self.assertRaisesRegex(ValueError, "legacy.c"):
                check_freeze(root, manifest)
            data.unlink()
            with self.assertRaisesRegex(ValueError, "legacy.c"):
                check_freeze(root, manifest)

    def test_baseline_cannot_advance(self):
        with self.assertRaisesRegex(ValueError, "baseline"):
            check_freeze(manifest={"baseline": "a different commit"})


class ReferenceTests(unittest.TestCase):
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

    def test_adoption_record_cannot_lag_pin_or_document_inventory(self):
        with tempfile.TemporaryDirectory(prefix="syncterm-rip3-adoption-test-") as directory:
            root = Path(directory)
            data = {"revision": REFERENCE, "reference_review": "review.json",
                    "documents": [{"path": "contract.md", "sha256": "a" * 64}]}
            review = {"revision": REFERENCE, "previous_revision": "b" * 40,
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


if __name__ == "__main__":
    unittest.main()
