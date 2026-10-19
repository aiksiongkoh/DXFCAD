"""Run a GUI test in its own FreeCAD application, including checked shutdown.

Launch with ordinary Python; do not import FreeCADGui into the launcher.
The profile must be inside this workspace and contain Mod/DXFCAD.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--freecad", required=True, type=Path)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--test", type=Path,
                        default=Path(__file__).with_name("test_circle_selection_gui.py"))
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[1]
    profile = args.profile.resolve()
    profile.relative_to(workspace)
    if not (profile / "Mod/DXFCAD/Init.py").is_file():
        parser.error("Install DXFCAD into the isolated profile's Mod/DXFCAD first")
    test = args.test.resolve()
    test.relative_to(workspace / "tests")
    result = profile / "gui-test-result.json"
    result.unlink(missing_ok=True)
    macro = profile / "run-test.FCMacro"
    # FreeCAD owns Qt, Coin, and Python startup/shutdown. Run the test after its
    # event loop starts; close this isolated application after recording results.
    macro.write_text("""import FreeCAD
import FreeCADGui
from PySide import QtCore
import json
import runpy
import traceback
from pathlib import Path

def run_test():
    try:
        # Keep FreeCAD's __main__ console namespace intact for GUI callbacks.
        runpy.run_path(TEST_PATH, run_name='dxfcad_gui_test')
        status = {'passed': True}
    except BaseException:
        status = {'passed': False, 'traceback': traceback.format_exc()}
        FreeCAD.Console.PrintError(status['traceback'])
    finally:
        Path(RESULT_PATH).write_text(json.dumps(status), encoding='utf-8')
        FreeCADGui.Selection.clearSelection()
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)

QtCore.QTimer.singleShot(0, run_test)
""".replace("TEST_PATH", repr(str(test))).replace("RESULT_PATH", repr(str(result))),
                     encoding="utf-8")
    env = os.environ.copy()
    env["FREECAD_USER_HOME"] = str(profile)
    env["FREECAD_USER_TEMP"] = str(profile)
    # Apply only to this automated process tree. A native fault must return a
    # failing exit code, rather than leave a modal Windows crash dialog open.
    if os.name == "nt":
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    log = profile / "gui-test.log"
    with log.open("w", encoding="utf-8") as output:
        try:
            completed = subprocess.run(
                [str(args.freecad.resolve()), "--user-cfg", str(profile / "user.cfg"),
                 "--system-cfg", str(profile / "system.cfg"), str(macro)],
                env=env, stdout=output, stderr=subprocess.STDOUT, timeout=120)
        except subprocess.TimeoutExpired:
            print(f"FAIL: GUI test did not shut down within 120 seconds; see {log}")
            return 1
    status = json.loads(result.read_text(encoding="utf-8")) if result.exists() else {}
    if completed.returncode != 0 or not status.get("passed"):
        print(f"FAIL: FreeCAD exit code {completed.returncode}; see {log}")
        if status.get("traceback"):
            print(status["traceback"])
        return 1
    print("PASS: GUI checks and FreeCAD shutdown completed (exit code 0)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
