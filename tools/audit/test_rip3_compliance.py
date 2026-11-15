#!/usr/bin/env python3
"""Exercise the source-preservation guard against actual file/body changes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
from pathlib import Path
import tempfile
import unittest

from rip3_compliance import BASELINE, c_functions, check_freeze, digest


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


if __name__ == "__main__":
    unittest.main()
