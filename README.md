# FEAR 3 dsPack Tools

This project contains a native Windows C++ extractor for FEAR 3 `.dsPack` archives and an older Python reference implementation. **For normal use, start with `dspack_gui.exe` or run `dspack_cpp.exe` without arguments.** The C++ GUI/CLI is the maintained, faster tool. `dspack.py` remains available for inspection, comparison, and its experimental LZX bridge, but it is no longer the primary extractor.

Both tools read archives without modifying them and write extracted resources to a destination folder.

## Documentation

- [Quick Start](quick-start.md): recommended GUI workflow and C++ CLI commands.
- [Archive Format](archive-format.md): archive header, indexes, names, and output paths.
- [Compression](compression.md): LZD, ZLib, LZX, StreamCompression, and per-tool support.
- [C++ GUI And CLI](cpp-fast-extractor.md): build, controls, commands, performance, validation, and limits.
- [Implementation And Python Reference](implementation.md): C++ architecture, old Python script, and verification notes.

## Feature Matrix

| Capability | C++ GUI/CLI (recommended) | Python `dspack.py` (older) |
|---|---|---|
| Read `.dsPack` v107/v108 index | Yes | Yes |
| List/filter resource paths | Yes | Yes |
| Extract uncompressed entries | Yes | Yes |
| Decode LZD, raw and wrapped | Yes, native and parallel by file | Yes, Python implementation |
| Decode ZLib, raw and wrapped | Yes, side-by-side `zlib1.dll` | Yes, Python standard library |
| Decode LZX | No; reported as unsupported or saved raw | Experimental CAB/7-Zip bridge; not validated on a real FEAR 3 LZX member |
| Batch-select archives / output subfolder per archive | Yes | No built-in batch workflow |
| Native GUI with progress/cancel | Yes | No |
| Rewrite/create archives | No | No |

The installed FEAR 3 archives tested so far contain LZD-compressed resources, no internally compressed ZLib members, and no LZX-compressed members. LZX support is therefore not needed for the verified installed set; the Python bridge is retained only as an experimental avenue for other builds or modified archives.

## Evidence And Confidence

Archive layout and codecs were investigated with IDA and live x32dbg observation of the game. A captured LZD input block produced identical SHA-256 output in the game and Python reference implementation. C++ output was compared byte-for-byte against Python and existing extracted results for installed resources. ZLib raw/wrapped support was tested with synthetic fixtures because the installed archives did not contain internally compressed ZLib entries. No real LZX resource was available to validate either tool's LZX behavior.
