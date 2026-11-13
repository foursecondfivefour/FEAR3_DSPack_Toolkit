# Quick Start

## Recommended: GUI

Build once if the executables are not present:

```powershell
./build_dspack_cpp.ps1
```

Then launch `dspack_gui.exe`, or run `dspack_cpp.exe` without arguments. The GUI scans for `.dsPack` files and lets you choose the game folder, output folder, archive selection, path filter, and worker count.

- Choose the FEAR 3 installation folder.
- Choose an output folder. Each archive is written into its own named subfolder by default.
- Select archives from the list. The initial scan selects all found archives; Ctrl/Shift selection and **Select all/none** are available.
- Set an optional case-insensitive path substring and worker count. Default: detected logical CPU count, capped at 32.
- Choose **Extract selected**. Use **Cancel** for cooperative cancellation; completed files remain, and the file currently being written may be partial.

## C++ CLI

Scan a game directory recursively:

```powershell
./dspack_cpp.exe scan --game "C:\Games\F.E.A.R. 3"
```

List resources from a chosen archive:

```powershell
./dspack_cpp.exe list --game "C:\Games\F.E.A.R. 3" --archive Resource.dsPack --filter mp_oil
```

Extract selected archives:

```powershell
./dspack_cpp.exe extract --game "C:\Games\F.E.A.R. 3" --out "C:\Games\fear3-out\other" --archive Boot.dsPack --archive Patch.dsPack --threads 8 --progress
```

Omit `--archive` to process all discovered archives. Each archive gets a separate output subfolder by default. `--flat` disables these subfolders and can cause same-path resources in different archives to overwrite one another. `--raw-compressed` preserves compressed data as `.packed`; otherwise LZD/ZLib are decoded and unsupported codecs are skipped. Ctrl+C requests cooperative cancellation.

## Older Python Reference

`dspack.py` is the older, slower single-archive implementation. Use it for reference/comparison, not as the main tool:

```powershell
py -3 ./dspack.py ./resources/Patch.dsPack --list
py -3 ./dspack.py ./resources/Patch.dsPack --extract C:\Temp\fear3-patch
```

It also attempts an experimental LZX bridge through 7-Zip, but this has not been tested with a real FEAR 3 LZX payload.

## Safety

- Input archives are read-only.
- Existing output files with matching paths are overwritten; use a fresh destination to preserve earlier work.
- Cancel/failure does not roll back completed files; an in-progress file may remain partial.
- Unsupported compressed entries are skipped by C++ by default or preserved in raw mode.
