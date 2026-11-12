# Install VS Code on Windows

VS Code is the editor used for DXFCAD source changes, reviews, and launch tasks.
Install the [Visual Studio C++ tools](INSTALL_VISUAL_STUDIO.md) separately to
compile DXFCAD.

## 1. Download and install

1. Open the official [VS Code Windows installation guide](https://code.visualstudio.com/docs/setup/windows).
2. Follow its download link and select the Windows **x64 User Installer**.
3. In Windows Explorer, open **Downloads** and double-click the downloaded
   `VSCodeUserSetup-...exe` installer.
4. Read and accept the license terms to continue.
5. Keep the default installation location.
6. On **Select Additional Tasks**, keep **Add to PATH** selected. You may also
   enable **Open with Code** in the Explorer context menu.
7. Click **Install**, then **Finish** to launch VS Code.

The user installer installs for your Windows account. Microsoft's guide also
provides a system installer for installations managed for all users.

## 2. Verify the installation

1. Open VS Code from the Start menu.
2. Select **Help > About** and confirm version information is shown.
3. Open a new Windows PowerShell window and run:

   ```powershell
   code --version
   ```

4. Confirm version information is printed. If `code` is not found, close and
   reopen the terminal after installation and check the installer's PATH option.

## 3. Open DXFCAD

1. In VS Code, select **File > Open Folder...**.
2. Select your DXFCAD checkout: the folder containing `AGENTS.md`,
   `CMakeLists.txt`, and `.vscode`.
3. Open `README.md` to confirm that you selected the repository root.

If you have not downloaded DXFCAD yet, follow the clone instructions in
[Windows quick installation](INSTALL_DXFCAD_IN_WINDOWS_QUICK.md#22-download-dxfcad).

## 4. Install development extensions

1. Open **Extensions** with `Ctrl+Shift+X`.
2. Search for each extension below, check its publisher, and click **Install**:

   | Extension | Publisher | Use in DXFCAD |
   | --- | --- | --- |
   | C/C++ | Microsoft | C++ editing and the Windows C++ debugger |
   | Python | Microsoft | Python editing and interpreter selection |
   | Python Debugger | Microsoft | Python test-runner launch configurations |
   | CMake Tools | Microsoft | Optional CMake integration |

3. Reload VS Code if prompted.

See Microsoft's [C/C++ documentation](https://code.visualstudio.com/docs/languages/cpp)
and [Python documentation](https://code.visualstudio.com/docs/python/python-tutorial)
for extension setup details.

## Next step

Follow [Develop DXFCAD with VS Code and Codex](DEVELOP_VSCODE_CODEX.md) to set up
Codex, select FreeCAD's Python interpreter, align IntelliSense with your build,
and use the repository's build, launch, and test configurations. Complete the
initial DXFCAD build before using its F5 launch configurations.
