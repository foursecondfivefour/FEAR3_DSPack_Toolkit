# FEAR 3 dsPack Toolkit

 read-only extractor for FEAR 3 / Despair Engine `.dsPack` archives. The recommended tool is a native Windows C++20 CLI and GUI; an older Python implementation is kept as a reference.

> **Before making this repository public:** choose and add an open-source license. No license is included yet, so default copyright rules apply and others do not automatically have permission to reuse or redistribute this code.

## Features

- Scan a FEAR 3 installation for `.dsPack` archives.
- Browse and select archives in a native Windows GUI.
- List, filter, and batch-extract indexed resources.
- Decode LZD and ZLib resources, including the engine's `StreamCompression` wrapper.
- Extract independent files in parallel; source archives are opened read-only.
- Keep unsupported compressed payloads with `.packed` using raw mode.
- Python reference script for comparison and an experimental, unverified LZX-to-CAB/7-Zip bridge.

## Quick Start

Requirements for building on Windows 10/11:

- Visual Studio 2022 or Build Tools with the x64 MSVC C++ workload and Windows SDK.
- Python 3 installed only to provide a local `zlib1.dll` when building/running ZLib support. The Python interpreter is not needed to run the finished executables.

Build both applications from a Developer PowerShell or ordinary PowerShell:

```powershell
./build_dspack_cpp.ps1
```

Run `dspack_gui.exe`, or run `dspack_cpp.exe` without arguments to open the GUI. The GUI scans for `.dsPack` archives, lets you choose a game folder and output folder, select archives, set an optional path filter and worker count, and monitor/cancel extraction.

CLI examples:

```powershell
./dspack_cpp.exe scan --game "C:\Games\F.E.A.R. 3"
./dspack_cpp.exe list --game "C:\Games\F.E.A.R. 3" --archive Resource.dsPack --filter mp_oil
./dspack_cpp.exe extract --game "C:\Games\F.E.A.R. 3" --out "C:\Games\fear3-out" --archive Boot.dsPack --archive Patch.dsPack --threads 8 --progress
```

Batch extraction writes each archive to its own subfolder by default. Omit `--archive` to extract all discovered archives. `--raw-compressed` preserves compressed payloads rather than decoding them. Ctrl+C requests cooperative cancellation. Existing output files may be overwritten; use a fresh destination when preserving prior output matters.

## Codec Support

| Codec | C++ GUI/CLI | Python reference |
|---|---|---|
| LZD | Supported, native | Supported |
| ZLib | Supported through side-by-side `zlib1.dll` | Supported through Python `zlib` |
| LZX | Not supported; skip or preserve raw | Experimental 7-Zip CAB bridge, not validated on a real FEAR 3 LZX resource |

The installed FEAR 3 archives tested so far contain LZD-compressed entries, no internally compressed ZLib entries, and no LZX entries. The LZX bridge is included for research only, not as a verified feature.

## Documentation

See [`docs/`](docs/README.md) for the format notes, codec analysis, build and CLI/GUI instructions, validation evidence, and known limitations.

## What This Project Does Not Include

- No FEAR 3 game archives, executable, assets, or extracted game content.
- No archive writer, patcher, or recompressor.
- No guarantee that every game release or modded archive is compatible.
- No verified LZX decoder.

## License

No license has been selected. Add a license file before publishing this repository publicly. The game and its archives are copyrighted by their respective owners and are not part of this project.
