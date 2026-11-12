# Install Visual Studio C++ tools on Windows

Visual Studio supplies the compiler and build tools for DXFCAD's C++ code.
You can edit the source in VS Code while using these tools to compile it.
DXFCAD requires CMake 3.20 or newer and a C++17 compiler for its domain library.

## 1. Download

1. Open the official [Visual Studio Community download page](https://visualstudio.microsoft.com/vs/community/).
2. Download **Visual Studio Community**.
3. In Windows Explorer, open **Downloads** and double-click
   `VisualStudioSetup.exe`.
4. If Windows asks for permission, confirm that you launched this installer
   and select **Yes**.
5. Review the license and privacy information, then click **Continue**.

Microsoft's [C++ installation guide](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation)
describes the installer and workload selection.

## 2. Select the C++ workload

1. In **Visual Studio Installer**, select **Desktop development with C++**
   on the **Workloads** tab.
2. In **Installation details**, confirm these components are selected:

   - MSVC C++ x64/x86 build tools.
   - Windows SDK.
   - C++ CMake tools for Windows, including CMake and Ninja.

3. Use **Individual components** if a required component is missing.
4. Keep the default installation locations and click **Install**.
5. Wait for installation to finish. Restart Windows if requested.

If Visual Studio is already installed, open **Visual Studio Installer**, click
**Modify** for that installation, and add the workload and components above.

## 3. Verify the x64 build environment

1. Open the Windows **Start** menu and search for `Developer PowerShell`.
2. Open **Developer PowerShell for VS** for your installed Visual Studio version.
3. Paste this command and press **Enter**:

   ```powershell
   Enter-VsDevShell -VsInstallPath $env:VSINSTALLDIR -Arch amd64 -HostArch amd64
   ```

4. Run:

   ```powershell
   $env:VSCMD_ARG_TGT_ARCH
   Get-Command cl
   cmake --version
   ninja --version
   ```

5. Confirm the architecture is **x64**, `cl` is found, and CMake and Ninja print
   versions. CMake must be at least **3.20**.

Use this developer shell for the manual DXFCAD configure and build commands.
If a tool is missing, check the installed components and reopen the shell.

## Next step

Follow [Windows quick installation, section 2](INSTALL_DXFCAD_IN_WINDOWS_QUICK.md#2-download-and-build)
to download and build DXFCAD. Install [VS Code](INSTALL_VSCODE.md) for the
editor workflow. Building native FreeCAD document objects additionally requires
a matching FreeCAD C++ SDK; the compiler installation alone does not supply it.
