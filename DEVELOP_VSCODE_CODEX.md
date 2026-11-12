# Develop DXFCAD with VS Code and Codex on Windows

Use VS Code to edit and review code, Codex to investigate and implement focused
changes, and FreeCAD to run the workbench. This guide follows the repository's
existing Windows launch tasks: FreeCAD 1.1.4, 64-bit Python 3.11, and the
`build/python311` compatibility build.

Both compatibility and native builds use the C++ domain model. The compatibility
build uses Python lifecycle proxies for FreeCAD document objects; the native
build uses dedicated C++ document objects and requires a matching FreeCAD C++ SDK.

## 1. Prepare the build once

Follow [Windows quick installation](INSTALL_DXFCAD_IN_WINDOWS_QUICK.md) through
**2.6 Run the tests**, or use the [detailed installation guide](INSTALL_WINDOWS.md).
If you already have a configured checkout, reuse it rather than cloning again.
The VS Code workflow below stages its own workbench in an isolated profile;
installing into your normal FreeCAD user profile is optional for development.

You need Git, the x64 MSVC C++ tools and Windows SDK, CMake 3.20+, Ninja,
FreeCAD, matching Python development files, and pybind11. VS Code is the editor;
the Visual Studio C++ tools supply the compiler. Installing VS Code alone does
not supply that compiler.

Confirm these files exist in your checkout:

```text
build/python311/CMakeCache.txt
build/python311/DXFCAD.cp311-win_amd64.pyd
```

Close FreeCAD before rebuilding or replacing an extension it has loaded.

## 2. Install and open VS Code

1. Follow [Install VS Code on Windows](INSTALL_VSCODE.md).
2. Start VS Code. Choose **File > Open Folder...** and select the DXFCAD
   repository folder containing `CMakeLists.txt` and `AGENTS.md`.
3. Open **Extensions** (`Ctrl+Shift+X`). Install these extensions, checking the
   publisher:

   | Extension | Publisher | Purpose |
   | --- | --- | --- |
   | C/C++ | Microsoft | C++ navigation and the `cppvsdbg` launch configuration |
   | Python | Microsoft | Python editing and interpreter selection |
   | Python Debugger | Microsoft | The `debugpy` test-runner launch configurations |
   | CMake Tools | Microsoft | CMake integration; optional when using only repository tasks |
   | Codex | OpenAI | Coding assistance beside the source |

   See Microsoft's [C/C++ guidance](https://code.visualstudio.com/docs/languages/cpp)
   and [Python guidance](https://code.visualstudio.com/docs/python/python-tutorial).
   Use the extension install link in the
   [official Codex IDE documentation](https://learn.chatgpt.com/docs/codex/ide).
4. Open the Command Palette (`Ctrl+Shift+P`), run **Python: Select Interpreter**,
   and select or enter `C:/Program Files/FreeCAD 1.1/bin/python.exe`.
5. If CMake Tools offers to configure before you have aligned its settings in
   section 3, dismiss that prompt and finish the setup below first.

The existing launch files assume FreeCAD is installed under
`C:/Program Files/FreeCAD 1.1`. For another location, update the FreeCAD paths in
`.vscode/launch.json` and `.vscode/settings.json`, and configure CMake with that
installation's Python. Keep the SDK, extension, and FreeCAD runtime matched.

## 3. Align code navigation with your build

The checked-in `.vscode/settings.json` points CMake and C++ IntelliSense at
`build/native`, while the checked-in build and launch tasks use `build/python311`.
For the compatibility workflow in this guide, open that settings file and change
these two values, retaining its other settings:

```json
"cmake.buildDirectory": "${workspaceFolder}/build/python311",
"C_Cpp.default.compileCommands": "${workspaceFolder}/build/python311/compile_commands.json"
```

Keep `CMAKE_EXPORT_COMPILE_COMMANDS` set to `true`. In an **x64 Visual Studio
Developer PowerShell**, change to the repository root and run:

```powershell
cmake -S . -B build/python311 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
```

This reuses the existing cache and its SDK paths. It is not a substitute for
the first configuration in the installation guide. Confirm that
`build/python311/compile_commands.json` now exists. If include errors remain,
run **C/C++: Reset IntelliSense Database** from the Command Palette.

The compatibility build does not compile `src/Native/` and cannot provide the
FreeCAD C++ SDK includes for those files. To develop native document objects,
use the [native SDK build instructions](README.md#native-freecad-document-object-build),
retain the `build/native` navigation settings, and adapt the build task, runtime,
Python, and isolated profile together. The supplied F5 configurations still
target FreeCAD 1.1/Python 3.11; they do not launch the native SDK runtime.
Keep native and compatibility binaries in separate build directories.

## 4. Build and run from VS Code

1. Save your edits and close the FreeCAD instance from the previous run.
2. Press `Ctrl+Shift+B` to run **DXFCAD: build and stage FreeCAD 1.1**.
3. Confirm the terminal reports a successful build and install. The task runs
   `.vscode/prepare-freecad.ps1`, loads the MSVC environment recorded in the
   CMake cache, and stages the complete workbench here:

   ```text
   build/python311/vscode-profile/Mod/DXFCAD/
   ```

4. Open **Run and Debug** (`Ctrl+Shift+D`). Select
   **DXFCAD: FreeCAD 1.1 (manual testing)** and press `F5`.
   This also runs the build/stage task, then launches FreeCAD and activates DXFCAD.
5. Open FreeCAD's **View > Panels > Report view**. Run
   **DXFCAD > Check DXFCAD Workbench** and check for
   `DXFCAD workbench loaded successfully.` This checks UI loading only.
6. In FreeCAD's Python console, inspect the loaded package:

   ```python
   import DXFCAD
   print(DXFCAD.__file__)
   print(DXFCAD.NATIVE_DOCUMENT_OBJECTS)
   ```

   Expect the isolated profile's `Mod/DXFCAD` path and `False` for this build.
7. Exercise the changed feature, inspect Report view, and close FreeCAD before
   the next build. Saved documents and screenshots can be kept under `build/`.

The profile contains its own preferences and temporary files. Editing source
does not update the staged copy until the build/stage task runs again. Normal
VS Code startup is sufficient for this task; manual CMake builds need the
developer shell environment.

## 5. Run the right checks

For terminal commands, work from the repository root. Build first, then list or
run the configured tests:

```powershell
ctest --test-dir build/python311 -N
ctest --test-dir build/python311 -R LWPolyline --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'LWPolyline tests failed' }
```

Replace `LWPolyline` with the relevant test-name pattern. Before finishing a
behavior change, run the broader configured suite when feasible:

```powershell
ctest --test-dir build/python311 --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest failed' }
```

For GUI tests, stage the workbench and close any manual FreeCAD instance using
the profile. Open a `tests/test_*_gui.py` file, select
**DXFCAD: current GUI test (FreeCAD 1.1)** in Run and Debug, and press `F5`.
The active editor must contain the intended test file. There is also a dedicated
**DXFCAD: contour GUI tests (FreeCAD 1.1)** configuration. These launch the
isolated runner, which checks assertions and FreeCAD shutdown.

To run a GUI test without the Python debugger, use a PowerShell terminal after
the build/stage task completes:

```powershell
& 'C:/Program Files/FreeCAD 1.1/bin/python.exe' tests/run_gui_test.py `
    --freecad 'C:/Program Files/FreeCAD 1.1/bin/FreeCAD.exe' `
    --profile build/python311/vscode-profile `
    --test tests/test_polyline_editing_gui.py
if ($LASTEXITCODE -ne 0) { throw 'GUI test failed' }
```

Inspect `gui-test.log` and `gui-test-result.json` under the profile on failure.
Run GUI suites one at a time when sharing this profile. CTest and GUI tests are
separate; passing one does not establish that the other passed.

The Python launch configurations debug `tests/run_gui_test.py`, not Python UI
code inside the child FreeCAD process. The manual launch uses the C++ debugger,
but the default Release build may optimize away useful breakpoint locations.
For C++ stepping, configure a separate matching `RelWithDebInfo` build and point
its build/stage task and launch profile at it. A Python breakpoint in
`dxfcad_*.py` requires a separate debugger attachment setup inside FreeCAD;
that setup is not supplied by the current launch files.

## 6. Work with Codex

Open the Codex icon in VS Code, or run **Codex: Open Codex Sidebar** from the
Command Palette. Sign in when prompted. Codex can use open files and selections
as context; include the intended behavior, affected files, and acceptance checks
in your request. These steps follow the
[official Codex IDE documentation](https://learn.chatgpt.com/docs/codex/ide).
Use local work for this Windows/FreeCAD workflow. Codex also supports native
Windows execution with bounded filesystem and network permissions; see the
[Windows sandbox documentation](https://learn.chatgpt.com/docs/windows/windows-sandbox).

Start each feature with a clean understanding of the working tree. Inspect
`git status --short`, retain any existing work, and create a feature branch when
appropriate, for example `git switch -c feature/polyline-editing-fix`.

For an initial orientation, send:

```text
Read AGENTS.md, ARCHITECTURE.md, and VERIFICATION.md. Explain the objects and
messages involved in LWPolyline editing, from the Python UI through the C++
domain and FreeCAD adapters. Identify the relevant tests. Do not edit yet.
```

For implementation, replace the bracketed text with a specific task:

```text
Implement [one behavior or bug fix] in DXFCAD.
Expected behavior: [input/action and independently checkable result].
Reproduction: [steps, actual result, expected result].
Use the existing object model and inspect ancestors, callers, ownership,
persistence, and tests before changing class behavior. Keep geometry, domain
validation, and DXF semantics in C++; keep UI workflows in Python. Preserve
native DXF data and unrelated behavior. Propose significant architecture or
public-contract changes for my approval before implementing them.
Use build/python311 and the isolated VS Code profile. Run the relevant build,
CTest, and GUI checks. Report exact commands, results, and checks not run.
```

Use one feature per task. Give Codex an error message and full relevant output
when a build or test fails. Ask it to inspect the repository before inventing
new APIs. For new numerical behavior, define analytical examples and justified
tolerances; tests should verify the contract, including rejected inputs and
unchanged state on failure. Check undo/redo, save/reopen, and DXF export when the
feature affects document editing or persistence.

Keep domain declarations and implementations in `include/DXFCAD/` and `src/`,
bindings in `src/Bindings/`, FreeCAD adapters in `src/FreeCAD/` or `src/Native/`,
and Python UI workflows in the root `dxfcad_*.py` modules. Follow the detailed
[source organization and naming rules](README.md#c-source-organization).
Do not modify FreeCAD source or add manufacturing policy to DXFCAD.

## 7. Review and finish a change

1. Open **Source Control** (`Ctrl+Shift+G`) and review each changed file.
2. Check the diff and whitespace:

   ```powershell
   git diff --check
   git diff --stat
   git diff
   git diff --cached --check
   git diff --cached --stat
   git diff --cached
   ```

3. Confirm the build and relevant tests actually ran. Record any unavailable
   runtime or skipped checks; compilation alone does not verify behavior.
4. Perform a focused manual FreeCAD check when the UI changed.
5. Stage only intended source, documentation, and tests, then review the staged
   diff again. Keep generated builds, local SDKs, credentials, and proprietary
   drawings outside commits.
6. Commit the reviewed change with a message describing the resulting behavior.

## Troubleshooting

| Symptom | Action |
| --- | --- |
| F5 says to configure the build first | Complete the initial `build/python311` configuration in the installation guide. |
| Build cannot replace a `.pyd` | Close processes that loaded the extension, then build/stage again. |
| C++ includes appear missing | Check the selected compile database and SDK; reset IntelliSense after reconfiguration. |
| Contour test task cannot find CTest | Its task uses `${env:ProgramFiles}/CMake/bin/ctest.exe`; adjust that path or run `ctest` from a terminal where it is available. |
| `debugpy` cannot load in the selected Python | Use the terminal GUI command above; check Python Debugger support for that interpreter before adding debugger packages. |
| FreeCAD loads old behavior | Rebuild/stage, restart, and inspect `DXFCAD.__file__`. |
| Native FCStd objects fail to restore | Use the matching native SDK build; the compatibility build cannot restore native C++ types. |
| Codex cannot download dependencies or write outside the checkout | Inspect the specific permission failure. Keep build outputs inside `build/`; handle required external installs or permissions explicitly. |
