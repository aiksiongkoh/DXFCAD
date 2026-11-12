# Install FreeCAD on Windows

These instructions install the DXFCAD project's tested baseline: FreeCAD 1.1.4
with 64-bit Python 3.11. They prepare the runtime for the DXFCAD compatibility
build; they do not install DXFCAD or a FreeCAD C++ SDK.

## 1. Download

1. Open the official [FreeCAD 1.1.4 release page](https://github.com/FreeCAD/FreeCAD/releases/tag/1.1.4).
2. Download
   [FreeCAD_1.1.4-Windows-x86_64-py311-installer.exe](https://github.com/FreeCAD/FreeCAD/releases/download/1.1.4/FreeCAD_1.1.4-Windows-x86_64-py311-installer.exe).
3. Wait until the download finishes.

## 2. Install

1. In Windows Explorer, open **Downloads**.
2. Double-click `FreeCAD_1.1.4-Windows-x86_64-py311-installer.exe`.
3. If Windows asks for permission, confirm that you launched this installer
   and select **Yes**.
4. Follow the installer prompts. Keep the default installation options and
   directory. The DXFCAD instructions assume `C:\Program Files\FreeCAD 1.1`.
5. If you choose another directory, record it for later build configuration.
6. Finish installation and launch **FreeCAD** from the Start menu.

## 3. Verify the runtime

1. In FreeCAD, select **View > Panels > Python console**.
2. Paste this block into the console and press **Enter**:

   ```python
   import sys, struct
   print(App.Version())
   print(sys.version)
   print(struct.calcsize("P") * 8)
   assert sys.version_info[:2] == (3, 11)
   assert struct.calcsize("P") == 8
   print(App.getUserAppDataDir())
   ```

3. Confirm FreeCAD **1.1.4**, Python **3.11**, and **64** bits are printed,
   with no assertion errors.
4. Record the user data directory printed by the last command. A normal
   DXFCAD installation goes in its `Mod/DXFCAD` subdirectory.
5. Close FreeCAD before building or replacing DXFCAD binaries.

If the Python version or architecture differs, use the specified installer
before continuing with the Python 3.11 build instructions.

## Next step

Install [Git](INSTALL_GIT.md) and the
[Visual Studio C++ tools](INSTALL_VISUAL_STUDIO.md), then follow
[Windows quick installation, section 2](INSTALL_DXFCAD_IN_WINDOWS_QUICK.md#2-download-and-build)
to download and build DXFCAD. For a native C++ document-object build, additional
matching FreeCAD development files are required; see the
[native build requirements](README.md#native-freecad-document-object-build).
