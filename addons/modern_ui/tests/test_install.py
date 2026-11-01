# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('modern_install', ROOT / 'install.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallerTests(unittest.TestCase):
    def test_install_remove_and_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scripts = root / 'scripts'
            scripts.mkdir()
            other = scripts / 'my_script.wren'
            other.write_text('// user script', encoding='utf-8')
            installer.install(scripts)
            installer.install(scripts)
            for source in installer.files():
                target = scripts / source.relative_to(ROOT / 'scripts')
                self.assertEqual(target.read_bytes(), source.read_bytes())
            archive = root / 'addon.zip'
            installer.package(archive)
            with zipfile.ZipFile(archive) as bundle:
                self.assertIn('syncterm-modern-ui/scripts/auto/menu/main_menu.wren', bundle.namelist())
                self.assertIn('syncterm-modern-ui/LICENSE', bundle.namelist())
            installer.uninstall(scripts)
            self.assertEqual(list(scripts.rglob('*.wren')), [other])

    def test_existing_override_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            scripts = Path(directory)
            override = scripts / 'auto/menu/main_menu.wren'
            override.parent.mkdir(parents=True)
            override.write_text('// another frontend', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'Existing script'):
                installer.install(scripts)
            self.assertEqual(list(scripts.rglob('*.wren')), [override])
            self.assertEqual(override.read_text(encoding='utf-8'), '// another frontend')

    def test_modified_script_is_preserved_on_remove(self):
        with tempfile.TemporaryDirectory() as directory:
            scripts = Path(directory)
            installer.install(scripts)
            modified = scripts / 'modern_ui.wren'
            modified.write_text('// customized', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'Modified script'):
                installer.uninstall(scripts)
            self.assertTrue((scripts / 'auto/menu/main_menu.wren').is_file())

    def test_failed_install_rolls_back(self):
        with tempfile.TemporaryDirectory() as directory:
            scripts = Path(directory)
            original = Path.open
            def failing_open(path, *args, **kwargs):
                if path == scripts / 'auto/menu/main_menu.wren' and args == ('xb',):
                    raise OSError('injected write error')
                return original(path, *args, **kwargs)
            with patch.object(Path, 'open', failing_open):
                with self.assertRaisesRegex(OSError, 'injected'):
                    installer.install(scripts)
            self.assertEqual(list(scripts.rglob('*.wren')), [])


if __name__ == '__main__':
    unittest.main()
