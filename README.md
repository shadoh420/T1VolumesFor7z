# T1VolumesFor7z

This plugin lets 7-Zip open, extract, create, and change Starsiege: Tribes (Tribes 1) and Starsiege `.vol` archives.

## Supported Formats

- **Read and extract:** Tribes 1 (`PVOL`) and Starsiege (` VOL`) volumes with Store (`0`), RLE (`1`), and LZH (`3`) compression.
- **Create and change:** Add, update, delete, and rename files in `.vol` archives. The plugin writes Store (`0`) by default and supports optional RLE (`1`) compression.
- **ZIP volumes:** ZIP-based `.vol` and Tribes 2 `.vl2` archives open with the built-in 7-Zip ZIP handler.

## Requirements

- Windows (64-bit or 32-bit)
- Official 7-Zip (`7z.exe`, `7zG.exe`, or `7zFM.exe`)

This plugin uses a static C runtime. You do not need to install the Visual C++ Redistributable.

## Install the Plugin

### Automatic Install

1. Download `T1VolumesFor7z-windows.zip` from the Releases page.
2. Extract the ZIP archive to a folder.
3. Open PowerShell as Administrator.
4. Run the install script:

```powershell
.\install.ps1
```

5. Restart 7-Zip File Manager if it is open.

### Manual Install

1. Open your 7-Zip installation folder (usually `C:\Program Files\7-Zip`).
2. Create a folder with the name `Formats` if it does not exist.
3. Copy the DLL that matches your 7-Zip version into `Formats`:
   - For 64-bit 7-Zip, copy `TribesVol.64.dll`.
   - For 32-bit 7-Zip, copy `TribesVol.32.dll`.
4. Restart 7-Zip File Manager if it is open.

> **NOTE:** Do not put both `TribesVol.64.dll` and `TribesVol.32.dll` in the same `Formats` folder. 7-Zip shows a load warning for the DLL that does not match its architecture.

To verify the installation, run this command:

```powershell
7z i | Select-String TribesVol
```

## Remove the Plugin

1. Open PowerShell as Administrator.
2. Run `.\uninstall.ps1`.

As an alternative, delete `TribesVol.*.dll` from the 7-Zip `Formats` folder.

## Command-Line Examples

```powershell
# List files in a volume
7z l Entities.vol

# Test a volume
7z t Entities.vol

# Extract a volume
7z x gui.vol -oextracted_gui

# Create a volume (Store compression)
7z a mymod.vol .\assets\*

# Create a volume with RLE compression
7z a -m0=RLE mymod_rle.vol .\assets\*

# Add, update, delete, or rename files in a volume
7z a mymod.vol newskin.bmp
7z u mymod.vol updated.dts
7z d mymod.vol obsolete.bmp
7z rn mymod.vol old.bmp skins\new.bmp
```

## Build and Test

Install Visual Studio 2022 with C++ tools and CMake 3.20 or newer. Then run these commands:

```powershell
cmake -S . -B build-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build-x64 --config Release

cmake -S . -B build-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build build-x86 --config Release

python tests/test_plugin.py --dll build-x64/Release/TribesVol.64.dll
```
