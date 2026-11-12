# Implementation And Limits

## Code Map

The implementation lives in `../dspack.py`.

- `Pack._read_header()` reads the prefix and common v107/v108 index fields.
- `_read_files()` and `_read_dirs()` parse fixed 24-byte records.
- `name()`, `directory_path()`, and `file_path()` resolve the string table and build logical asset paths.
- `lzd_decompress()` decodes one raw LZD block.
- `lzd_decompress_resource()` detects and decodes either a raw LZD payload or a StreamCompression wrapper.
- `Pack.extract()` streams stored entries to the destination and decodes compressed LZD entries.
- `main()` implements the CLI.

## Validation Performed

The recovered LZD decoder was checked against the installed FEAR 3 build in x32dbg:

1. A breakpoint on `sub_B44930` caught the game decoding a 4,580-byte block into a 7,483-byte output buffer.
2. The captured input was passed to the Python decoder.
3. The SHA-256 hashes of the full Python output and the game's output buffer matched exactly.

The implementation was also checked against installed archives. At the time of validation, every compressed entry in `Boot.dsPack` and `Patch.dsPack` decoded successfully; a distributed sample of `Resource.dsPack` compressed entries decoded successfully; all files in the extracted `other` archives matched their indexed logical sizes. Re-run validation after code or archive changes; these are results for the installed game files, not a guarantee for every release/modded pack.

## Deliberate Scope Limits

- Only `.dsPack` versions `0x5A5A0107` and `0x5A5A0108` are accepted.
- The parser consumes the common header/index fields needed for extraction. The engine reads additional version-dependent header and lookup data that this tool does not reconstruct.
- The game has ZLib and LZX paths, but this Python prototype does not implement those decoders.
- Audio formats inside `Audio.dsPack` are not decoded or converted. The tool only copies indexed payloads according to the pack compression metadata.
- Archive generation, patching, compression, timestamps, and duplicate-resolution behavior are out of scope.
- The purpose of `MiniPack` is a filesystem/lazy-mount behavior in the engine; the flag is not treated as a codec.

## Evidence Labels

This documentation distinguishes three confidence levels:

- **Observed in code:** static behavior visible in IDA pseudocode/disassembly, such as `sub_A50420` seeking to a block offset and reading a stored byte count.
- **Validated at runtime:** behavior compared against the live game in x32dbg, specifically one LZD block whose Python output hash matched the game buffer.
- **Interpretation:** names assigned to fields from their consumers. Where the engine does not reveal a field's purpose, it remains labelled unknown/reserved.

## Contributing Safely

- Keep archive access read-only; write outputs only under a caller-selected destination.
- Add checks before trusting counts, offsets, sizes, or decoded lengths from a pack.
- Validate changes against at least one raw LZD block and one StreamCompression-wrapped resource.
- Do not label ZLib/LZX or a future archive version as supported until its decoder/parser passes output validation against the game or independently verified assets.
