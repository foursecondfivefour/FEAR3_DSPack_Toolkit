# Quick Start

## Recommended: GUI

Build once if the executables are not present:

```powershell
.\build_dspack_cpp.ps1
```

Then launch either `dspack_gui.exe` or `dspack_cpp.exe` without arguments. The GUI starts with the game folder set to the tool's directory, defaults output to the sibling `fear3-out` folder, and scans for `.dsPack` archives automatically.

1. Confirm or browse to the FEAR 3 installation folder.
2. Choose the output folder. Each archive gets its own named subfolder beneath it by default.
3. Select the archives to extract. The initial scan selects all found archives; use Ctrl/Shift selection, **Select all**, or **Select none** to change that.
4. Optionally set a case-insensitive path substring filter and worker count. The default worker count is the detected logical CPU count, capped at 32.
5. Choose **Extract selected**. The progress bar/status update while work runs. **Cancel** cooperatively stops scheduling more entries; completed files remain, while the file being written at cancellation may be partial.

## C++ CLI

Scan archives recursively under the game folder:

```powershell
.\dspack_cpp.exe scan --game "C:\Games\F.E.A.R. 3"
```

List an archive or filter the list:

```powershell
.\dspack_cpp.exe list --game "C:\Games\F.E.A.R. 3" --archive Resource.dsPack --filter mp_oil
```

Extract selected archives. Repeat `--archive` to choose more than one; by default each archive is written to its own subdirectory to avoid collisions:

```powershell
.\dspack_cpp.exe extract --game "C:\Games\F.E.A.R. 3" --out "C:\Games\fear3-out\other" --archive Boot.dsPack --archive Patch.dsPack --threads 12 --progress
```

Omit `--archive` to extract every discovered archive. Use a fresh output directory if you need to preserve earlier results; files at matching paths are overwritten. `--flat` disables archive subdirectories and is advanced because equal internal paths may overwrite each other. Ctrl+C requests cooperative cancellation.

Other useful switches:

- `--filter TEXT`: case-insensitive substring filter on logical resource paths; not a glob/regex.
- `--threads N`: set worker count; default detects logical processors, capped at 32 and selected-entry count.
- `--raw-compressed`: preserve compressed payloads as `.packed` rather than decoding them.
- `--progress`: print CLI progress rows.
- `--help`: print command usage.

## Legacy Python Reference

`dspack.py` is the older, slower single-archive CLI. It is retained as a readable reference and for its experimental 7-Zip LZX bridge; use the C++ GUI/CLI for ordinary extraction.

```powershell
py -3 .\dspack.py .\resources\Patch.dsPack --list
py -3 .\dspack.py .\resources\Patch.dsPack --extract C:\Temp\fear3-patch
```

Python usage and its codec caveats are documented in [Implementation And Python Reference](implementation.md) and [Compression](compression.md).

## Safety

- Source archives are opened read-only; no archive is rebuilt or modified.
- Always use a destination folder you control. Existing files at matching paths are overwritten.
- A failed/cancelled run does not roll back completed files, and the file being written may remain partial.
- Unknown/unsupported compressed entries are skipped by default or preserved with `--raw-compressed`.
