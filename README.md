# T1VolumesFor7z

This plugin lets 7-Zip open, extract, create, and change Starsiege: Tribes (Tribes 1) and Starsiege `.vol` archives on Windows and Linux.

## Supported Formats

- **Read and extract:** Tribes 1 (`PVOL`) and Starsiege (` VOL`) volumes with Store (`0`), RLE (`1`), and LZH (`3`) compression.
- **Create and change:** Add, update, delete, and rename files in `.vol` archives. The plugin writes Store (`0`) by default and supports optional RLE (`1`) compression.
- **ZIP volumes:** ZIP-based `.vol` and Tribes 2 `.vl2` archives open with the built-in 7-Zip ZIP handler.

## Requirements

- **Windows:** Official 7-Zip (64-bit or 32-bit). The DLL uses a static C runtime and needs no Visual C++ Redistributable.
- **Linux:** Modular 7-Zip v23 or newer (`7z` with `7z.so`, from distro packages such as `apt install 7zip`, `dnf install 7zip`, or `pacman -S 7zip`).

> **NOTE:** On Linux, this plugin does not work with the standalone `7zz` binary because `7zz` does not load external plugins.

## Install on Windows

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

## Install on Linux

1. Install modular 7-Zip (`sudo apt install 7zip`, `sudo dnf install 7zip`, or `sudo pacman -S 7zip`).
2. Download `T1VolumesFor7z-linux-x64.tar.gz` from the Releases page.
3. Extract the archive to a folder.
4. Run the install script:

```bash
sudo ./install.sh
```

As an alternative, copy `TribesVol.so` into the `Formats` folder next to `7z.so` (usually `/usr/lib/7zip/Formats/` or `/usr/libexec/7zip/Formats/`).

To verify the installation on either OS, run:

```bash
7z i | grep TribesVol          # Linux
7z i | Select-String TribesVol # Windows PowerShell
```

## Remove the Plugin

- **Windows:** Open PowerShell as Administrator and run `.\uninstall.ps1`, or delete `TribesVol.*.dll` from the 7-Zip `Formats` folder.
- **Linux:** Run `sudo ./uninstall.sh`, or delete `TribesVol.so` from the 7-Zip `Formats` folder.

## Command-Line Examples

```bash
# List, test, or extract a volume
7z l Entities.vol
7z t Entities.vol
7z x gui.vol -oextracted_gui

# Create a volume (Store compression, default)
7z a mymod.vol ./assets/*

# Create a volume with RLE compression
7z a -m0=RLE mymod_rle.vol ./assets/*

# Add, update, delete, or rename files in a volume
7z a mymod.vol newskin.bmp
7z u mymod.vol updated.dts
7z d mymod.vol obsolete.bmp
7z rn mymod.vol old.bmp skins/new.bmp
```

## Build and Test

### Windows

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release

cmake -S . -B build-x86 -A Win32
cmake --build build-x86 --config Release

python tests/test_plugin.py --dll build-x64/Release/TribesVol.64.dll
```

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

python3 tests/test_plugin.py --dll build/TribesVol.so
```

To build for legacy `p7zip` (older than 7-Zip v23), add `-DTRIBESVOL_LEGACY_P7ZIP=ON` to the CMake configure command.
