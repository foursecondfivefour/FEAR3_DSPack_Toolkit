# Implementation And Python Reference

## Current C++ Tool

The recommended implementation is `../dspack_cpp.cpp`, built into `dspack_cpp.exe` and `dspack_gui.exe` by `../build_dspack_cpp.ps1`.

- `MappedFile` opens the archive read-only and maps it into the process address space.
- `Pack::Parse()` validates the common v107/v108 header fields, 24-byte file/directory records, string table, and payload ranges.
- `Pack::Name()`, `DirectoryPath()`, and `FilePath()` reconstruct logical resource paths and reject traversal components.
- `LzdBlock()`/`LzdResource()` implement raw and wrapped LZD decoding.
- `ZlibApi`, `ZlibResource()`, and `ZlibResource` support zlib-wrapped data using a side-by-side `zlib1.dll`.
- `ExtractSingleArchive()` is shared by the GUI and modern batch CLI. It validates output path collisions within an archive, maps the archive once, and sends independent entries to a bounded worker pool.
- The GUI uses a background worker, cooperative cancellation, posted progress updates, and the Windows Common Item Dialog for folder selection.

See [C++ GUI And CLI](cpp-fast-extractor.md) for build commands, user workflow, runtime dependency, performance design, and validation.

## Older Python Version

`../dspack.py` is an **older, slower reference implementation**, not the primary user-facing tool. It remains useful for readable algorithm experiments, comparing decoded data, and attempting LZX through an experimental CAB/7-Zip bridge. It has a single-archive command-line interface and no GUI or native worker pool. Its `Pack.extract()` dispatches LZD, ZLib, and experimental LZX decoding; the LZX bridge has no real FEAR 3 LZX resource against which to confirm compatibility. Its LZD/ZLib results were validated separately.

Do not infer that Python-only behavior is present in C++. In particular, C++ does not include the Python experimental LZX bridge.

## Validation Performed

- A live game call to the LZD decoder in x32dbg was captured. Python output and the game's full output buffer had identical SHA-256 hashes for that sample block.
- C++ `Boot.dsPack` (2,464 entries) and `Patch.dsPack` (40 entries) output matched the prior extracted reference byte-for-byte. The newer batch CLI output for `Patch.dsPack` also matched all 40 reference resources.
- Selected large LZD entries from `Resource.dsPack`, including `mp_oil_env.dsworld` and `mp_oil_audio.dsworld`, matched the Python decoder byte-for-byte.
- Synthetic raw and multi-block StreamCompression ZLib fixtures matched their original data.
- The installed archives contain no internally compressed ZLib entries and no LZX-compressed entries. Thus ZLib has no real installed-game payload test, and LZX has no real game payload test.
- A warm-cache test on this machine measured about 0.58 seconds for C++ versus 19.0 seconds for Python to extract `Boot.dsPack`, approximately 33x in that run. This is machine/cache dependent, not a general performance guarantee.

These checks apply to the installed archive set. Re-run comparisons after code changes, and do not generalize them to other releases or modded archives.

## Deliberate Limits

- Only `.dsPack` versions `0x5A5A0107` and `0x5A5A0108` are accepted.
- The parser consumes the common header/index fields needed for listing/extraction, not every auxiliary lookup table used by the engine.
- C++ decodes LZD and ZLib. LZX is unsupported and is skipped unless raw mode is selected.
- Python also attempts an experimental LZX-to-CAB bridge via 7-Zip, but that route is unverified on real FEAR 3 data.
- Audio payloads are copied according to pack metadata; audio codec conversion is outside scope.
- Archive creation, editing, recompression, timestamp restoration, and engine duplicate-priority resolution are out of scope.
- `MiniPack` is an engine filesystem/lazy-mount behavior, not a compression codec.

## Evidence Labels

- **Observed statically:** behavior visible in IDA pseudocode/disassembly, such as archive enumeration and block reads.
- **Observed dynamically:** behavior captured in x32dbg, such as opening/reading an archive or running the game LZD decoder.
- **Validated output:** bytes compared against a game buffer or a previously verified extracted resource.
- **Interpretation:** field names inferred from how consumers use a value. Unknown fields remain labelled unknown.
