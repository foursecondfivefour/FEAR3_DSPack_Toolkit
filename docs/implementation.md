# Implementation And Python Reference

## C++ Implementation

The maintained Windows implementation is `../dspack_cpp.cpp`, built as CLI and GUI by `../build_dspack_cpp.ps1`.

- `MappedFile` opens the source archive read-only and maps it into memory.
- `Pack::Parse()` validates the common index fields, records, string table, and payload bounds.
- Path helpers resolve names and reject traversal; C++ also rejects rooted/drive paths and output collisions within one archive.
- `LzdBlock()`/`LzdResource()` decode LZD.
- `ZlibApi`/`ZlibResource()` decode ZLib through a local `zlib1.dll`.
- `ExtractSingleArchive()` is shared by the GUI and batch CLI; it dispatches files to a bounded worker pool.
- The Win32 GUI uses background workers, cancellation, posted progress updates, and the Windows folder picker.

See [C++ GUI And CLI](cpp-fast-extractor.md) for build, usage, runtime dependency, and validation.

## Older Python Reference

`../dspack.py` is the **older, slower reference implementation**. It has a single-archive CLI, no GUI, and no native worker pool. It remains for readable algorithm study, comparison, and its experimental 7-Zip LZX bridge. The LZX bridge has not been checked against a real FEAR 3 LZX member. Do not assume Python-only behavior exists in C++.

## Validation

- One LZD call captured from the live game in x32dbg produced output with identical SHA-256 in Python and the game buffer.
- C++ `Boot.dsPack` (2,464 entries) and `Patch.dsPack` (40 entries) matched previous extracted references byte-for-byte.
- Selected large LZD resources from `Resource.dsPack` matched Python output byte-for-byte.
- Synthetic raw and wrapped ZLib fixtures matched original bytes.
- No installed archive contained internally compressed ZLib or LZX entries. ZLib has synthetic-fixture coverage only; LZX has no real-game end-to-end test.
- A warm-cache Boot extraction measured 0.58 s C++ vs 19.0 s Python on the development machine, approximately 33x in that run. This is illustrative only.

## Limits

- Only `.dsPack` v107/v108.
- Common indexes are parsed; auxiliary engine lookup structures are not fully reconstructed.
- C++ supports LZD/ZLib, not LZX. Python's LZX bridge is experimental.
- Audio conversion, archive writing/recompression, timestamp restoration, and exact duplicate-priority behavior are out of scope.
- `MiniPack` is a filesystem/mount behavior, not compression.
