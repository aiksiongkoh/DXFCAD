# Install Git on Windows

Git downloads DXFCAD's source and tracks development changes. These instructions
target the project's Windows x64 setup.

## 1. Download

1. Open the official [Git for Windows download page](https://git-scm.com/install/windows).
2. Select **Git for Windows/x64 Setup**.
3. Wait until the installer download finishes.

## 2. Install

1. In Windows Explorer, open **Downloads** and double-click the Git installer.
2. If Windows asks for permission, confirm that you launched this installer
   and select **Yes**.
3. Read the license information and click **Next**.
4. Keep the default installation directory and components.
5. On the PATH selection page, select
   **Git from the command line and also from 3rd-party software**.
6. Keep the remaining default options. Click **Next** until **Install** appears,
   then click **Install**.
7. Click **Finish** when installation completes.

## 3. Verify Git

1. Close and reopen any terminal or VS Code window that was open during
   installation so it receives the updated PATH.
2. Open **Windows PowerShell** from the Start menu.
3. Paste this command and press **Enter**:

   ```powershell
   git --version
   ```

4. Confirm it prints a Git version, such as `git version 2.56.0.windows.4`.
   The exact version may differ.

If PowerShell cannot find `git`, reopen the terminal and check that the PATH
option in step 2 was selected.

## Next step

Install the [Visual Studio C++ tools](INSTALL_VISUAL_STUDIO.md), then follow
[Windows quick installation, section 2](INSTALL_DXFCAD_IN_WINDOWS_QUICK.md#2-download-and-build).
That guide includes the clone command. If a DXFCAD checkout already exists,
reuse it rather than cloning into the same folder.
