# FEAR 3 dsPack Notes

This documentation describes the community `dspack.py` reader/extractor and the `.dsPack` format details recovered from the FEAR 3 / Despair Engine binary. The tool is read-only with respect to source archives: it opens them for reading and writes extracted files to a destination directory.

## Pages

- [Quick Start](quick-start.md): commands, filtering, output layout, and safe extraction.
- [Archive Format](archive-format.md): magic, supported versions, header fields, indexes, and paths.
- [Compression](compression.md): LZD block decoding, the StreamCompression wrapper, and other engine codecs.
- [Implementation and Limits](implementation.md): how the Python prototype is structured, validation, and remaining gaps.

## Status At A Glance

| Capability | Status |
|---|---|
| Read v107/v108 file and directory indexes | Implemented |
| List logical asset paths and sizes | Implemented |
| Extract entries stored without internal compression | Implemented |
| Decode raw LZD blocks | Implemented and compared against the running game |
| Decode LZD StreamCompression blocks | Implemented; checksum, ranges, and output length are checked |
| Decode ZLib or LZX archive members | Not implemented |
| Rebuild or modify `.dsPack` archives | Not implemented |

The engine advertises ZLib, LZX, and LZD codec IDs. That does not mean this Python utility supports all three. See [Compression](compression.md) for the distinction between engine capability and extractor capability.

## Evidence And Confidence

Format and algorithm notes were recovered from static IDA analysis of the game executable and checked against installed archives. The LZD implementation was additionally checked against a live `sub_B44930` invocation in x32dbg: the game's 7,483-byte output and the Python output had identical SHA-256 hashes. This validates that captured input block, not every possible LZD input or every archive revision.

Field names such as directory parent or compression block offset are based on how game functions consume the fields. Unknown fields are intentionally labelled unknown rather than guessed.
