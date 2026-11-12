# Install DXFCAD on Windows (quick guide)

FreeCAD 1.1.4, 64-bit Python 3.11, compatibility build.

After building and testing, continue with
[Develop DXFCAD with VS Code and Codex](DEVELOP_VSCODE_CODEX.md) for editor setup,
isolated FreeCAD launches, testing, and the daily coding workflow.

## 1. Install software

Separate installation guides are available for [FreeCAD](INSTALL_FREECAD.md),
[Git](INSTALL_GIT.md), [Visual Studio C++ tools](INSTALL_VISUAL_STUDIO.md), and
[VS Code](INSTALL_VSCODE.md).

### 1.1 FreeCAD

1. Download [FreeCAD_1.1.4-Windows-x86_64-py311-installer.exe](https://github.com/FreeCAD/FreeCAD/releases/download/1.1.4/FreeCAD_1.1.4-Windows-x86_64-py311-installer.exe).
2. In Windows Explorer,  
   Open folder **Downloads**  
   DoubleClick/FreeCAD_1.1.4-Windows-x86_64-py311-installer.exe
3. If Windows asks for permission, confirm that you launched this installer and select **Yes**.
4. Follow the installer prompts. Keep the default options and installation
   directory. Record the directory if you change it.
5. Finish installation and launch **FreeCAD** from the Start menu.
6. In FreeCAD,  
   Click Menu Item/View/Panels/Python console/
7. In Python console, paste:

   ```python
   import sys, struct
   print(App.Version())
   print(sys.version)
   print(struct.calcsize("P") * 8)
   assert sys.version_info[:2] == (3, 11)
   assert struct.calcsize("P") == 8
   print(App.getUserAppDataDir())
   ```

8. Confirm FreeCAD **1.1.4**, Python **3.11**, and **64** bits, with no assertion errors.
9. Record the user data directory printed by the last command. Close FreeCAD.

### 1.2 Git

1. Download [Git-2.56.0.4-64-bit.exe](https://github.com/git-for-windows/git/releases/download/v2.56.0.windows.4/Git-2.56.0.4-64-bit.exe)
    from the official [Git for Windows download page](https://git-scm.com/install/windows).
2. In Windows Explorer,  
    Open folder **Downloads**  
    DoubleClick/Git-2.56.0.4-64-bit.exe
3. If Windows asks for permission, confirm that you launched this installer and select **Yes**.
4. Read the license information. Click **Next**.
5. Keep the default installation directory and components. Click **Next** through those pages.
6. On the PATH selection page, select **Git from the command line and also from 3rd-party software**.
7. Keep the remaining default options. Click **Next** until **Install** appears, then click **Install**.
8. When installation finishes, click **Finish**.
9. Click the Windows **Start** button.  
    Type `Windows PowerShell` in the search box.  
    Click **Windows PowerShell** in the search results.  
    In the new PowerShell window, click after the `PS ...>` prompt.  
    Copy the command below, paste it with **Ctrl+V**, and press **Enter**:

    ```powershell
    git --version
    ```

10. Confirm a version such as `git version 2.56.0.windows.4` appears. Close PowerShell.

### 1.3 Visual Studio Community

1. Open the [Visual Studio Community download page](https://visualstudio.microsoft.com/vs/community/).  
    Click **Free download** for **Visual Studio Community**.
2. In Windows Explorer,  
    Open folder **Downloads**  
    DoubleClick/VisualStudioSetup.exe
3. If Windows asks for permission, confirm that you launched this installer and select **Yes**.
4. In **Visual Studio Installer**, review the license and privacy information.  
    Click **Continue** to prepare the installer.
5. On the **Workloads** tab, select **Desktop development with C++**.
6. In **Installation details**, confirm these components are selected:  
    **MSVC C++ x64/x86 build tools** (the current toolset),  
    **Windows SDK**, and  
    **C++ CMake tools for Windows** (CMake 3.20+ and Ninja).  
    Use the **Individual components** tab if a required component is not selected.
7. Keep the default installation locations. Click **Install**.
8. Wait for the download and installation to finish. Restart Windows if requested.
9. Close Visual Studio Installer. Continue with **2. Download and build** below.

## 2. Download and build

### 2.1 Open the build shell

1. Click the Windows **Start** button.
2. Type `Developer PowerShell` in the search box.
3. Click **Developer PowerShell for VS** for your installed Visual Studio Community version.
4. In the new window, click after the `PS ...>` prompt.  
   Copy the command below, paste it with **Ctrl+V**, and press **Enter**:

   ```powershell
   Enter-VsDevShell -VsInstallPath $env:VSINSTALLDIR -Arch amd64 -HostArch amd64
   ```

5. Paste these commands and press **Enter**:

   ```powershell
   $env:VSCMD_ARG_TGT_ARCH
   Get-Command cl
   cmake --version
   ninja --version
   ```

6. Confirm the target architecture is `x64`, `cl` is found, and CMake and Ninja
   print their versions. Keep this window open.

Run all remaining PowerShell commands in this same window. Stop on any error.

### 2.2 Download DXFCAD

1. Click after the PowerShell prompt.
2. Copy the entire block below, paste it with **Ctrl+V**, and press **Enter**.
   Copy only the commands inside the box, without the Markdown backticks.

   ```powershell
   $ErrorActionPreference = 'Stop'
   $sourceRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'GitHub'
   New-Item -ItemType Directory -Force $sourceRoot | Out-Null
   Set-Location $sourceRoot
   git clone https://github.com/aiksiongkoh/DXFCAD.git
   if ($LASTEXITCODE -ne 0) { throw 'Clone failed' }
   Set-Location DXFCAD
   $repo = (Get-Location).Path
   $freecadPython = 'C:/Program Files/FreeCAD 1.1/bin/python.exe'
   ```

3. Wait until cloning finishes and the `PS ...>` prompt returns.
4. If FreeCAD was installed elsewhere, replace the path in the following
   command with your installation's `bin/python.exe` path, then run it:

   ```powershell
   $freecadPython = 'C:/Program Files/FreeCAD 1.1/bin/python.exe'
   ```

5. Paste and run:

   ```powershell
   & $freecadPython -c "import sys, struct; print(sys.version); assert sys.version_info[:2] == (3, 11); assert struct.calcsize('P') == 8"
   if ($LASTEXITCODE -ne 0) { throw 'FreeCAD Python check failed' }
   Write-Output $repo
   ```

6. Confirm Python **3.11** is printed without an assertion error. Record the
   repository folder printed by `$repo`.
7. If cloning reports that `DXFCAD` already exists, stop. For an existing
   checkout, open that folder and follow the [detailed guide](INSTALL_WINDOWS.md)
   rather than cloning over it.

### 2.3 Download build dependencies

1. In the same PowerShell window, copy and run this block once:

   ```powershell
   New-Item -ItemType Directory -Force build/dependencies | Out-Null
   Invoke-WebRequest 'https://www.nuget.org/api/v2/package/python/3.11.9' -OutFile build/dependencies/python.3.11.9.zip
   Expand-Archive build/dependencies/python.3.11.9.zip -DestinationPath build/dependencies/python311
   $pythonSdk = (Resolve-Path build/dependencies/python311/tools).Path
   $pybindTarget = Join-Path $repo 'build/dependencies/pybind11'
   & "$pythonSdk/python.exe" -m pip install --disable-pip-version-check --target $pybindTarget 'pybind11==3.0.4'
   if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed' }
   $pybindCmake = Join-Path $pybindTarget 'pybind11/share/cmake/pybind11'
   ```

2. Wait for the download, extraction, and pip installation to finish.
3. Confirm pip reports successful installation of `pybind11-3.0.4`.
4. If extraction reports that the destination already exists, stop; reuse the
   existing dependencies using the [detailed guide](INSTALL_WINDOWS.md).

### 2.4 Configure the build

1. Copy the complete block below, paste it into the same PowerShell window,
   and press **Enter**. Keep each backtick at the end of its line.

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
   if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
   ```

2. Wait for `Configuring done`, `Generating done`, and
   `Build files have been written to` messages.

### 2.5 Compile DXFCAD

1. Paste and run:

   ```powershell
   cmake --build build/python311 --parallel 6
   if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
   ```

2. Wait until compilation and linking finish and the PowerShell prompt returns.
   Keep FreeCAD closed while building.

### 2.6 Run the tests

1. Paste and run:

   ```powershell
   ctest --test-dir build/python311 --output-on-failure
   if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
   ```

2. Wait for the test summary. Confirm it reports **100% tests passed**.
3. If any tests fail, stop and retain the error output before installing.

### 2.7 Prepare the installation folder

1. Paste and run:

   ```powershell
   cmake --install build/python311 --prefix "$repo/build/stage/DXFCAD"
   if ($LASTEXITCODE -ne 0) { throw 'Staging failed' }
   ```

2. Wait for the installation messages to finish. The prepared workbench is
   now in `build/stage/DXFCAD` inside your repository folder.

## 3. Install DXFCAD

### 3.1 Copy the workbench

1. Confirm FreeCAD is closed.
2. In the same PowerShell window, paste and run:

   ```powershell
   explorer.exe "$repo/build/stage"
   ```

3. Leave this Windows Explorer window open.
4. Press **Win+E** to open another Windows Explorer window.
5. Press **Ctrl+L**. Paste the user data directory recorded in section 1.1.
   Press **Enter**.
6. Open folder **Mod**. If it does not exist, press **Ctrl+Shift+N**, type
   `Mod`, press **Enter**, and open the new folder.
7. If a **DXFCAD** folder already exists there, move it outside **Mod** as a
   backup before continuing.
8. Return to the first Windows Explorer window showing `build/stage`.
   Click the **DXFCAD** folder once and press **Ctrl+C**.
9. Return to the second window showing **Mod**. Press **Ctrl+V** to paste the
   copied **DXFCAD** folder. Wait for copying to finish.
10. Open folder **DXFCAD**. Confirm `Init.py`, `InitGui.py`, and
    `DXFCAD.cp311-win_amd64.pyd` are directly inside it.
    The path must end in `Mod/DXFCAD`, not `Mod/DXFCAD/DXFCAD`.

### 3.2 Load and check DXFCAD

1. Click the Windows **Start** button. Search for `FreeCAD` and open it.
2. Click the workbench selector in the top toolbar. Select **DXFCAD**.
3. In FreeCAD,  
   Click Menu Item/View/Panels/Report view/
4. Click Menu Item/DXFCAD/Check DXFCAD Workbench/
5. In **Report view**, confirm:
   `DXFCAD workbench loaded successfully.`
6. Click Menu Item/View/Panels/Python console/
7. In Python console, paste these commands, pressing **Enter** to execute them:

   ```python
   import DXFCAD
   print(DXFCAD.__file__)
   print(DXFCAD.NATIVE_DOCUMENT_OBJECTS)
   ```

8. Confirm the printed module path is inside your installed `Mod/DXFCAD`
   folder and the final value is `False` for this compatibility build.

### 3.3 Import, save, and reopen a drawing

1. Choose a small DXF drawing for the first check.
2. In FreeCAD,  
   Click Menu Item/DXFCAD/Import DXF/DWG.../
3. In the file dialog, browse to the DXF, click its filename, and click **Open**.
4. Wait for the import to finish. Click inside the drawing view, then press
   **V**, then **F**, to fit the drawing. Check the geometry and **Report view** for errors.
5. Click Menu Item/File/Save As/
6. Choose a folder, enter a filename ending in `.FCStd`, and click **Save**.
7. Click Menu Item/File/Close/ to close the saved document.
8. Click Menu Item/File/Open/. Select the saved `.FCStd` file and click **Open**.
9. Confirm the drawing reappears and **Report view** shows no restore errors.

### 3.4 Optional: enable DWG import

1. Open the [ODA File Converter download page](https://www.opendesign.com/guestfiles/oda_file_converter).
2. Scroll to **Downloads**. Click **Download** for **Windows 10 or later (x64)**.
3. Wait for the `.msi` installer to finish downloading. The current filename is
   `ODAFileConverter_QT6_vc16_amd64dll_27.10.msi`; the version suffix may change.
4. In Windows Explorer,  
   Open folder **Downloads**  
   DoubleClick/ODAFileConverter_QT6_vc16_amd64dll_27.10.msi
5. Follow the installation wizard. Review and accept the license terms to proceed.
6. Keep the default installation folder. Record the folder shown by the installer.
7. Click **Install** when offered. If Windows asks for permission, confirm that
   you launched this installer and select **Yes**.
8. Wait for installation to finish. Click **Finish** to close the wizard.
9. In Windows Explorer, press **Ctrl+L**, enter the installation folder recorded
   above, and press **Enter**. Confirm `ODAFileConverter.exe` is inside it.
10. Record the full executable path: the installation folder followed by
    `\ODAFileConverter.exe`.
11. In FreeCAD,  
    Click Menu Item/Edit/Preferences/
12. Select **Import/Export**, then **DWG**. If the DWG settings are missing,
    close Preferences, activate the **Draft** workbench, and reopen Preferences.
13. Set **Conversion method** to **ODA Converter**.
14. In the ODA converter path setting, browse to and select `ODAFileConverter.exe`
    using the full path recorded above. Click **OK** to save the preferences.
15. Select **DXFCAD** in the workbench selector.
16. Click Menu Item/DXFCAD/Import DXF/DWG.../  
    Browse to a small `.dwg` file, select it, and click **Open**.
17. Wait for conversion and import to finish. Check the drawing and **Report view**
    for errors.

[Detailed instructions and troubleshooting](INSTALL_WINDOWS.md).
