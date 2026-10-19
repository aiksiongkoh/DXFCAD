"""Guard the UTF-8 text compilation used by FreeCAD's workbench loader.

Importing Python files or using runpy can consume a BOM, masking a startup
failure: FreeCAD reads Init.py/InitGui.py as UTF-8 text before compiling them.
"""

from pathlib import Path
import unittest


class WorkbenchStartupTests(unittest.TestCase):
    def test_entry_points_compile_as_utf8_text(self):
        root = Path(__file__).resolve().parents[1]
        for name in ("Init.py", "InitGui.py"):
            with self.subTest(entry_point=name):
                path = root / name
                compile(path.read_text(encoding="utf-8"), str(path), "exec")


if __name__ == "__main__":
    unittest.main()
