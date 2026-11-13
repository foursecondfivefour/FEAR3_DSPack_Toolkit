# C++ GUI And CLI

The native Windows implementation is `../dspack_cpp.cpp`; `../build_dspack_cpp.ps1` builds `dspack_cpp.exe` and `dspack_gui.exe`. The Python script is older and remains a reference.

## Build Requirements

- Windows 10/11.
- Visual Studio 2022 or Build Tools with x64 MSVC and Windows SDK.
- `zlib1.dll`. The build script searches Python 3's DLL folder; Python is only a convenient source of the DLL, not a runtime requirement. A compatible DLL path can be supplied explicitly.

```powershell
./build_dspack_cpp.ps1
# Optional explicit zlib path:
./build_dspack_cpp.ps1 -ZlibDllPath C:\path\to\zlib1.dll
```

The script locates `vcvarsall.bat` via `vswhere` or standard Visual Studio folders, then builds optimized x64 C++20 CLI and GUI binaries and copies the zlib DLL to `dspack_cpp_runtime`. The toolchain follows [Microsoft's MSVC command-line build guidance](https://learn.microsoft.com/cpp/build/building-on-the-command-line?view=msvc-170) and uses [Microsoft STL](https://github.com/microsoft/STL).

## GUI

Launch `dspack_gui.exe`, or run `dspack_cpp.exe` without arguments. It defaults the game path to the executable's directory, output to a sibling `fear3-out`, and scans archives. Select archives, output folder, optional filter and worker count, then click **Extract selected**. Each archive gets its own output folder. Cancel is cooperative: completed files remain and a file being written may be partial.

The folder picker uses `IFileOpenDialog` with `FOS_PICKFOLDERS`; work runs off the UI thread and progress is posted back to the UI.

## CLI

```powershell
./dspack_cpp.exe scan --game "C:\Games\F.E.A.R. 3"
./dspack_cpp.exe list --game "C:\Games\F.E.A.R. 3" --archive Resource.dsPack --filter mp_oil
./dspack_cpp.exe extract --game "C:\Games\F.E.A.R. 3" --out "C:\Games\fear3-out" --archive Boot.dsPack --archive Patch.dsPack --threads 8 --progress
```

- `scan`: recursively find `.dsPack`, report codec/count/size.
- `list`: list entries; `--archive NAME` is repeatable.
- `extract`: all discovered archives by default, or repeated `--archive NAME`; one output folder per archive.
- `--filter TEXT`: case-insensitive resource-path substring.
- `--threads N`: default logical CPU count, capped at 32 and selected entry count.
- `--progress`: print progress rows.
- `--raw-compressed`: save encoded payloads as `.packed`.
- `--flat`: advanced; disables per-archive output folders and may cause cross-archive collisions.
- Ctrl+C requests cooperative cancellation.

Legacy syntax remains available:

```powershell
./dspack_cpp.exe ./resources/Patch.dsPack --extract C:\Temp\Patch --threads 8
```

## Performance And Safety

- Read-only memory mapping of input.
- Index parsed once per archive.
- Bounded worker pool, independent files decoded concurrently, direct-to-file output.
- Paths and per-archive normalized output collisions are checked.
- Existing output paths may be overwritten; choose a fresh output directory to preserve prior work.

## Codec Support And Validation

C++ decodes LZD natively and ZLib through side-by-side `zlib1.dll`. LZX is unsupported: skip or preserve raw. Unknown compressed codecs are also skipped/preserved raw.

On installed data, Boot (2,464 files) and Patch (40 files) matched prior outputs byte-for-byte; selected large Resource LZD entries matched Python; synthetic ZLib fixtures matched expected bytes. A warm-cache Boot run measured 0.58 s vs 19.0 s Python on one development machine (~33x in that run). No real ZLib-compressed or LZX-compressed resource was present for end-to-end validation.
