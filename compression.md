# Compression And Codecs

## Engine Codec Flags

IDA analysis of `sub_AA9AC0` shows these archive-level codec selections:

| Flag | Codec ID | Engine codec name | `dspack.py` |
|---:|---:|---|---|
| `0x08` | `0xC5735A0F` | ZLib | Not decoded |
| `0x10` | `0xC5735A10` | LZX | Not decoded |
| `0x20` | `0xC5735A11` | LZD | Decoded |

The game diagnostic `sub_A767C0` names ZLib, LZX, and LZD. Codec choice is archive-level and the Python helper chooses the first matching flag in ZLib, LZX, LZD order. `Audio.dsPack` advertises ZLib in its archive flags, but its entries have equal logical/stored sizes in the inspected install; they are copied as stored data. This is not an audio codec implementation.

Other notable flags observed in the engine:

- `0x01`: `AllowDuplicates` in the engine diagnostic.
- `0x02`: `InternalCompression`.
- `0x80000000`: `MiniPack` filesystem behavior, not a compression algorithm.

## Raw LZD Block

The LZD block decoder in `dspack.py` is based on engine functions `sub_B44930`, `sub_714AC0`, `sub_4694F0`, `sub_65C290`, `sub_7D7630`, `sub_AE3D50`, and `sub_78A130`.

At a high level it:

1. Reads a compact prefix-tree description and builds the symbol tree / 10-bit fast lookup table.
2. Initializes the engine-style bit reader over big-endian 32-bit words, consumed least-significant bit first after word loading.
3. Reads three 32-bit fields: LZ command count, expected output length, and offset of the literal-data section.
4. Executes literal runs and three match forms. Matches copy from already produced output, including overlapping back-references.
5. Reads a final literal run and requires the produced byte count to equal the expected logical size.

The implementation rejects truncated trees/bitstreams, impossible node references, invalid back-reference distances, and output-size mismatches.

## StreamCompression Wrapper

Some resources wrap several LZD blocks in a `StreamCompression` record. The game parser `sub_839990` reads a 32-byte header and then `12 * block_count` bytes of index data; `sub_A50420` seeks to block offsets and decodes a block on demand.

The current wrapper implementation reads eight big-endian DWORDs:

| Offset | Meaning used by decoder |
|---:|---|
| `+0x00` | checksum |
| `+0x04` | codec ID, required to be LZD (`0xC5735A11`) |
| `+0x08` | block count |
| `+0x0C` | total logical size |
| `+0x10` | max input/buffer-size field (included in checksum) |
| `+0x14` | logical block size |
| `+0x18` | auxiliary DWORD (included in checksum) |
| `+0x1C` | trailing DWORD, currently ignored |

Each 12-byte big-endian block record is interpreted as:

| Record offset | Meaning used by decoder |
|---:|---|
| `+0x00` | stored payload offset, relative to the wrapped stream |
| `+0x04` | stored payload size |
| `+0x08` | unknown/reserved DWORD, included in checksum |

The checksum starts at `0x811C9DC5` and folds header DWORDs 1 through 6, followed by all three DWORDs of each block record, with `hash = (hash * 16777619) XOR word` modulo 32 bits. This behavior is recovered from `sub_D7AD60` and `sub_8986D0` and validated on installed game resources.

The wrapper validates the checksum, table bounds, block data ranges, and final output size before returning decoded bytes. Unknown header/index fields are preserved as unknown in the documentation.

## Current Support Boundary

- LZD raw blocks and LZD StreamCompression-wrapped members are decoded.
- ZLib and LZX flags are recognized for reporting, but their compressed entries are skipped by default.
- `--raw-compressed` writes encoded bytes with a `.packed` suffix; it does not decode them.
- The implementation is for extraction, not archive creation or recompression.
