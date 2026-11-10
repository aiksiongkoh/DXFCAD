# Install FreeCAD and DXFCAD from scratch on Windows

This guide targets 64-bit Windows and the tested FreeCAD 1.1.4 / Python 3.11
combination. It installs FreeCAD, builds DXFCAD from source, and installs the
workbench in your user profile. A FreeCAD source checkout is not required.

This is the **compatibility build**: DXFCAD's C++ domain library supplies entity
data and algorithms, while FreeCAD objects use Python document proxies. The
optional native C++ FreeCAD document objects need a matching FreeCAD C++ SDK;
see [the native build instructions](README.md#native-freecad-document-object-build).
The compatibility build cannot restore FCStd documents saved with those native
C++ object types.

## 1. Install FreeCAD

1. Download the Windows x64 installer from the official
   [FreeCAD 1.1.4 release](https://github.com/FreeCAD/FreeCAD/releases/tag/1.1.4).
   The [FreeCAD download page](https://www.freecad.org/downloads.php) also links
   to available releases. This guide uses 1.1.4 as a tested baseline, rather
   than assuming every newer release uses the same Python version.
2. Run the installer. The commands below assume the installation directory is
   `C:\Program Files\FreeCAD 1.1`; adjust `$freecadBin` if yours differs.
3. Start FreeCAD and enable **View > Panels > Python console**. Run:

   ```python
   import sys, struct
   print(App.Version())
   print(sys.version)
   print(struct.calcsize("P") * 8)
   print(App.getUserAppDataDir())
   ```

   Check that Python is **3.11** and the architecture is **64**. Keep the user
   data directory for step 6. If Python has a different major/minor version,
   stop here: the Python SDK and DXFCAD binary must match that runtime. The
   Python 3.11 SDK below is not suitable for Python 3.12 or 3.14.
4. Close FreeCAD before building or replacing a loaded DXFCAD extension.

## 2. Install the build tools

Install these tools using their official installers:

- [Git for Windows](https://git-scm.com/downloads/win), with Git available from
  the command line.
- [Visual Studio Community or Build Tools](https://visualstudio.microsoft.com/downloads/).
  Select **Desktop development with C++**, including the x64 MSVC compiler,
  a Windows SDK, and **C++ CMake tools for Windows** (CMake and Ninja).
  See Microsoft's [CMake installation guidance](https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio#installation).

DXFCAD requires CMake 3.20 or newer and a C++17 compiler. The existing Windows
builds were tested with MSVC 19.51. VS Code is optional.

Open **Developer PowerShell for Visual Studio** from the Start menu. Ensure
the target architecture is x64; if necessary, run
`Enter-VsDevShell -Arch amd64 -HostArch amd64` in that developer shell.
Use this shell for all PowerShell commands below. Check the tools:

```powershell
git --version
cmake --version
ninja --version
Get-Command cl
```

If a command is missing, modify the tool installation and reopen the developer
shell. A separate [CMake installer](https://cmake.org/download/) can provide
CMake if needed; select its option to add CMake to PATH.

## 3. Download DXFCAD

Choose a source directory you own. This example uses `Documents\GitHub`:

```powershell
$sourceRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'GitHub'
New-Item -ItemType Directory -Force $sourceRoot | Out-Null
Set-Location $sourceRoot
git clone https://github.com/aiksiongkoh/DXFCAD.git
if ($LASTEXITCODE -ne 0) { throw 'DXFCAD clone failed' }
Set-Location DXFCAD
$repo = (Get-Location).Path
$freecadBin = 'C:/Program Files/FreeCAD 1.1/bin'
$freecadPython = Join-Path $freecadBin 'python.exe'
& $freecadPython -c "import sys, struct; print(sys.version); assert sys.version_info[:2] == (3, 11); assert struct.calcsize('P') == 8"
if ($LASTEXITCODE -ne 0) { throw 'This guide requires FreeCAD with 64-bit Python 3.11' }
```

If you already cloned DXFCAD, open its directory instead of cloning it again.
Keep this shell open so the variables remain available. Downloads in the next
step require internet access.

## 4. Obtain Python development files and pybind11

FreeCAD supplies its Python interpreter but the installed package does not
supply the headers and import library needed to compile DXFCAD. Obtain those
from the official [64-bit Python 3.11.9 NuGet package](https://www.nuget.org/packages/python/3.11.9).
These development files have been used with FreeCAD's Python 3.11.14 runtime.

Run once from the repository root:

```powershell
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force build/dependencies | Out-Null
Invoke-WebRequest 'https://www.nuget.org/api/v2/package/python/3.11.9' -OutFile build/dependencies/python.3.11.9.zip
Expand-Archive build/dependencies/python.3.11.9.zip -DestinationPath build/dependencies/python311
$pythonSdk = (Resolve-Path build/dependencies/python311/tools).Path
$sdkPython = Join-Path $pythonSdk 'python.exe'
$pybindTarget = Join-Path $repo 'build/dependencies/pybind11'
& $sdkPython -m pip install --disable-pip-version-check --target $pybindTarget 'pybind11==3.0.4'
if ($LASTEXITCODE -ne 0) { throw 'pybind11 installation failed' }
$pybindCmake = Join-Path $pybindTarget 'pybind11/share/cmake/pybind11'
foreach ($required in @("$pythonSdk/include/Python.h", "$pythonSdk/libs/python311.lib", "$pybindCmake/pybind11Config.cmake")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing dependency: $required" }
}
```

This uses the SDK interpreter's pip and installs pybind11 into the ignored
`build` directory. It does not install packages into FreeCAD or require a
system Python installation. pybind11's [installation documentation](https://pybind11.readthedocs.io/en/stable/installing.html#include-with-pypi)
describes its Python package, which includes headers and CMake files.

On a subsequent session, reuse the extracted SDK and pybind11 directory by
setting the variables again; do not repeat `Expand-Archive` into an existing
directory.

## 5. Configure, build, test, and stage DXFCAD

From the same developer PowerShell and repository directory:

```powershell
cmake -S . -B build/python311 -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DDXFCAD_BUILD_PYTHON=ON `
    -DDXFCAD_BUILD_NATIVE_OBJECTS=OFF `
    "-DPython_EXECUTABLE=$freecadPython" `
    "-DPython_INCLUDE_DIR=$pythonSdk/include" `
    "-DPython_LIBRARY=$pythonSdk/libs/python311.lib" `
    "-Dpybind11_DIR=$pybindCmake" `
    "-DDXFCAD_FREECAD_PYTHON_EXECUTABLE=$freecadPython"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build build/python311 --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'DXFCAD build failed' }
ctest --test-dir build/python311 --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'DXFCAD tests failed; inspect the output before installing' }
cmake --install build/python311 --prefix "$repo/build/stage/DXFCAD"
if ($LASTEXITCODE -ne 0) { throw 'DXFCAD staging failed' }
```

PowerShell backticks must be the final character on their line. The staging
command creates a complete workbench folder; it has not yet installed DXFCAD
in your FreeCAD profile. The stage should include `Init.py`, `InitGui.py`,
the `dxfcad_*.py` modules, and `DXFCAD.cp311-win_amd64.pyd`.

CTest verifies domain and FreeCAD integration behavior. Interactive GUI checks
are separate; the next steps check that the installed workbench loads.

## 6. Install the staged workbench

Use the exact user data directory printed by FreeCAD in step 1. In File
Explorer, create a `Mod` folder under that directory if needed. Copy the entire
`build\stage\DXFCAD` folder from the repository into `Mod`.

The resulting layout must be:

```text
<FreeCAD user data directory>/
  Mod/
    DXFCAD/
      Init.py
      InitGui.py
      DXFCAD.cp311-win_amd64.pyd
      dxfcad_workbench.py
      ...other staged Python files...
```

Do not nest another `DXFCAD` directory inside `Mod/DXFCAD`. Copying only the
repository source leaves out the compiled extension. If replacing a previous
installation, close FreeCAD and move the old `Mod/DXFCAD` folder outside `Mod`
as a backup before copying the new staged folder.

## 7. Verify the installation

1. Start FreeCAD and select **DXFCAD** in the workbench selector.
2. Enable **View > Panels > Report view** and run
   **DXFCAD > Check DXFCAD Workbench**. Expect
   `DXFCAD workbench loaded successfully.` This checks UI loading.
3. In FreeCAD's Python console, run:

   ```python
   import DXFCAD
   print(DXFCAD.__file__)
   print(DXFCAD.NATIVE_DOCUMENT_OBJECTS)
   ```

   The module path should point into your installed `Mod/DXFCAD` folder. For
   this compatibility build, `NATIVE_DOCUMENT_OBJECTS` should be `False`.
4. Use **DXFCAD > Import DXF/DWG...** to import a small DXF drawing. Inspect the
   geometry and Report view, save an FCStd document, close it, and reopen it.
   DXFCAD must remain installed to restore its document proxies.

DWG import additionally requires an external converter configured in FreeCAD
under **Edit > Preferences > Import/Export > DWG**. DXFCAD reuses FreeCAD's
converter configuration; see [DWG import details](README.md#import-and-display-a-drawing).
Plain DXF import does not require a DWG converter.

## Troubleshooting and updating

| Symptom | Check |
| --- | --- |
| `cl`, `cmake`, or `ninja` is missing | Use the x64 Visual Studio developer shell and install the C++ workload components from step 2. |
| CMake cannot find Python development files | Check `Python.h`, `python311.lib`, and the explicit paths in steps 4 and 5. |
| CMake cannot find pybind11 | `pybind11_DIR` must contain `pybind11Config.cmake`; it is not the top-level pip target directory. |
| Extension fails to load or reports a DLL error | Check 64-bit architecture and Python 3.11 in both the SDK and FreeCAD. Rebuild in a new build directory when changing runtimes. |
| DXFCAD is absent from the selector | Check `Mod/DXFCAD/InitGui.py`, folder nesting, and Report view; restart FreeCAD. |
| An older module loads | Inspect `DXFCAD.__file__` and move duplicate installations out of FreeCAD's module directories. |
| Rebuild cannot replace the `.pyd` file | Close every FreeCAD or Python process using that build. |

To update, close FreeCAD, pull the desired DXFCAD revision into a clean working
tree, repeat the build/test/stage commands, and replace the installed folder as
described in step 6. Do not reuse a build directory across Python major/minor
versions or between compatibility and native FreeCAD SDK builds.
