# C++ Fast Extractor

The native Windows implementation is `../dspack_cpp.cpp`; `../build_dspack_cpp.ps1` builds `dspack_cpp.exe` and `dspack_gui.exe` with the installed x64 MSVC toolchain. It does not overwrite the older `dspack.py` reference script. Launching `dspack_cpp.exe` without arguments opens the GUI when the GUI executable is beside it.

## Build

From the FEAR 3 install directory:

```powershell
.\build_dspack_cpp.ps1
```

The script uses the Visual Studio 2022 x64 environment, compiles an optimized C++20 CLI and Windows-subsystem GUI, and copies the local Python 3.14 `zlib1.dll` into `dspack_cpp_runtime\`. The CLI resolves that dependency relative to itself. No Python interpreter is needed to run the resulting executables; the DLL must remain beside them in the runtime subfolder for ZLib decoding. The build script currently expects the documented local MSVC and Python runtime paths; edit those variables if the tools are installed elsewhere.

The command-line toolchain uses the standard Visual Studio developer environment (`vcvarsall.bat amd64`) as described by [Microsoft's command-line build documentation](https://learn.microsoft.com/cpp/build/building-on-the-command-line?view=msvc-170). The source uses the C++ filesystem and thread facilities implemented by [Microsoft STL](https://github.com/microsoft/STL).

## Usage

### GUI

Run `dspack_gui.exe` or launch `dspack_cpp.exe` with no arguments. The GUI defaults the game folder to the tool's folder and the output folder to `fear3-out`; it scans for `.dsPack` files automatically. Choose folders with **Browse**, use **Scan** to refresh, select archives from the list (all found archives start selected), adjust the optional path filter or worker count, then choose **Extract selected**. Each archive gets its own subfolder below the output directory to avoid cross-archive filename collisions. **Cancel** stops scheduling more files after the current file finishes; completed files remain, and an in-progress file may be partial.

The folder picker uses Windows' modern `IFileOpenDialog` with `FOS_PICKFOLDERS` on an STA-initialized UI thread, following the [Microsoft Common Item Dialog guidance](https://learn.microsoft.com/en-us/windows/win32/shell/common-file-dialog). Work runs off the UI thread and progress returns through posted window messages; the progress control is updated on the UI thread.

### CLI

The command-line program has explicit subcommands for scripts and batch workflows:

```powershell
.\dspack_cpp.exe scan --game "C:\Games\F.E.A.R. 3"
.\dspack_cpp.exe list --game "C:\Games\F.E.A.R. 3" --archive Resource.dsPack --filter mp_oil
.\dspack_cpp.exe extract --game "C:\Games\F.E.A.R. 3" --out "C:\Games\fear3-out" --archive Boot.dsPack --archive Patch.dsPack --threads 12 --progress
```

`scan` finds archives recursively and reports codec, indexed file count and size. `list` displays selected archive entries. `extract` extracts all discovered archives by default, or only repeated `--archive NAME` selections; every selected archive gets an output subfolder named after the archive. `--flat` disables those subfolders and is advanced because same-named internal paths may overwrite one another. Duplicate archive names that would map to the same output folder are rejected. `--filter` is a case-insensitive substring match; `--threads N` limits workers; `--raw-compressed` writes encoded payloads with `.packed`. Ctrl+C requests cooperative cancellation. The legacy one-archive form remains available:

```powershell
.\dspack_cpp.exe .\resources\Patch.dsPack --extract C:\Temp\Patch --threads 8
```

Legacy single-archive options:

- `--list`: list indexed entries without extraction.
- `--extract DIR`: write files under a caller-selected destination.
- `--filter TEXT`: case-insensitive UTF-8 substring filter.
- `--threads N`: set the worker count; default is detected logical processor count, capped at 32 and at selected file count.
- `--raw-compressed`: keep compressed payloads as `<name>.packed` instead of decoding them.

Use a new destination directory when preserving previous output matters. Existing files at output paths are overwritten; archive files are opened read-only.

## Performance Design

- Maps the source archive read-only, avoiding shared seek state and per-entry input reads.
- Parses and validates the index once, then schedules selected entries through a bounded worker pool.
- Gives each entry independent decoder state and writes its own output file directly.
- Decodes independent LZD resources concurrently. Blocks inside a single wrapped resource are decoded in order to avoid nested-thread oversubscription; file-level workers provide the main parallelism.
- Caps the default worker count at 32. More threads are not guaranteed to improve throughput; tune `--threads` for the CPU, storage, and destination filesystem.

## Supported Codecs

- LZD raw blocks and LZD `StreamCompression` wrappers are decoded natively.
- ZLib raw streams and ZLib `StreamCompression` wrappers are decoded through the side-by-side `zlib1.dll` runtime.
- LZX is not decoded by this C++ build. Such entries are counted as `skipped unsupported` unless `--raw-compressed` is specified.
- Unknown compressed codecs are also skipped unless raw mode is requested.
- The original Python prototype has an experimental external 7-Zip LZX bridge; that code is not ported into the C++ executable.

For more algorithm detail and the distinction between engine codec IDs and extractor support, see [Compression](compression.md).

## Validation And Measurements

On the installed FEAR 3 build:

- All 2,464 files in `Boot.dsPack` matched the earlier extracted reference byte-for-byte.
- All 40 files in `Patch.dsPack` matched the earlier extracted reference byte-for-byte.
- Selected large `Resource.dsPack` LZD outputs (`mp_oil_env.dsworld` and `mp_oil_audio.dsworld`) matched Python decoder output byte-for-byte.
- Synthetic raw and two-block ZLib fixtures matched their original bytes.
- On this machine, full `Boot.dsPack` extraction measured about 0.58 s for C++ versus 19.0 s for Python, around 33x in that run. Results are filesystem-cache sensitive and are not a general benchmark guarantee.

The installed archives contain no internally compressed LZX resources, so the C++ LZX path remains explicitly unsupported rather than unverified code pretending to decode it.
